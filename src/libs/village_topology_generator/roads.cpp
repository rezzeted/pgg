#include "pch.h"

#include "roads.h"

namespace village_topology_generator {
namespace {

struct Node {
    double f;
    double h;
    int index;
};

struct NodeGreater {
    bool operator()(const Node& a, const Node& b) const {
        if (a.f != b.f) return a.f > b.f;
        if (a.h != b.h) return a.h > b.h;
        return a.index > b.index;
    }
};

// Admissible heuristic: octile distance; every step costs at least its length
// (cell cost >= 1).
double octile(int dx, int dy) {
    const int lo = std::min(std::abs(dx), std::abs(dy));
    const int hi = std::max(std::abs(dx), std::abs(dy));
    return hi + (std::sqrt(2.0) - 1.0) * lo;
}

// A* core over a raw cost field (row-major, w*h). Kept separate from
// Analysis so the skeleton builder can run it on its discounted copy.
// h_scale: heuristic multiplier = minimum possible cell cost in the field,
// keeping the octile heuristic admissible (1.0 for raw cost, road_discount
// for a discounted field).
std::optional<RoadPath> astar_path(const double* cost, int w, int h,
                                   std::pair<int, int> from,
                                   std::pair<int, int> to,
                                   double h_scale) {
    auto in_bounds = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };
    if (!in_bounds(from.first, from.second) || !in_bounds(to.first, to.second)) {
        return std::nullopt;
    }
    const double inf = std::numeric_limits<double>::infinity();
    const int start = from.second * w + from.first;
    const int goal = to.second * w + to.first;
    if (cost[start] == inf || cost[goal] == inf) {
        return std::nullopt;
    }

    std::vector<double> g(static_cast<size_t>(w) * static_cast<size_t>(h), inf);
    std::vector<int> parent(g.size(), -1);
    std::vector<uint8_t> closed(g.size(), 0);
    std::priority_queue<Node, std::vector<Node>, NodeGreater> open;

    g[start] = 0.0;
    const double h0 = h_scale * octile(to.first - from.first, to.second - from.second);
    open.push({h0, h0, start});

    static constexpr int kDirs[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
    };

    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        if (closed[cur.index]) continue;
        closed[cur.index] = 1;
        if (cur.index == goal) break;
        const int cx = cur.index % w;
        const int cy = cur.index / w;
        for (const auto& d : kDirs) {
            const int nx = cx + d[0];
            const int ny = cy + d[1];
            if (!in_bounds(nx, ny)) continue;
            const int ni = ny * w + nx;
            if (closed[ni]) continue;
            const double cell_cost = cost[ni];
            if (cell_cost == inf) continue;
            const double step_len = (d[0] != 0 && d[1] != 0) ? std::sqrt(2.0) : 1.0;
            // Entering cost over the mean of both cells: symmetric roads.
            const double step = step_len * 0.5 * (cost[cur.index] + cell_cost);
            const double ng = g[cur.index] + step;
            if (ng < g[ni]) {
                g[ni] = ng;
                parent[ni] = cur.index;
                const double hh = h_scale * octile(to.first - nx, to.second - ny);
                open.push({ng + hh, hh, ni});
            }
        }
    }

    if (goal != start && parent[goal] == -1) {
        return std::nullopt;
    }

    RoadPath path;
    path.total_cost = g[goal];
    for (int i = goal; i != -1; i = parent[i]) {
        path.cells.emplace_back(i % w, i / w);
    }
    std::reverse(path.cells.begin(), path.cells.end());
    return path;
}

}  // namespace

std::optional<RoadPath> find_road(const Analysis& analysis,
                                  std::pair<int, int> from,
                                  std::pair<int, int> to) {
    return astar_path(analysis.cost.data(), analysis.width, analysis.height, from, to, 1.0);
}

namespace {

void discount_road_cells(std::vector<double>& cost, int w, const RoadPath& path, double discount) {
    for (const auto& [x, y] : path.cells) {
        double& c = cost[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        // Idempotent min(): a cell shared by several roads keeps the same
        // discount, so the heuristic scale in astar_path stays valid.
        if (std::isfinite(c)) {
            c = std::min(c, discount);
        }
    }
}

// Multi-source Dijkstra over the cost field: dist[i] = least cost to reach
// any source, parent[i] = next step toward it (-2 at sources). 8-connected,
// deterministic tie-break (dist, index).
void dijkstra_forest(const double* cost, int w, int h,
                     const std::vector<int>& sources,
                     std::vector<double>& dist, std::vector<int>& parent) {
    const double inf = std::numeric_limits<double>::infinity();
    dist.assign(static_cast<size_t>(w) * static_cast<size_t>(h), inf);
    parent.assign(dist.size(), -1);
    std::priority_queue<Node, std::vector<Node>, NodeGreater> open;
    std::vector<uint8_t> closed(dist.size(), 0);
    for (const int s : sources) {
        if (cost[s] == inf || dist[s] == 0.0) continue;
        dist[s] = 0.0;
        parent[s] = -2;
        open.push({0.0, 0.0, s});
    }
    static constexpr int kDirs[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
    };
    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        if (closed[cur.index]) continue;
        closed[cur.index] = 1;
        const int cx = cur.index % w;
        const int cy = cur.index / w;
        for (const auto& d : kDirs) {
            const int nx = cx + d[0];
            const int ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const int ni = ny * w + nx;
            if (closed[ni] || cost[ni] == inf) continue;
            const double step_len = (d[0] != 0 && d[1] != 0) ? std::sqrt(2.0) : 1.0;
            const double step = step_len * 0.5 * (cost[cur.index] + cost[ni]);
            const double nd = dist[cur.index] + step;
            if (nd < dist[ni]) {
                dist[ni] = nd;
                parent[ni] = cur.index;
                open.push({nd, 0.0, ni});
            }
        }
    }
}

// Walk the parent tree from `from` back to a source. nullopt when `from` is
// disconnected or is itself a source (empty road).
std::optional<RoadPath> walk_to_source(const std::vector<double>& dist,
                                       const std::vector<int>& parent,
                                       int w, std::pair<int, int> from) {
    const int start = from.second * w + from.first;
    if (parent[start] == -1 || parent[start] == -2) return std::nullopt;
    RoadPath path;
    path.total_cost = dist[start];
    for (int i = start; i >= 0; i = parent[i]) {
        path.cells.emplace_back(i % w, i / w);
    }
    return path;
}

}  // namespace

