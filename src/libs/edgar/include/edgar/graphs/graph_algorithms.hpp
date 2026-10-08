#pragma once

#include "edgar/graphs/undirected_graph.hpp"

#include <map>
#include <map>
#include <queue>
#include <set>
#include <vector>

namespace edgar::graphs {

template <typename T>
bool is_connected(const UndirectedAdjacencyListGraph<T>& g) {
    if (g.vertex_count() == 0) {
        return true;
    }
    const auto verts = g.vertices();
    std::set<T> seen;
    std::queue<T> q;
    q.push(verts.front());
    seen.insert(verts.front());
    while (!q.empty()) {
        const T v = q.front();
        q.pop();
        for (const T& n : g.neighbours(v)) {
            if (!seen.contains(n)) {
                seen.insert(n);
                q.push(n);
            }
        }
    }
    return seen.size() == g.vertex_count();
}

template <typename T>
bool is_tree(const UndirectedAdjacencyListGraph<T>& g) {
    const std::size_t n = g.vertex_count();
    if (n == 0) {
        return true;
    }
    if (!is_connected(g)) {
        return false;
    }
    std::size_t edge_count = 0;
    for (const T& v : g.vertices()) {
        edge_count += g.neighbours(v).size();
    }
    edge_count /= 2;
    return edge_count + 1 == n;
}

template <typename T>
bool is_bipartite(const UndirectedAdjacencyListGraph<T>& g) {
    if (g.vertex_count() == 0) {
        return true;
    }
    const auto verts = g.vertices();
    std::map<T, int> color;
    for (const T& start : verts) {
        if (color.count(start)) {
            continue;
        }
        color[start] = 0;
        std::queue<T> q;
        q.push(start);
        while (!q.empty()) {
            const T v = q.front();
            q.pop();
            for (const T& n : g.neighbours(v)) {
                if (!color.count(n)) {
                    color[n] = 1 - color[v];
                    q.push(n);
                } else if (color[n] == color[v]) {
                    return false;
                }
            }
        }
    }
    return true;
}

template <typename T>
bool is_bipartite(const UndirectedAdjacencyListGraph<T>& g, std::vector<T>& partition_a, std::vector<T>& partition_b) {
    partition_a.clear();
    partition_b.clear();
    if (g.vertex_count() == 0) {
        return true;
    }
    const auto verts = g.vertices();
    std::map<T, int> color;
    for (const T& start : verts) {
        if (color.count(start)) {
            continue;
        }
        color[start] = 0;
        std::queue<T> q;
        q.push(start);
        while (!q.empty()) {
            const T v = q.front();
            q.pop();
            for (const T& n : g.neighbours(v)) {
                if (!color.count(n)) {
                    color[n] = 1 - color[v];
                    q.push(n);
                } else if (color[n] == color[v]) {
                    return false;
                }
            }
        }
    }
    for (const auto& [v, c] : color) {
        if (c == 0) {
            partition_a.push_back(v);
        } else {
            partition_b.push_back(v);
        }
    }
    return true;
}

} // namespace edgar::graphs
