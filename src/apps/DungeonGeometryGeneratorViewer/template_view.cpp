#include "pch.h"

#include "template_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "dungeon_topology_generator/generator/grid2d/manual_door_mode_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/simple_door_mode_grid2d.hpp"

#include "level.h"

namespace {

namespace dtgeom = dungeon_topology_generator::geometry;
namespace dtgen = dungeon_topology_generator::generator::grid2d;

constexpr ImU32 kOutline = IM_COL32(235, 235, 240, 255);
constexpr ImU32 kDoorValid = IM_COL32(90, 220, 90, 70);    // simple mode: every valid position
constexpr ImU32 kDoorManual = IM_COL32(240, 160, 60, 255); // manual mode: declared segments
constexpr ImU32 kGridMinor = IM_COL32(255, 255, 255, 12);
constexpr ImU32 kGridMajor = IM_COL32(255, 255, 255, 24);

// The fixed v1 sets (layout.cpp is_concrete_role, project.cpp style_code).
constexpr const char* kConcreteRoles[] = {"hall", "corridor", "crypt", "entry", "stairs"};
constexpr const char* kNonCorridorRoles[] = {"hall", "crypt", "entry", "stairs"};
constexpr const char* kStyles[] = {"stone", "brick", "plain", "mortar", "sandstone", "none"};
constexpr const char* kTransformNames[] = {"identity", "rot90",  "rot180",  "rot270",
                                           "mirror_x", "mirror_y", "diag13", "diag24"};

std::string joinNames(const std::vector<std::string>& names) {
    std::string s;
    for (const std::string& n : names) {
        if (!s.empty()) s += ", ";
        s += n;
    }
    return s;
}

const char* transformName(dtgeom::TransformationGrid2D t) {
    using T = dtgeom::TransformationGrid2D;
    switch (t) {
        case T::Identity: return "identity";
        case T::Rotate90: return "rot90";
        case T::Rotate180: return "rot180";
        case T::Rotate270: return "rot270";
        case T::MirrorX: return "mirror_x";
        case T::MirrorY: return "mirror_y";
        case T::Diagonal13: return "diag13";
        case T::Diagonal24: return "diag24";
    }
    return "?";
}

const dungeon_geometry_generator::layout::CatalogEntry* findEntry(const Level& level, const std::string& name) {
    for (const dungeon_geometry_generator::layout::CatalogEntry& e : level.catalog.entries)
        if (e.name == name) return &e;
    return nullptr;
}

dungeon_geometry_generator::TemplateDecl* findDecl(Level& level, const std::string& name) {
    if (!level.project.layout) return nullptr;
    for (dungeon_geometry_generator::TemplateDecl& t : level.project.layout->templates)
        if (t.name == name) return &t;
    return nullptr;
}

// --- field editors (write into level.project; return true on change) --------

// Role checkboxes; unchecking the last remaining role is rejected.
bool editRoles(std::vector<std::string>& roles, const char* const* allowed, int allowedCount) {
    bool changed = false;
    for (int i = 0; i < allowedCount; ++i) {
        const std::string role = allowed[i];
        const bool has = std::find(roles.begin(), roles.end(), role) != roles.end();
        bool on = has;
        if (on && roles.size() == 1) ImGui::BeginDisabled();
        if (ImGui::Checkbox(role.c_str(), &on)) {
            if (on)
                roles.push_back(role);
            else
                roles.erase(std::find(roles.begin(), roles.end(), role));
            changed = true;
        }
        if (on && roles.size() == 1) ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    return changed;
}

// Transform checkboxes over the effective list; the first edit turns an
// unset (default-rotations) list into an explicit one (port semantics:
// explicit empty = identity only).
bool editTransforms(dungeon_geometry_generator::TemplateDecl& decl) {
    bool changed = false;
    const std::vector<std::string> cur = dungeon_geometry_generator::effective_transforms(decl);
    for (const char* name : kTransformNames) {
        bool on = std::find(cur.begin(), cur.end(), name) != cur.end();
        ImGui::PushID(name);
        if (ImGui::Checkbox(name, &on)) {
            if (!decl.transforms_set) {
                decl.transforms = cur;
                decl.transforms_set = true;
            }
            if (on)
                decl.transforms.push_back(name);
            else
                decl.transforms.erase(std::find(decl.transforms.begin(), decl.transforms.end(), name));
            changed = true;
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (decl.transforms_set && decl.transforms.empty())
        ImGui::TextDisabled("(empty list = identity only)");
    return changed;
}

// Simple-door overrides (nullopt = inherit the project rule). Manual doors
// stay read-only until the contour editor (iteration C).
bool editSimpleDoors(dungeon_geometry_generator::TemplateDoors& doors, int projLength,
                     int projCorner) {
    bool changed = false;
    bool ovLen = doors.length.has_value();
    if (ImGui::Checkbox("override length", &ovLen)) {
        doors.length = ovLen ? std::optional<int>(projLength) : std::nullopt;
        changed = true;
    }
    if (doors.length) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        int v = *doors.length;
        if (ImGui::InputInt("cells##door_len", &v)) {
            *doors.length = std::max(v, 1);
            changed = true;
        }
    }
    bool ovCorner = doors.corner_distance.has_value();
    if (ImGui::Checkbox("override corner distance", &ovCorner)) {
        doors.corner_distance = ovCorner ? std::optional<int>(projCorner) : std::nullopt;
        changed = true;
    }
    if (doors.corner_distance) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        int v = *doors.corner_distance;
        if (ImGui::InputInt("cells##door_corner", &v)) {
            *doors.corner_distance = std::max(v, 0);
            changed = true;
        }
    }
    return changed;
}

bool editOverrideDouble(const char* label, std::optional<double>& v, double def, double lo,
                        double hi) {
    bool changed = false;
    bool set = v.has_value();
    if (ImGui::Checkbox(label, &set)) {
        v = set ? std::optional<double>(def) : std::nullopt;
        changed = true;
    }
    if (v) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        float f = static_cast<float>(*v);
        char id[32];
        std::snprintf(id, sizeof(id), "##%s", label);
        if (ImGui::DragFloat(id, &f, 0.05f, static_cast<float>(lo), static_cast<float>(hi), "%.2f")) {
            *v = static_cast<double>(f);
            changed = true;
        }
    }
    return changed;
}

bool editOverrideStyle(const char* label, std::optional<std::string>& v, const char* def) {
    bool changed = false;
    bool set = v.has_value();
    if (ImGui::Checkbox(label, &set)) {
        v = set ? std::optional<std::string>(def) : std::nullopt;
        changed = true;
    }
    if (v) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        char id[32];
        std::snprintf(id, sizeof(id), "##%s", label);
        if (ImGui::BeginCombo(id, v->c_str())) {
            for (const char* s : kStyles)
                if (ImGui::Selectable(s, *v == s)) {
                    *v = s;
                    changed = true;
                }
            ImGui::EndCombo();
        }
    }
    return changed;
}

