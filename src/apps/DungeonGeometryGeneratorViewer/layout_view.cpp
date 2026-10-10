#include "pch.h"

#include "layout_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>

namespace dg = dungeon_geometry_generator;

namespace {

// Deterministic fallback node positions for a graph without a generated
// layout: BFS layers from the entry room (else the first room by id), x =
// layer, y = centered index within the layer, disconnected components follow
// with a one-layer gap. Coordinates are world grid cells, like the layout
// centroids that replace them after Generate.
void fallbackPositions(const dg::LayoutParams& g, std::map<std::string, std::pair<double, double>>& pos) {
    std::map<std::string, std::vector<std::string>> adj;
    for (const dg::Passage& p : g.passages) {
        adj[p.a].push_back(p.b);
        adj[p.b].push_back(p.a);
    }
    for (auto& [_, nbs] : adj) {
        std::sort(nbs.begin(), nbs.end());
        nbs.erase(std::unique(nbs.begin(), nbs.end()), nbs.end());
    }

    std::vector<std::string> ids;
    for (const dg::GraphRoom& r : g.rooms) ids.push_back(r.id);
    std::sort(ids.begin(), ids.end());
    const auto isEntry = [&g](const std::string& id) {
        for (const dg::GraphRoom& r : g.rooms)
            if (r.id == id) return r.role == "entry";
        return false;
    };
    std::stable_partition(ids.begin(), ids.end(), isEntry);  // entry roots first

    std::map<std::string, int> depth;
    int nextBase = 0;
    for (const std::string& root : ids) {
        if (depth.count(root)) continue;
        std::queue<std::pair<std::string, int>> q;
        q.push({root, nextBase});
        depth[root] = nextBase;
        int maxD = nextBase;
        while (!q.empty()) {
            const auto [id, d] = q.front();
            q.pop();
            maxD = std::max(maxD, d);
            for (const std::string& nb : adj[id])
                if (!depth.count(nb)) {
                    depth[nb] = d + 1;
                    q.push({nb, d + 1});
                }
        }
        nextBase = maxD + 2;  // one empty column between components
    }

    constexpr double kDx = 6.0;  // cells between layers (graph node boxes are ~4 cells wide at default zoom)
    constexpr double kDy = 3.0;  // cells between nodes of one layer
    std::map<int, std::vector<std::string>> layers;
    for (const std::string& id : ids) layers[depth[id]].push_back(id);
    for (auto& [d, layer] : layers) {
        for (size_t i = 0; i < layer.size(); ++i)
            pos[layer[i]] = {d * kDx, (static_cast<double>(i) - (layer.size() - 1) * 0.5) * kDy};
    }
}

// --- graph edit helpers (mutate LayoutParams in place) ------------------------

bool hasRoom(const dg::LayoutParams& g, const std::string& id) {
    for (const dg::GraphRoom& r : g.rooms)
        if (r.id == id) return true;
    return false;
}

void deleteRoom(dg::LayoutParams& g, const std::string& id) {
    g.editor_node_pos.erase(id);
    g.passages.erase(std::remove_if(g.passages.begin(), g.passages.end(),
                                    [&](const dg::Passage& p) { return p.a == id || p.b == id; }),
                     g.passages.end());
    g.rooms.erase(std::remove_if(g.rooms.begin(), g.rooms.end(),
                                 [&](const dg::GraphRoom& r) { return r.id == id; }),
                  g.rooms.end());
}

void deletePassage(dg::LayoutParams& g, const std::string& a, const std::string& b) {
    g.passages.erase(std::remove_if(g.passages.begin(), g.passages.end(),
                                    [&](const dg::Passage& p) {
                                        return (p.a == a && p.b == b) || (p.a == b && p.b == a);
                                    }),
                     g.passages.end());
}

bool hasPassage(const dg::LayoutParams& g, const std::string& a, const std::string& b) {
    for (const dg::Passage& p : g.passages)
        if ((p.a == a && p.b == b) || (p.a == b && p.b == a)) return true;
    return false;
}

// Sorted room ids (the add-passage combos and deterministic iteration).
std::vector<std::string> roomIds(const dg::LayoutParams& g) {
    std::vector<std::string> ids;
    for (const dg::GraphRoom& r : g.rooms) ids.push_back(r.id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// --- canvas look: forked from topo_view.cpp's graph pane ----------------------
// The Layout editor and the read-only Topo graph share the visual language;
// the helpers are anonymous there, so the fork keeps its own copies.

constexpr ImU32 kRolePalette[] = {
    IM_COL32(130, 180, 250, 255),  // blue
    IM_COL32(120, 210, 140, 255),  // green
    IM_COL32(210, 130, 200, 255),  // magenta
    IM_COL32(230, 210, 110, 255),  // yellow
    IM_COL32(150, 170, 235, 255),  // indigo
    IM_COL32(120, 200, 200, 255),  // teal
    IM_COL32(235, 150, 120, 255),  // salmon
    IM_COL32(170, 200, 130, 255),  // olive
};
constexpr ImU32 kNeutral = IM_COL32(150, 150, 160, 255);
constexpr ImU32 kDoorOpen = IM_COL32(90, 220, 90, 255);
constexpr ImU32 kDoorOther = IM_COL32(240, 160, 60, 255);
constexpr ImU32 kSelection = IM_COL32(255, 230, 120, 255);
constexpr float kGraphNodeW = 88.0f;
constexpr float kGraphNodeH = 36.0f;
constexpr ImU32 kGraphBg = IM_COL32(24, 27, 32, 255);

constexpr const char* kRoles[] = {"hall", "corridor", "crypt", "entry", "stairs"};
constexpr const char* kDoors[] = {"open", "gate"};

ImU32 withAlpha(ImU32 c, unsigned a) { return (c & 0x00FFFFFF) | (a << 24); }

ImU32 roleColor(const dg::TopoModel& model, const std::string& role) {
    const size_t n = sizeof(kRolePalette) / sizeof(kRolePalette[0]);
    for (size_t i = 0; i < model.roles.size(); ++i)
        if (model.roles[i] == role) return kRolePalette[i % n];
    return kNeutral;
}

ImVec2 toScreen(const TopoCam& cam, const ImVec2& origin, double x, double y) {
    return ImVec2(origin.x + cam.offsetX + static_cast<float>(x * cam.zoom),
                  origin.y + cam.offsetY + static_cast<float>(y * cam.zoom));
}

ImVec2 toWorld(const TopoCam& cam, const ImVec2& local) {
    return ImVec2((local.x - cam.offsetX) / cam.zoom, (local.y - cam.offsetY) / cam.zoom);
}

void zoomToCursor(TopoCam& cam, const ImVec2& local, float factor) {
    const ImVec2 w = toWorld(cam, local);
    cam.zoom = std::clamp(cam.zoom * factor, 2.0f, 256.0f);
    cam.offsetX = local.x - static_cast<float>(w.x * cam.zoom);
    cam.offsetY = local.y - static_cast<float>(w.y * cam.zoom);
}

// Fit the graph camera like topo_view.cpp's fitGraphCam: the node boxes are
// fixed-size in points, so the world bbox is expanded by half a box before
// solving the zoom.
void fitGraphCam(TopoCam& cam, const dg::TopoModel& model, float w, float h) {
    double minx = 0.0, maxx = 0.0, miny = 0.0, maxy = 0.0;
    bool any = false;
    for (const auto& n : model.nodes) {
        if (!n.hasLayout) continue;
        if (!any) {
            minx = n.minx;
            maxx = n.maxx;
            miny = n.miny;
            maxy = n.maxy;
            any = true;
        } else {
            minx = std::min(minx, static_cast<double>(n.minx));
            maxx = std::max(maxx, static_cast<double>(n.maxx));
            miny = std::min(miny, static_cast<double>(n.miny));
            maxy = std::max(maxy, static_cast<double>(n.maxy));
        }
    }
    double spanX = std::max(maxx - minx, 1.0) + 3.0;
    double spanY = std::max(maxy - miny, 1.0) + 3.0;
    const float z0 = std::clamp(static_cast<float>(std::min(w / spanX, h / spanY)), 2.0f, 96.0f);
    spanX += 2.0 * (kGraphNodeW * 0.5 + 16.0) / z0;
    spanY += 2.0 * (kGraphNodeH * 0.5 + 16.0) / z0;
    cam.zoom = std::clamp(static_cast<float>(std::min(w / spanX, h / spanY)), 2.0f, 96.0f);
    const double midX = (minx + maxx) * 0.5;
    const double midY = (miny + maxy) * 0.5;
    cam.offsetX = static_cast<float>(w * 0.5 - midX * cam.zoom);
    cam.offsetY = static_cast<float>(h * 0.5 - midY * cam.zoom);
}

// Screen-space rect of an edge's door label plate (the drawing pass uses the
// same math — keep them in sync).
void edgeLabelRect(const dg::TopoEdge& e, const TopoCam& cam, const ImVec2& rmin, ImVec2& a,
                   ImVec2& b) {
    const ImVec2 at = toScreen(cam, rmin, e.lx, e.lz);
    const ImVec2 ts = ImGui::CalcTextSize(e.door.c_str());
    a = ImVec2(at.x - ts.x * 0.5f - 3.0f, at.y - ts.y * 0.5f - 2.0f);
    b = ImVec2(at.x + ts.x * 0.5f + 3.0f, at.y + ts.y * 0.5f + 2.0f);
}

}  // namespace

void buildLayoutGraphModel(const Level& level, dg::TopoModel& out) {
    out = dg::TopoModel{};
    if (!level.loaded || !level.project.layout) return;
    const dg::LayoutParams& g = *level.project.layout;
    out = dg::build_topo(g, level.generated ? level.layoutData : dg::LayoutData{});

    // The editor canvas edits the graph: layout-only ghosts (a stale layout
    // room whose graph room was deleted) are not shown.
    {
        std::unordered_set<std::string> ids;
        for (const dg::GraphRoom& r : g.rooms) ids.insert(r.id);
        out.nodes.erase(std::remove_if(out.nodes.begin(), out.nodes.end(),
                                       [&](const dg::TopoNode& n) { return !ids.count(n.id); }),
                        out.nodes.end());
    }

    std::map<std::string, std::pair<double, double>> fallback;
    fallbackPositions(g, fallback);
    std::unordered_map<std::string, size_t> idx;
    for (size_t i = 0; i < out.nodes.size(); ++i) idx.emplace(out.nodes[i].id, i);
    for (dg::TopoNode& n : out.nodes) {
        const auto pin = g.editor_node_pos.find(n.id);
        if (pin == g.editor_node_pos.end() && n.hasLayout) continue;  // real centroid + bbox
        double cx = 0.0, cz = 0.0;
        if (pin != g.editor_node_pos.end()) {
            cx = pin->second.first;
            cz = pin->second.second;
        } else {
            const auto it = fallback.find(n.id);
            if (it == fallback.end()) continue;
            cx = it->second.first;
            cz = it->second.second;
        }
        n.cx = cx;
        n.cz = cz;
        n.minx = static_cast<int>(std::floor(cx - 2.0));
        n.maxx = static_cast<int>(std::ceil(cx + 2.0));
        n.miny = static_cast<int>(std::floor(cz - 1.5));
        n.maxy = static_cast<int>(std::ceil(cz + 1.5));
        n.hasLayout = true;  // synthetic position: draw the node, keep the contour as-is
    }
    // Door labels sit at the edge midpoints; recompute over the final
    // positions (build_topo skipped edges with an unplaced endpoint).
    for (dg::TopoEdge& e : out.edges) {
        const auto ia = idx.find(e.a);
        const auto ib = idx.find(e.b);
        if (ia == idx.end() || ib == idx.end()) continue;
        e.lx = (out.nodes[ia->second].cx + out.nodes[ib->second].cx) * 0.5;
        e.lz = (out.nodes[ia->second].cz + out.nodes[ib->second].cz) * 0.5;
        e.labelOk = true;
    }
}

LayoutGraphActions drawLayoutGraphView(Level& level, const dg::TopoModel& model, Selection& selection,
                                       LayoutGraphState& st) {
    LayoutGraphActions res;
    dg::LayoutParams* graph =
        level.loaded && level.project.layout ? &*level.project.layout : nullptr;
    TopoGraphState& gst = st.graph;

    if (!graph) {
        ImGui::TextDisabled("(no layout tier in the project)");
        return res;
    }

    // --- toolbar -------------------------------------------------------------
    const float fullW = ImGui::GetContentRegionAvail().x;

    bool wantFit = false;
    bool wantDelete = false;
    if (ImGui::SmallButton("Add room")) {
        st.addRoomOpen = true;
        st.addRoomErr.clear();
        // Suggest the first free roomN id; spawn at the canvas center.
        for (int i = 1;; ++i) {
            std::snprintf(st.addRoomId, sizeof(st.addRoomId), "room%d", i);
            if (!hasRoom(*graph, st.addRoomId)) break;
        }
        const ImVec2 center =
            toWorld(gst.cam, ImVec2(gst.viewW * 0.5f, gst.viewH * 0.5f));
        st.addRoomX = center.x;
        st.addRoomY = center.y;
    }
    ImGui::SameLine();
    if (graph->rooms.size() < 2) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Add passage")) {
        st.addPassageOpen = true;
        st.addPassageErr.clear();
        st.addPassageA = 0;
        st.addPassageB = graph->rooms.size() > 1 ? 1 : 0;
    }
    if (graph->rooms.size() < 2) ImGui::EndDisabled();
    ImGui::SameLine();

    // The Delete target: the selected passage wins over the selected room
    // (the canvas keeps the two mutually exclusive; the label says which).
    std::string delLabel = "Delete";
    if (!st.selEdgeA.empty() && hasPassage(*graph, st.selEdgeA, st.selEdgeB)) {
        delLabel = "Delete passage " + st.selEdgeA + "-" + st.selEdgeB;
    } else if (selection.kind == Selection::Kind::Room && hasRoom(*graph, selection.id)) {
        delLabel = "Delete room " + selection.id;
    }
    const bool delEnabled = delLabel != "Delete";
    if (!delEnabled) ImGui::BeginDisabled();
    if (ImGui::SmallButton(delLabel.c_str())) wantDelete = true;
    if (!delEnabled) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete the selected room (with its passages) or passage (Del)");
    ImGui::SameLine();

    if (ImGui::SmallButton("Fit")) wantFit = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu rooms, %zu passages", model.nodes.size(), model.edges.size());

    if (!level.generated) {
        ImGui::TextDisabled(
            "auto-arranged — drag nodes to pin positions (saved with the project); Generate "
            "builds the real layout");
    } else if (!graph->editor_node_pos.empty()) {
        ImGui::TextDisabled("pinned nodes keep their positions here; the Topo tab shows the "
                            "generated layout");
    }

    // --- canvas --------------------------------------------------------------

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(fullW, 64.0f);
    avail.y = std::max(avail.y, 64.0f);
    gst.viewW = avail.x;
    gst.viewH = avail.y;

    ImGui::InvisibleButton("##layout_graph_canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();

    if (model.nodes.empty()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(rmin, rmax, true);
        dl->AddRectFilled(rmin, rmax, kGraphBg);
        dl->PopClipRect();
    } else {
        if (!gst.fitted || wantFit) {
            fitGraphCam(gst.cam, model, avail.x, avail.y);
            gst.fitted = true;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouseLocal(io.MousePos.x - rmin.x, io.MousePos.y - rmin.y);
        const ImVec2 mouseWorld = toWorld(gst.cam, mouseLocal);
        const bool hovered = ImGui::IsItemHovered();

        if (hovered && io.MouseWheel != 0.0f)
            zoomToCursor(gst.cam, mouseLocal, io.MouseWheel > 0 ? 1.2f : 1.0f / 1.2f);

        // Hover: the topmost node box containing the cursor (screen-space
        // hit; the boxes are fixed-size in points). While dragging, the
        // dragged node keeps the hover.
        int hoverNode = -1;
        if (hovered) {
            for (int i = static_cast<int>(model.nodes.size()) - 1; i >= 0; --i) {
                const auto& n = model.nodes[i];
                if (!n.hasLayout) continue;
                const ImVec2 c = toScreen(gst.cam, rmin, n.cx, n.cz);
                if (std::fabs(mouseLocal.x - (c.x - rmin.x)) <= kGraphNodeW * 0.5f &&
                    std::fabs(mouseLocal.y - (c.y - rmin.y)) <= kGraphNodeH * 0.5f) {
                    hoverNode = i;
                    break;
                }
            }
        }
        gst.hoverNode = hoverNode;

        // LMB press on a node: select it right away and start a potential
        // drag (the grab offset keeps the node from jumping).
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverNode >= 0) {
            const auto& n = model.nodes[hoverNode];
            st.dragId = n.id;
            st.dragGrabX = mouseWorld.x - n.cx;
            st.dragGrabY = mouseWorld.y - n.cz;
            if (selection.kind != Selection::Kind::Room || selection.id != n.id) {
                selection = Selection{Selection::Kind::Room, n.id};
                res.selectionChanged = true;
            }
            if (!st.selEdgeA.empty()) {
                st.selEdgeA.clear();
                st.selEdgeB.clear();
            }
        }
        // Dragging writes the pinned position (absolute, drift-free); the
        // model picks it up on the next frame's rebuild.
        if (!st.dragId.empty()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
                    auto& pos = graph->editor_node_pos[st.dragId];
                    const double nx = mouseWorld.x - st.dragGrabX;
                    const double ny = mouseWorld.y - st.dragGrabY;
                    if (nx != pos.first || ny != pos.second) {
                        pos = {nx, ny};
                        res.markDirty = true;
                    }
                }
            } else {
                st.dragId.clear();
            }
        }

        // Pan on any-button drag from empty space (node drags belong to the node).
        if (ImGui::IsItemActive() && st.dragId.empty() &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
            gst.cam.offsetX += io.MouseDelta.x;
            gst.cam.offsetY += io.MouseDelta.y;
        }

        // Edge hover: the door label plates (screen-space rects).
        int hoverEdge = -1;
        if (hovered && hoverNode < 0 && st.dragId.empty()) {
            for (size_t i = 0; i < model.edges.size(); ++i) {
                const auto& e = model.edges[i];
                if (!e.labelOk || e.door.empty()) continue;
                ImVec2 a, b;
                edgeLabelRect(e, gst.cam, rmin, a, b);
                if (io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y &&
                    io.MousePos.y <= b.y) {
                    hoverEdge = static_cast<int>(i);
                    break;
                }
            }
        }
        st.hoverEdge = hoverEdge;

        // Click = LMB release within a small drag threshold.
        bool clicked = false;
        if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const ImVec2 press = io.MouseClickedPos[ImGuiMouseButton_Left];
            const float dx = io.MousePos.x - press.x, dy = io.MousePos.y - press.y;
            if (dx * dx + dy * dy < 16.0f) clicked = true;
        }
        if (clicked) {
            if (hoverNode >= 0) {
                selection = Selection{Selection::Kind::Room, model.nodes[hoverNode].id};
                st.selEdgeA.clear();
                st.selEdgeB.clear();
                res.selectionChanged = true;
            } else if (hoverEdge >= 0) {
                st.selEdgeA = model.edges[hoverEdge].a;
                st.selEdgeB = model.edges[hoverEdge].b;
                if (selection.kind != Selection::Kind::None) {
                    selection = Selection{};
                    res.selectionChanged = true;
                }
            } else if (selection.kind != Selection::Kind::None || !st.selEdgeA.empty()) {
                selection = Selection{};
                st.selEdgeA.clear();
                st.selEdgeB.clear();
                res.selectionChanged = true;
            }
        }
        if (hoverNode >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            ImGui::IsItemHovered()) {
            selection = Selection{Selection::Kind::Room, model.nodes[hoverNode].id};
            res.selectionChanged = true;
            res.focus = true;
        }

