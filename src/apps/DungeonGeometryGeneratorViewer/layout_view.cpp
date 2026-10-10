#include "pch.h"

#include "layout_view.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <unordered_map>
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

}  // namespace

void buildLayoutGraphModel(const Level& level, dg::TopoModel& out) {
    out = dg::TopoModel{};
    if (!level.loaded || !level.project.layout) return;
    out = dg::build_topo(*level.project.layout,
                         level.generated ? level.layoutData : dg::LayoutData{});
    if (level.generated) return;  // real centroids; unplaced rooms stay listed

    std::map<std::string, std::pair<double, double>> pos;
    fallbackPositions(*level.project.layout, pos);
    std::unordered_map<std::string, size_t> idx;
    for (size_t i = 0; i < out.nodes.size(); ++i) idx.emplace(out.nodes[i].id, i);
    for (dg::TopoNode& n : out.nodes) {
        const auto it = pos.find(n.id);
        if (it == pos.end()) continue;
        n.cx = it->second.first;
        n.cz = it->second.second;
        n.minx = static_cast<int>(std::floor(n.cx - 2.0));
        n.maxx = static_cast<int>(std::ceil(n.cx + 2.0));
        n.miny = static_cast<int>(std::floor(n.cz - 1.5));
        n.maxy = static_cast<int>(std::ceil(n.cz + 1.5));
        n.hasLayout = true;  // synthetic position: draw the node, keep the empty contour
    }
    for (dg::TopoEdge& e : out.edges) {
        const auto ia = idx.find(e.a);
        const auto ib = idx.find(e.b);
        if (ia == idx.end() || ib == idx.end()) continue;
        e.lx = (out.nodes[ia->second].cx + out.nodes[ib->second].cx) * 0.5;
        e.lz = (out.nodes[ia->second].cz + out.nodes[ib->second].cz) * 0.5;
        e.labelOk = true;
    }
}

TopoGraphResult drawLayoutGraphView(const Level& level, const dg::TopoModel& model, Selection& selection,
                                    TopoGraphState& st) {
    if (!level.generated)
        ImGui::TextDisabled("auto-arranged — real positions come from the generated layout after Generate");
    return drawTopoGraph(model, selection, st);
}