// Template/room level of the 4.2 hierarchy: every field optional.
bool editFillOverride(dungeon_geometry_generator::FillOverride& o) {
    bool changed = false;
    ImGui::TextDisabled("fill overrides:");
    ImGui::Indent();
    ImGui::PushID("fill_override");
    changed |= editOverrideDouble("h", o.h, 3.0, 0.5, 20.0);
    changed |= editOverrideDouble("wall_t", o.wall_t, 0.6, 0.05, 4.0);
    changed |= editOverrideStyle("style", o.style, "stone");
    changed |= editOverrideStyle("floor", o.floor, "stone");
    changed |= editOverrideStyle("ceil", o.ceil, "plain");
    ImGui::PopID();
    ImGui::Unindent();
    return changed;
}

bool editRange(const char* label, dungeon_geometry_generator::IntRange& r) {
    int v[2] = {r.lo, r.hi};
    ImGui::SetNextItemWidth(180.0f);
    if (!ImGui::InputInt2(label, v)) return false;
    r.lo = std::max(v[0], 1);
    r.hi = std::max(v[1], r.lo);
    return true;
}

bool editInt(const char* label, int& v, int lo) {
    ImGui::SetNextItemWidth(110.0f);
    if (!ImGui::InputInt(label, &v)) return false;
    v = std::max(v, lo);
    return true;
}

// --- contour canvas ---------------------------------------------------------