        // Delete key (the toolbar button sets wantDelete too).
        if ((hovered || !st.dragId.empty()) && !io.WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            wantDelete = true;

        // --- drawing ---------------------------------------------------------

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(rmin, rmax, true);
        dl->AddRectFilled(rmin, rmax, kGraphBg);

        // Wires under the boxes: bezier node-center to node-center, dtype color.
        {
            std::unordered_map<std::string, int> widx;
            widx.reserve(model.nodes.size());
            for (size_t i = 0; i < model.nodes.size(); ++i)
                widx.emplace(model.nodes[i].id, static_cast<int>(i));
            for (size_t i = 0; i < model.edges.size(); ++i) {
                const auto& e = model.edges[i];
                const auto ia = widx.find(e.a);
                const auto ib = widx.find(e.b);
                if (ia == widx.end() || ib == widx.end()) continue;
                const auto& na = model.nodes[ia->second];
                const auto& nb = model.nodes[ib->second];
                if (!na.hasLayout || !nb.hasLayout) continue;
                const ImVec2 p0 = toScreen(gst.cam, rmin, na.cx, na.cz);
                const ImVec2 p1 = toScreen(gst.cam, rmin, nb.cx, nb.cz);
                const float bend = std::max(30.0f, std::fabs(p1.x - p0.x) * 0.4f);
                const bool sel = st.selEdgeA == e.a && st.selEdgeB == e.b;
                const bool hov = st.hoverEdge == static_cast<int>(i);
                const ImU32 base = e.door == "open" ? kDoorOpen : kDoorOther;
                const ImU32 c = sel ? kSelection : withAlpha(base, hov ? 255 : 200);
                dl->AddBezierCubic(p0, ImVec2(p0.x + bend, p0.y), ImVec2(p1.x - bend, p1.y), p1, c,
                                   sel ? 3.0f : 1.6f);
            }
        }

        // Door-type labels at the edge midpoints on a dark plate.
        for (size_t i = 0; i < model.edges.size(); ++i) {
            const auto& e = model.edges[i];
            if (!e.labelOk || e.door.empty()) continue;
            ImVec2 a, b;
            edgeLabelRect(e, gst.cam, rmin, a, b);
            const bool sel = st.selEdgeA == e.a && st.selEdgeB == e.b;
            dl->AddRectFilled(a, b, IM_COL32(16, 18, 22, 200));
            if (sel || st.hoverEdge == static_cast<int>(i))
                dl->AddRect(a, b, sel ? kSelection : IM_COL32(200, 210, 235, 255), 0.0f, 0, 1.2f);
            dl->AddText(ImVec2(a.x + 3.0f, a.y + 2.0f),
                        e.door == "open" ? kDoorOpen : kDoorOther, e.door.c_str());
        }

        // Node boxes: fixed 88x36 points, centered at the centroids, id + role.
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            const auto& n = model.nodes[i];
            if (!n.hasLayout) continue;
            const ImVec2 c = toScreen(gst.cam, rmin, n.cx, n.cz);
            const ImVec2 a(c.x - kGraphNodeW * 0.5f, c.y - kGraphNodeH * 0.5f);
            const ImVec2 b(c.x + kGraphNodeW * 0.5f, c.y + kGraphNodeH * 0.5f);
            const ImU32 role = roleColor(model, n.role);
            dl->AddRectFilled(a, b, withAlpha(role, 46), 4.0f);
            ImU32 border = withAlpha(role, 220);
            float thickness = 1.2f;
            if (selection.kind == Selection::Kind::Room && selection.id == n.id) {
                border = kSelection;
                thickness = 2.2f;
            } else if (gst.hoverNode == static_cast<int>(i)) {
                border = IM_COL32(200, 210, 235, 255);
            }
            dl->AddRect(a, b, border, 4.0f, 0, thickness);
            dl->PushClipRect(a, b, true);
            dl->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), IM_COL32(232, 236, 244, 255), n.id.c_str());
            dl->AddText(ImVec2(a.x + 6.0f, a.y + 19.0f), withAlpha(role, 220), n.role.c_str());
            dl->PopClipRect();
        }

        dl->PopClipRect();
    }

    // --- edits ---------------------------------------------------------------

    if (wantDelete) {
        if (!st.selEdgeA.empty() && hasPassage(*graph, st.selEdgeA, st.selEdgeB)) {
            deletePassage(*graph, st.selEdgeA, st.selEdgeB);
            st.selEdgeA.clear();
            st.selEdgeB.clear();
            res.markDirty = true;
            res.graphChanged = true;
        } else if (selection.kind == Selection::Kind::Room && hasRoom(*graph, selection.id)) {
            if (st.dragId == selection.id) st.dragId.clear();
            deleteRoom(*graph, selection.id);
            selection = Selection{};
            res.selectionChanged = true;
            res.markDirty = true;
            res.graphChanged = true;
        }
    }

    // --- modals ----------------------------------------------------------------

    if (st.addRoomOpen) {
        ImGui::OpenPopup("Add room");
        st.addRoomOpen = false;
    }
    if (ImGui::BeginPopupModal("Add room", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("id", st.addRoomId, sizeof(st.addRoomId));
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("role", kRoles[st.addRoomRole])) {
            for (int i = 0; i < 5; ++i)
                if (ImGui::Selectable(kRoles[i], st.addRoomRole == i)) st.addRoomRole = i;
            ImGui::EndCombo();
        }
        if (!st.addRoomErr.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "%s", st.addRoomErr.c_str());
        if (ImGui::Button("Add", ImVec2(100.0f, 0.0f))) {
            const std::string id = st.addRoomId;
            if (id.find_first_not_of(" \t") == std::string::npos) {
                st.addRoomErr = "empty id";
            } else if (hasRoom(*graph, id)) {
                st.addRoomErr = "room \"" + id + "\" already exists";
            } else {
                dg::GraphRoom room;
                room.id = id;
                room.role = kRoles[st.addRoomRole];
                graph->rooms.push_back(std::move(room));
                graph->editor_node_pos[id] = {st.addRoomX, st.addRoomY};
                selection = Selection{Selection::Kind::Room, id};
                res.selectionChanged = true;
                res.markDirty = true;
                res.graphChanged = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (st.addPassageOpen) {
        ImGui::OpenPopup("Add passage");
        st.addPassageOpen = false;
    }
    if (ImGui::BeginPopupModal("Add passage", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::vector<std::string> ids = roomIds(*graph);
        const auto combo = [&](const char* label, int& cur, int exclude) {
            bool changed = false;
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo(label, ids[cur].c_str())) {
                for (size_t i = 0; i < ids.size(); ++i) {
                    if (static_cast<int>(i) == exclude) continue;
                    if (ImGui::Selectable(ids[i].c_str(), cur == static_cast<int>(i))) {
                        cur = static_cast<int>(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        };
        ImGui::Text("from");
        ImGui::SameLine();
        combo("##passage_a", st.addPassageA, -1);
        ImGui::SameLine();
        ImGui::Text("to");
        ImGui::SameLine();
        combo("##passage_b", st.addPassageB, -1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::BeginCombo("##passage_door", kDoors[st.addPassageDoor])) {
            for (int i = 0; i < 2; ++i)
                if (ImGui::Selectable(kDoors[i], st.addPassageDoor == i)) st.addPassageDoor = i;
            ImGui::EndCombo();
        }
        if (!st.addPassageErr.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "%s", st.addPassageErr.c_str());
        if (ImGui::Button("Add", ImVec2(100.0f, 0.0f))) {
            const std::string& a = ids[st.addPassageA];
            const std::string& b = ids[st.addPassageB];
            if (a == b) {
                st.addPassageErr = "a room cannot connect to itself";
            } else if (hasPassage(*graph, a, b)) {
                st.addPassageErr = "passage " + std::min(a, b) + " - " + std::max(a, b) +
                                   " already exists";
            } else {
                dg::Passage p;
                p.a = a;
                p.b = b;
                p.door = kDoors[st.addPassageDoor];
                graph->passages.push_back(std::move(p));
                res.markDirty = true;
                res.graphChanged = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    return res;
}
