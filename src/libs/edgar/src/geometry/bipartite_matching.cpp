#include "edgar/geometry/bipartite_matching.hpp"

#include <queue>
#include <unordered_set>

namespace edgar::geometry {

namespace {

std::vector<std::pair<int, int>> kuhn_max_matching(int n_left, int n_right,
                                                   const std::vector<std::pair<int, int>>& edges) {
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(n_left));
    for (const auto& e : edges) {
        adj[static_cast<std::size_t>(e.first)].push_back(e.second);
    }
    std::vector<int> pair_r(static_cast<std::size_t>(n_right), -1);
    std::vector<char> seen;
    const auto try_augment = [&](auto&& self, int x) -> bool {
        for (int y : adj[static_cast<std::size_t>(x)]) {
            if (seen[static_cast<std::size_t>(y)]) {
                continue;
            }
            seen[static_cast<std::size_t>(y)] = 1;
            const int px = pair_r[static_cast<std::size_t>(y)];
            if (px < 0 || self(self, px)) {
                pair_r[static_cast<std::size_t>(y)] = x;
                return true;
            }
        }
        return false;
    };
    for (int u = 0; u < n_left; ++u) {
        seen.assign(static_cast<std::size_t>(n_right), 0);
        try_augment(try_augment, u);
    }
    std::vector<std::pair<int, int>> out;
    for (int v = 0; v < n_right; ++v) {
        const int u = pair_r[static_cast<std::size_t>(v)];
        if (u >= 0) {
            out.emplace_back(u, v);
        }
    }
    return out;
}

} // namespace

std::vector<std::pair<int, int>> hopcroft_karp_max_matching(int n_left, int n_right,
                                                            const std::vector<std::pair<int, int>>& edges) {
    return kuhn_max_matching(n_left, n_right, edges);
}

std::pair<std::vector<int>, std::vector<int>> bipartite_min_vertex_cover(
    int n_left, int n_right, const std::vector<std::pair<int, int>>& edges) {
    const auto matching = kuhn_max_matching(n_left, n_right, edges);
    std::vector<int> pair_l(static_cast<std::size_t>(n_left), -1);
    std::vector<int> pair_r(static_cast<std::size_t>(n_right), -1);
    for (const auto& e : matching) {
        pair_l[static_cast<std::size_t>(e.first)] = e.second;
        pair_r[static_cast<std::size_t>(e.second)] = e.first;
    }
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(n_left));
    for (const auto& e : edges) {
        adj[static_cast<std::size_t>(e.first)].push_back(e.second);
    }

    std::vector<char> vis_l(static_cast<std::size_t>(n_left), 0);
    std::vector<char> vis_r(static_cast<std::size_t>(n_right), 0);
    std::queue<std::pair<int, bool>> q;
    for (int u = 0; u < n_left; ++u) {
        if (pair_l[static_cast<std::size_t>(u)] < 0) {
            vis_l[static_cast<std::size_t>(u)] = 1;
            q.emplace(u, false);
        }
    }
    while (!q.empty()) {
        const auto [x, from_right] = q.front();
        q.pop();
        if (!from_right) {
            const int u = x;
            for (int v : adj[static_cast<std::size_t>(u)]) {
                if (v == pair_l[static_cast<std::size_t>(u)]) {
                    continue;
                }
                if (!vis_r[static_cast<std::size_t>(v)]) {
                    vis_r[static_cast<std::size_t>(v)] = 1;
                    q.emplace(v, true);
                }
            }
        } else {
            const int v = x;
            const int u = pair_r[static_cast<std::size_t>(v)];
            if (u >= 0 && !vis_l[static_cast<std::size_t>(u)]) {
                vis_l[static_cast<std::size_t>(u)] = 1;
                q.emplace(u, false);
            }
        }
    }

    std::vector<int> cover_l;
    std::vector<int> cover_r;
    for (int u = 0; u < n_left; ++u) {
        if (!vis_l[static_cast<std::size_t>(u)]) {
            cover_l.push_back(u);
        }
    }
    for (int v = 0; v < n_right; ++v) {
        if (vis_r[static_cast<std::size_t>(v)]) {
            cover_r.push_back(n_left + v);
        }
    }
    return {std::move(cover_l), std::move(cover_r)};
}

std::vector<int> bipartite_max_independent_set(int n_left, int n_right,
                                               const std::vector<std::pair<int, int>>& edges) {
    const auto cover = bipartite_min_vertex_cover(n_left, n_right, edges);
    std::unordered_set<int> in_cover;
    for (int x : cover.first) {
        in_cover.insert(x);
    }
    for (int x : cover.second) {
        in_cover.insert(x);
    }
    std::vector<int> out;
    const int n = n_left + n_right;
    for (int i = 0; i < n; ++i) {
        if (!in_cover.count(i)) {
            out.push_back(i);
        }
    }
    return out;
}

} // namespace edgar::geometry