ImVec2 toScreen(const TemplateViewState& st, const ImVec2& origin, double x, double y) {
    return ImVec2(origin.x + st.offsetX + static_cast<float>(x * st.zoom),
                  origin.y + st.offsetY + static_cast<float>(y * st.zoom));
}

ImVec2 toWorld(const TemplateViewState& st, const ImVec2& local) {
    return ImVec2((local.x - st.offsetX) / st.zoom, (local.y - st.offsetY) / st.zoom);
}

void fitContour(TemplateViewState& st, float w, float h,
                const std::vector<dtgeom::Vector2Int>& pts) {
    int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (const dtgeom::Vector2Int& p : pts) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    // Two cells of padding around the contour.
    const double spanX = std::max(maxx - minx, 1) + 4.0;
    const double spanY = std::max(maxy - miny, 1) + 4.0;
    st.zoom = std::clamp(static_cast<float>(std::min(w / spanX, h / spanY)), 2.0f, 96.0f);
    st.offsetX = static_cast<float>(w * 0.5 - (minx + maxx) * 0.5 * st.zoom);
    st.offsetY = static_cast<float>(h * 0.5 - (miny + maxy) * 0.5 * st.zoom);
}

// Contour canvas: pan/zoom as the Topo panes, the outline, and the door
// segments — simple mode shows every valid door position dimmed, manual mode
// the declared segments bright. Read-only until the contour editor (C).
void drawContourCanvas(const dungeon_geometry_generator::layout::CatalogEntry& entry,
                       TemplateViewState& st) {
    const dtgeom::PolygonGrid2D& outline = entry.dungeon_topology_generator.outline();
    const std::vector<dtgeom::Vector2Int>& pts = outline.points();
    if (pts.empty()) {
        ImGui::TextDisabled("(empty outline)");
        return;
    }
    const bool manual =
        dynamic_cast<const dtgen::ManualDoorModeGrid2D*>(&entry.dungeon_topology_generator.doors()) !=
        nullptr;
    const std::vector<dtgen::DoorLineGrid2D> doors =
        entry.dungeon_topology_generator.doors().get_doors(outline);

    bool wantFit = false;
    if (ImGui::SmallButton("Fit")) wantFit = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu contour pts, %s doors: %zu %s", pts.size(), manual ? "manual" : "simple",
                        doors.size(), manual ? "declared" : "valid positions");

    const float fullW = ImGui::GetContentRegionAvail().x;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(fullW, 64.0f);
    avail.y = std::max(avail.y, 64.0f);

    ImGui::InvisibleButton("##tmpl_canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();

    if (!st.fitted || wantFit) {
        fitContour(st, avail.x, avail.y, pts);
        st.fitted = true;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouseLocal(io.MousePos.x - rmin.x, io.MousePos.y - rmin.y);
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        const ImVec2 w = toWorld(st, mouseLocal);
        st.zoom = std::clamp(st.zoom * (io.MouseWheel > 0 ? 1.2f : 1.0f / 1.2f), 2.0f, 256.0f);
        st.offsetX = mouseLocal.x - w.x * st.zoom;
        st.offsetY = mouseLocal.y - w.y * st.zoom;
    }
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
         ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
         ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        st.offsetX += io.MouseDelta.x;
        st.offsetY += io.MouseDelta.y;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(rmin, rmax, true);

    // Cell grid over the visible rect.
    {
        const ImVec2 w0 = toWorld(st, ImVec2(0.0f, 0.0f));
        const ImVec2 w1 = toWorld(st, avail);
        const int x0 = static_cast<int>(std::floor(w0.x)), x1 = static_cast<int>(std::ceil(w1.x));
        const int y0 = static_cast<int>(std::floor(w0.y)), y1 = static_cast<int>(std::ceil(w1.y));
        if (st.zoom >= 6.0f && x1 - x0 < 2000 && y1 - y0 < 2000) {
            for (int x = x0; x <= x1; ++x)
                dl->AddLine(toScreen(st, rmin, x, y0), toScreen(st, rmin, x, y1),
                            x % 5 == 0 ? kGridMajor : kGridMinor, 1.0f);
            for (int y = y0; y <= y1; ++y)
                dl->AddLine(toScreen(st, rmin, x0, y), toScreen(st, rmin, x1, y),
                            y % 5 == 0 ? kGridMajor : kGridMinor, 1.0f);
        }
    }

    // Door segments under the outline.
    const ImU32 doorCol = manual ? kDoorManual : kDoorValid;
    for (const dtgen::DoorLineGrid2D& d : doors)
        dl->AddLine(toScreen(st, rmin, d.line.from.x, d.line.from.y),
                    toScreen(st, rmin, d.line.to.x, d.line.to.y), doorCol, 2.5f);

    std::vector<ImVec2> screen;
    screen.reserve(pts.size());
    for (const dtgeom::Vector2Int& p : pts) screen.push_back(toScreen(st, rmin, p.x, p.y));
    dl->AddPolyline(screen.data(), static_cast<int>(screen.size()), kOutline, ImDrawFlags_Closed,
                    2.0f);

    dl->PopClipRect();
}

