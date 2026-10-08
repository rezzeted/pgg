#pragma once

#include <utility>
#include <vector>

namespace edgar::geometry {

/// Hopcroft–Karp maximum matching: left vertices [0, n_left), right [0, n_right), edges (u, v).
std::vector<std::pair<int, int>> hopcroft_karp_max_matching(int n_left, int n_right,
                                                             const std::vector<std::pair<int, int>>& edges);

/// König: minimum vertex cover as global ids — left ids [0, n_left), right ids [n_left, n_left + n_right).
std::pair<std::vector<int>, std::vector<int>> bipartite_min_vertex_cover(
    int n_left, int n_right, const std::vector<std::pair<int, int>>& edges);

/// Maximum independent set (complement of min vertex cover in bipartite graphs).
std::vector<int> bipartite_max_independent_set(int n_left, int n_right,
                                                const std::vector<std::pair<int, int>>& edges);

} // namespace edgar::geometry