RoadNetwork grow_skeleton(const Analysis& analysis,
                          std::pair<int, int> center,
                          const std::vector<std::pair<int, int>>& anchors,
                          const SkeletonParams& params) {
    RoadNetwork net;
    const int w = analysis.width;
    const int h = analysis.height;
    net.cost_with_roads = analysis.cost;
    if (w <= 0 || h <= 0) return net;

    // Heuristic scale: the cheapest possible cell after all discounts is
    // road_discount, so scaling keeps the heuristic admissible and the search
    // optimal w.r.t. the discounted field (this is what makes merging happen
    // instead of a greedy beeline to each anchor).
    const double h_scale = params.merge_roads ? params.road_discount : 1.0;
    const double discount = params.merge_roads ? params.road_discount : 1.0;

    auto by_distance_to = [](std::pair<int, int> ref) {
        return [ref](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            const long long dxa = a.first - ref.first;
            const long long dya = a.second - ref.second;
            const long long dxb = b.first - ref.first;
            const long long dyb = b.second - ref.second;
            const long long d2a = dxa * dxa + dya * dya;
            const long long d2b = dxb * dxb + dyb * dyb;
            if (d2a != d2b) return d2a < d2b;
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        };
    };

    if (params.style == GrowthStyle::trunk) {
        // Unique targets (anchors + center), sorted for deterministic picks.
        std::vector<std::pair<int, int>> targets = anchors;
        targets.push_back(center);
        std::sort(targets.begin(), targets.end(), by_distance_to({0, 0}));
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        if (targets.size() < 2) return net;

        // Trunk = farthest pair; strict > keeps the first pair on ties.
        size_t bi = 0, bj = 1;
        long long best_d2 = -1;
        for (size_t i = 0; i < targets.size(); ++i) {
            for (size_t j = i + 1; j < targets.size(); ++j) {
                const long long dx = targets[i].first - targets[j].first;
                const long long dy = targets[i].second - targets[j].second;
                const long long d2 = dx * dx + dy * dy;
                if (d2 > best_d2) {
                    best_d2 = d2;
                    bi = i;
                    bj = j;
                }
            }
        }
        auto trunk = astar_path(net.cost_with_roads.data(), w, h, targets[bi], targets[bj], h_scale);
        if (!trunk) return net;
        if (params.merge_roads) {
            discount_road_cells(net.cost_with_roads, w, *trunk, discount);
        }
        std::vector<int> road_cells;
        road_cells.reserve(trunk->cells.size());
        for (const auto& [x, y] : trunk->cells) {
            road_cells.push_back(y * w + x);
        }
        net.reached.push_back(targets[bi]);
        net.reached.push_back(targets[bj]);
        net.roads.push_back(std::move(*trunk));

        std::vector<std::pair<int, int>> remaining;
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i != bi && i != bj) remaining.push_back(targets[i]);
        }
        // Branches: each target connects to the nearest (by cost) road cell;
        // nearest-cost target first, ties by (y, x). Dijkstra is rerun after
        // each branch so later branches see freshly discounted cells.
        std::vector<double> dist;
        std::vector<int> parent;
        while (!remaining.empty()) {
            dijkstra_forest(net.cost_with_roads.data(), w, h, road_cells, dist, parent);
            size_t pick = 0;
            double best = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < remaining.size(); ++i) {
                const auto& t = remaining[i];
                const double d = dist[static_cast<size_t>(t.second) * w + t.first];
                if (d < best) {
                    best = d;
                    pick = i;
                }
            }
            const auto target = remaining[pick];
            remaining.erase(remaining.begin() + static_cast<long>(pick));
            auto branch = walk_to_source(dist, parent, w, target);
            if (!branch) continue;  // unreachable or already on a road
            if (params.merge_roads) {
                discount_road_cells(net.cost_with_roads, w, *branch, discount);
            }
            for (const auto& [x, y] : branch->cells) {
                road_cells.push_back(y * w + x);
            }
            net.reached.push_back(target);
            net.roads.push_back(std::move(*branch));
        }
        return net;
    }

    // radial: center to each anchor, nearest anchors first.
    std::vector<std::pair<int, int>> ordered;
    for (const auto& a : anchors) {
        if (a != center) ordered.push_back(a);
    }
    std::sort(ordered.begin(), ordered.end(), by_distance_to(center));

    for (const auto& anchor : ordered) {
        auto path = astar_path(net.cost_with_roads.data(), w, h, center, anchor, h_scale);
        if (!path) continue;
        if (params.merge_roads) {
            discount_road_cells(net.cost_with_roads, w, *path, discount);
        }
        net.roads.push_back(std::move(*path));
        net.reached.push_back(anchor);
    }
    return net;
}

}  // namespace village_topology_generator