// --- shared bits ------------------------------------------------------------

// The per-card "Save & Re-layout" button (top-right of the header line).
void drawSaveButton(bool projectDirty, TemplateViewActions& actions) {
    if (!projectDirty) ImGui::BeginDisabled();
    if (ImGui::Button("Save & Re-layout")) actions.saveApply = true;
    if (!projectDirty) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Write project.json, then re-layout + refill (Ctrl+S)");
    if (projectDirty) {
        ImGui::SameLine();
        ImGui::TextDisabled("(modified)");
    }
}

void drawFillOverrideView(const dungeon_geometry_generator::FillOverride& fill) {
    if (fill.empty()) {
        ImGui::TextDisabled("fill overrides: none");
        return;
    }
    ImGui::TextDisabled("fill overrides:");
    if (fill.h) ImGui::BulletText("h = %.2f", *fill.h);
    if (fill.wall_t) ImGui::BulletText("wall_t = %.2f", *fill.wall_t);
    if (fill.style) ImGui::BulletText("style = %s", fill.style->c_str());
    if (fill.floor) ImGui::BulletText("floor = %s", fill.floor->c_str());
    if (fill.ceil) ImGui::BulletText("ceil = %s", fill.ceil->c_str());
}

// Usage: the graph rooms whose role matches (can adopt the template) and the
// placed rooms of the current layout that actually use it (click selects).
void drawUsage(const Level& level, const dungeon_geometry_generator::layout::CatalogEntry& entry,
               TemplateViewActions& actions) {
    const dungeon_geometry_generator::LayoutParams& layout = *level.project.layout;
    ImGui::TextDisabled("rooms with a matching role:");
    ImGui::Indent();
    bool any = false;
    for (const dungeon_geometry_generator::GraphRoom& room : layout.rooms) {
        const bool match = std::find(entry.roles.begin(), entry.roles.end(), room.role) !=
                           entry.roles.end();
        if (!match) continue;
        any = true;
        char label[256];
        std::snprintf(label, sizeof(label), "%s  [%s]##use", room.id.c_str(), room.role.c_str());
        if (ImGui::Selectable(label)) {
            actions.selectRoom = true;
            actions.roomId = room.id;
        }
    }
    if (!any) ImGui::TextDisabled("(none)");
    ImGui::Unindent();

    ImGui::TextDisabled("placed with this template in the current layout:");
    ImGui::Indent();
    any = false;
    for (const dungeon_geometry_generator::LayoutRoomData& room : level.layoutData.rooms) {
        if (room.tmpl != entry.name) continue;
        any = true;
        char label[256];
        std::snprintf(label, sizeof(label), "%s##placed", room.id.c_str());
        if (ImGui::Selectable(label)) {
            actions.selectRoom = true;
            actions.roomId = room.id;
        }
    }
    if (!any) ImGui::TextDisabled("(none)");
    ImGui::Unindent();
}

// --- cards ------------------------------------------------------------------

void drawTemplateCard(Level& level, const std::string& name, TemplateViewState& st,
                      bool projectDirty, TemplateViewActions& actions) {
    const dungeon_geometry_generator::layout::CatalogEntry* entry = findEntry(level, name);
    dungeon_geometry_generator::TemplateDecl* decl = findDecl(level, name);
    if (!entry) {
        ImGui::TextDisabled("template '%s' is no longer in the catalog — close this tab",
                            name.c_str());
        return;
    }
    const bool parametric = entry->parametric;

    ImGui::Text("%s", entry->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", parametric ? "[parametric]" : "[explicit]");
    ImGui::SameLine();
    drawSaveButton(projectDirty, actions);

    if (parametric || !decl) {
        // A generated instance: derived — edit the generator instead.
        ImGui::TextDisabled("roles: %s", joinNames(entry->roles).c_str());
        std::string transforms;
        for (const dtgeom::TransformationGrid2D t :
             entry->dungeon_topology_generator.allowed_transformations()) {
            if (!transforms.empty()) transforms += ", ";
            transforms += transformName(t);
        }
        ImGui::Text("transforms (%zu): %s",
                    entry->dungeon_topology_generator.allowed_transformations().size(),
                    transforms.c_str());
        if (const auto* simple = dynamic_cast<const dtgen::SimpleDoorModeGrid2D*>(
                &entry->dungeon_topology_generator.doors())) {
            ImGui::Text("doors: simple, length %d, corner distance %d", simple->door_length(),
                        simple->corner_distance());
        }
        drawFillOverrideView(entry->fill);
        ImGui::Separator();
        drawContourCanvas(*entry, st);
        ImGui::Separator();
        drawUsage(level, *entry, actions);
        ImGui::Separator();
        const bool rects = name.rfind("rect_", 0) == 0;
        if (ImGui::Button(rects ? "Open the rooms_rect generator" : "Open the corridors generator")) {
            actions.openTab = true;
            actions.tab = {rects ? ProjectTreeSelection::Kind::GeneratorRects
                                 : ProjectTreeSelection::Kind::GeneratorCorridors,
                           {}};
        }
        return;
    }

    // An explicit template: editable declaration.
    dungeon_geometry_generator::LayoutParams& layout = *level.project.layout;

    ImGui::TextDisabled("roles:");
    ImGui::SameLine();
    if (editRoles(decl->roles, kConcreteRoles, 5)) actions.markDirty = true;

    ImGui::TextDisabled("transforms:");
    ImGui::SameLine();
    if (editTransforms(*decl)) actions.markDirty = true;

    if (decl->doors.manual) {
        ImGui::Text("doors: manual, %zu segments (read-only until the contour editor)",
                    decl->doors.segments.size());
    } else {
        ImGui::TextDisabled("doors: simple (project rule: length %d, corner %d)",
                            layout.door_length, layout.door_corner_distance);
        if (editSimpleDoors(decl->doors, layout.door_length, layout.door_corner_distance))
            actions.markDirty = true;
    }

    if (editFillOverride(decl->fill)) actions.markDirty = true;
    ImGui::Separator();
    ImGui::TextDisabled("contour (read-only until iteration C):");
    drawContourCanvas(*entry, st);
    ImGui::Separator();
    drawUsage(level, *entry, actions);
}

// A generated-template thumbnail: the contour fitted into a fixed tile.
bool drawThumbnail(const dungeon_geometry_generator::layout::CatalogEntry& entry) {
    ImGui::BeginGroup();
    const float thumb = 92.0f;
    ImGui::InvisibleButton("##thumb", ImVec2(thumb, thumb));
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();

    const std::vector<dtgeom::Vector2Int>& pts = entry.dungeon_topology_generator.outline().points();
    if (!pts.empty()) {
        int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
        for (const dtgeom::Vector2Int& p : pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        const float pad = 8.0f;
        const float scale =
            std::min((thumb - 2.0f * pad) / std::max(maxx - minx, 1),
                     (thumb - 2.0f * pad) / std::max(maxy - miny, 1));
        const float ox = rmin.x + thumb * 0.5f - (minx + maxx) * 0.5f * scale;
        const float oy = rmin.y + thumb * 0.5f - (miny + maxy) * 0.5f * scale;
        std::vector<ImVec2> screen;
        screen.reserve(pts.size());
        for (const dtgeom::Vector2Int& p : pts)
            screen.emplace_back(ox + p.x * scale, oy + p.y * scale);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(rmin, rmax,
                    ImGui::IsItemHovered() ? IM_COL32(120, 160, 220, 160) : IM_COL32(90, 95, 105, 160),
                    4.0f, 0, 1.0f);
        dl->AddPolyline(screen.data(), static_cast<int>(screen.size()), kOutline,
                        ImDrawFlags_Closed, 1.5f);
    }
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::EndGroup();
    return clicked;
}

// Shared layout-tier params edited on both generator cards.
void editSharedLayoutParams(dungeon_geometry_generator::LayoutParams& layout,
                            TemplateViewActions& actions) {
    ImGui::TextDisabled("shared layout params:");
    ImGui::Indent();
    if (editInt("door_length", layout.door_length, 1) |
        editInt("door_corner_distance", layout.door_corner_distance, 0) |
        editInt("catalog_budget", layout.catalog_budget, 1))
        actions.markDirty = true;
    ImGui::Unindent();
}

void drawGeneratorCard(Level& level, bool rects, bool projectDirty, TemplateViewActions& actions) {
    dungeon_geometry_generator::LayoutParams& layout = *level.project.layout;
    ImGui::Text("%s", rects ? "rooms_rect" : "corridors");
    ImGui::SameLine();
    ImGui::TextDisabled("[parametric generator]");
    ImGui::SameLine();
    drawSaveButton(projectDirty, actions);

    if (rects) {
        if (editRange("w range", layout.rect_w) | editRange("h range", layout.rect_h))
            actions.markDirty = true;
        ImGui::TextDisabled("roles:");
        ImGui::SameLine();
        bool custom = layout.rect_roles_set;
        if (ImGui::Checkbox("custom", &custom)) {
            layout.rect_roles_set = custom;
            if (custom && layout.rect_roles.empty()) layout.rect_roles.push_back("hall");
            actions.markDirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("off = all non-corridor graph roles");
        if (layout.rect_roles_set) {
            ImGui::SameLine();
            if (editRoles(layout.rect_roles, kNonCorridorRoles, 4)) actions.markDirty = true;
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("(all non-corridor graph roles)");
        }
    } else {
        if (editInt("width", layout.corridor_width, 1) |
            editRange("length range", layout.corridor_length))
            actions.markDirty = true;
        ImGui::TextDisabled("roles: corridor");
    }
    editSharedLayoutParams(layout, actions);
    ImGui::Separator();

    const char* prefix = rects ? "rect_" : "corridor_";
    const size_t prefixLen = std::string(prefix).size();
    int count = 0;
    for (const dungeon_geometry_generator::layout::CatalogEntry& e : level.catalog.entries) {
        if (!e.parametric || e.name.compare(0, prefixLen, prefix) != 0) continue;
        ++count;
        ImGui::PushID(e.name.c_str());
        if (drawThumbnail(e)) {
            actions.openTab = true;
            actions.tab = {ProjectTreeSelection::Kind::Template, e.name};
        }
        ImGui::PopID();
        const float nextX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + 92.0f;
        if (nextX < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine();
    }
    if (count == 0)
        ImGui::TextDisabled("(no templates generated — the graph has no rooms for these roles)");
}

}  // namespace

std::string templateTabLabel(const ProjectTreeSelection& sel) {
    switch (sel.kind) {
        case ProjectTreeSelection::Kind::Layout: return "Layout";
        case ProjectTreeSelection::Kind::GeneratorRects: return "gen: rooms_rect";
        case ProjectTreeSelection::Kind::GeneratorCorridors: return "gen: corridors";
        case ProjectTreeSelection::Kind::Template: return sel.name;
        default: return "?";
    }
}

TemplateViewActions drawTemplateView(Level& level, const ProjectTreeSelection& sel,
                                     TemplateViewState& st, bool projectDirty) {
    TemplateViewActions actions;
    if (!level.project.layout) {
        ImGui::TextDisabled("the project has no layout tier");
        return actions;
    }
    switch (sel.kind) {
        case ProjectTreeSelection::Kind::Template:
            drawTemplateCard(level, sel.name, st, projectDirty, actions);
            break;
        case ProjectTreeSelection::Kind::GeneratorRects:
            drawGeneratorCard(level, true, projectDirty, actions);
            break;
        case ProjectTreeSelection::Kind::GeneratorCorridors:
            drawGeneratorCard(level, false, projectDirty, actions);
            break;
        default:
            ImGui::TextDisabled("(unknown tab)");
            break;
    }
    return actions;
}
