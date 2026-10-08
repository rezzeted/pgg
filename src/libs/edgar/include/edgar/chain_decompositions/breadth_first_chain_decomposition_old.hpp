#pragma once

#include "edgar/chain_decompositions/chain.hpp"
#include "edgar/graphs/graph_algorithms.hpp"
#include "edgar/graphs/planar_faces.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <algorithm>
#include <climits>
#include <map>
#include <stdexcept>
#include <vector>

namespace edgar::chain_decompositions {

/// Legacy breadth-first chain decomposition (matches Edgar.Tests `BreadthFirstChainDecompositionOld`).
class BreadthFirstChainDecompositionOld {
public:
    explicit BreadthFirstChainDecompositionOld(bool group_solo_vertices = true) : group_solo_vertices_(group_solo_vertices) {}

    std::vector<Chain<int>> get_chains(const graphs::UndirectedAdjacencyListGraph<int>& graph) {
        initialize(graph);

        if (!faces_.empty()) {
            const auto largest = std::max_element(faces_.begin(), faces_.end(),
                                                  [](const std::vector<int>& a, const std::vector<int>& b) {
                                                      return a.size() < b.size();
                                                  });
            if (largest != faces_.end()) {
                faces_.erase(largest);
            }
        }

        std::vector<Chain<int>> chains;

        if (!faces_.empty()) {
            chains.push_back(Chain<int>{get_first_cycle(), static_cast<int>(chains.size()), false});
        }

        while (!faces_.empty()) {
            std::vector<int> chain = get_next_cycle();
            bool is_from_cycle = true;
            if (chain.empty()) {
                chain = get_next_path();
                is_from_cycle = false;
            }
            chains.push_back(Chain<int>{std::move(chain), static_cast<int>(chains.size()), is_from_cycle});
        }

        while (has_uncovered()) {
            std::vector<int> chain = get_next_path();
            if (chain.empty()) {
                throw std::runtime_error("BreadthFirstChainDecompositionOld: expected path for uncovered vertices");
            }
            chains.push_back(Chain<int>{std::move(chain), static_cast<int>(chains.size()), false});
        }

        return chains;
    }

private:
    bool group_solo_vertices_;
    graphs::UndirectedAdjacencyListGraph<int> graph_;
    std::vector<std::vector<int>> faces_;
    std::map<int, int> depth_;
    int chains_counter_ = 0;

    void initialize(const graphs::UndirectedAdjacencyListGraph<int>& graph) {
        if (!graphs::is_connected(graph)) {
            throw std::invalid_argument("The graph must be connected");
        }
        graph_ = graph;
        faces_ = graphs::get_planar_faces(graph_);
        depth_.clear();
        for (const int v : graph_.vertices()) {
            depth_[v] = -1;
        }
        chains_counter_ = 0;
    }

    bool is_covered(int node) const { return depth_.at(node) != -1; }

    int get_depth(int node) const { return depth_.at(node); }

    void set_depth(int node, int d) { depth_[node] = d; }

    int smallest_covered_neighbour_depth(int node) {
        int smallest = INT_MAX;
        for (const int& neighbour : graph_.neighbours(node)) {
            const int d = get_depth(neighbour);
            if (d != -1 && d < smallest) {
                smallest = d;
            }
        }
        return smallest;
    }

    int uncovered_neighbours_count(int node) {
        int n = 0;
        for (const int& neighbour : graph_.neighbours(node)) {
            if (!is_covered(neighbour)) {
                ++n;
            }
        }
        return n;
    }

    bool has_uncovered() const {
        for (const auto& kv : depth_) {
            if (kv.second == -1) {
                return true;
            }
        }
        return false;
    }

    std::vector<int> get_first_cycle() {
        if (faces_.empty()) {
            throw std::invalid_argument("get_first_cycle");
        }
        const auto smallest =
            std::min_element(faces_.begin(), faces_.end(),
                             [](const std::vector<int>& a, const std::vector<int>& b) { return a.size() < b.size(); });
        std::vector<int> smallest_face = *smallest;
        faces_.erase(smallest);

        for (const int node : smallest_face) {
            set_depth(node, chains_counter_);
        }
        ++chains_counter_;
        remove_covered_nodes_from_faces();
        return smallest_face;
    }

    void remove_covered_nodes_from_faces() {
        for (int i = static_cast<int>(faces_.size()) - 1; i >= 0; --i) {
            const auto& face = faces_[static_cast<std::size_t>(i)];
            bool all = true;
            for (const int n : face) {
                if (!is_covered(n)) {
                    all = false;
                    break;
                }
            }
            if (all) {
                faces_.erase(faces_.begin() + i);
            }
        }
    }

    std::vector<int> get_next_cycle() {
        int best_face_index = -1;
        int best_face_size = INT_MAX;
        int best_face_depth = INT_MAX;

        for (std::size_t i = 0; i < faces_.size(); ++i) {
            const auto& face = faces_[i];
            std::vector<int> nodes_to_check;
            for (const int x : face) {
                nodes_to_check.push_back(x);
            }
            for (const int x : face) {
                for (const int nb : graph_.neighbours(x)) {
                    if (is_covered(nb)) {
                        nodes_to_check.push_back(nb);
                    }
                }
            }
            if (nodes_to_check.empty()) {
                continue;
            }
            int min_depth = INT_MAX;
            for (int n : nodes_to_check) {
                if (is_covered(n)) {
                    min_depth = std::min(min_depth, get_depth(n));
                }
            }
            if (min_depth == INT_MAX) {
                continue;
            }
            int size = 0;
            for (int n : face) {
                if (!is_covered(n)) {
                    ++size;
                }
            }
            if (min_depth < best_face_depth || (min_depth == best_face_depth && size < best_face_size)) {
                best_face_depth = min_depth;
                best_face_size = size;
                best_face_index = static_cast<int>(i);
            }
        }

        if (best_face_index == -1) {
            return {};
        }

        std::vector<int> next_face;
        for (int n : faces_[static_cast<std::size_t>(best_face_index)]) {
            if (!is_covered(n)) {
                next_face.push_back(n);
            }
        }
        faces_.erase(faces_.begin() + best_face_index);

        if (next_face.empty()) {
            throw std::runtime_error("get_next_cycle: empty face");
        }

        int counter = chains_counter_;
        int first_vertex_index = -1;
        for (std::size_t i = 0; i < next_face.size(); ++i) {
            bool ok = false;
            for (const int nb : graph_.neighbours(next_face[i])) {
                if (is_covered(nb)) {
                    ok = true;
                    break;
                }
            }
            if (ok) {
                first_vertex_index = static_cast<int>(i);
                break;
            }
        }
        if (first_vertex_index == -1) {
            throw std::runtime_error("get_next_cycle: no vertex neighbouring covered");
        }

        std::vector<int> chain;
        set_depth(next_face[static_cast<std::size_t>(first_vertex_index)], counter++);
        chain.push_back(next_face[static_cast<std::size_t>(first_vertex_index)]);
        next_face.erase(next_face.begin() + first_vertex_index);

        while (!next_face.empty()) {
            auto best_it =
                std::min_element(next_face.begin(), next_face.end(),
                                 [&](int a, int b) { return smallest_covered_neighbour_depth(a) < smallest_covered_neighbour_depth(b); });
            const int next_vertex = *best_it;
            set_depth(next_vertex, counter++);
            chain.push_back(next_vertex);
            next_face.erase(best_it);
        }

        for (const int node : chain) {
            set_depth(node, chains_counter_);
        }
        ++chains_counter_;
        remove_covered_nodes_from_faces();
        return chain;
    }

    std::vector<int> get_solo_neighbours(int node, bool& node_in_faces) {
        std::vector<int> solo;
        node_in_faces = false;
        for (const int neighbour : graph_.neighbours(node)) {
            if (is_covered(neighbour)) {
                continue;
            }
            if (uncovered_neighbours_count(neighbour) == 0) {
                bool in_face = false;
                for (const auto& f : faces_) {
                    for (int x : f) {
                        if (x == neighbour) {
                            in_face = true;
                            break;
                        }
                    }
                    if (in_face) {
                        break;
                    }
                }
                if (in_face) {
                    node_in_faces = true;
                    continue;
                }
                solo.push_back(neighbour);
            }
        }
        return solo;
    }

    std::vector<int> get_next_path() {
        int first_vertex = 0;
        int first_depth = INT_MAX;
        bool found_first = false;
        bool has_origin = false;
        int origin = 0;

        const auto verts = graph_.vertices();
        if (std::any_of(verts.begin(), verts.end(), [&](int x) { return is_covered(x); })) {
            for (const int node : verts) {
                if (is_covered(node)) {
                    continue;
                }
                std::vector<int> covered_neighbours;
                for (const int nb : graph_.neighbours(node)) {
                    if (is_covered(nb)) {
                        covered_neighbours.push_back(nb);
                    }
                }
                if (covered_neighbours.empty()) {
                    continue;
                }
                auto min_it = std::min_element(covered_neighbours.begin(), covered_neighbours.end(),
                                               [&](int a, int b) { return get_depth(a) < get_depth(b); });
                const int min_depth = get_depth(*min_it);
                if (min_depth < first_depth) {
                    first_depth = min_depth;
                    first_vertex = node;
                    found_first = true;
                    has_origin = true;
                    origin = *min_it;
                }
            }
        } else {
            for (const int v : verts) {
                if (graph_.neighbours(v).size() == 1) {
                    first_vertex = v;
                    found_first = true;
                    break;
                }
            }
        }

        if (!found_first) {
            throw std::runtime_error("get_next_path: no start vertex");
        }

        std::vector<int> chain;
        chain.push_back(first_vertex);
        set_depth(first_vertex, chains_counter_);

        bool node_in_faces = false;

        if (group_solo_vertices_ && has_origin && uncovered_neighbours_count(first_vertex) == 0) {
            bool nif = false;
            const auto solo = get_solo_neighbours(origin, nif);
            if (nif) {
                node_in_faces = true;
            }
            for (const int neighbour : solo) {
                chain.push_back(neighbour);
                set_depth(neighbour, chains_counter_);
            }
        }

        while (true) {
            const int last_node = chain.back();
            std::vector<int> neighbours;
            for (const int x : graph_.neighbours(last_node)) {
                if (!is_covered(x)) {
                    neighbours.push_back(x);
                }
            }
            if (neighbours.empty()) {
                break;
            }

            if (group_solo_vertices_) {
                bool nif_local = false;
                const auto solo_neighbours = get_solo_neighbours(last_node, nif_local);
                if (nif_local) {
                    node_in_faces = true;
                }
                if (!solo_neighbours.empty()) {
                    for (const int neighbour : solo_neighbours) {
                        chain.push_back(neighbour);
                        set_depth(neighbour, chains_counter_);
                    }
                    break;
                }
            }

            const int next_node = neighbours.front();
            for (const auto& f : faces_) {
                for (int x : f) {
                    if (x == next_node) {
                        node_in_faces = true;
                        break;
                    }
                }
            }
            chain.push_back(next_node);
            set_depth(next_node, chains_counter_);

            if (node_in_faces) {
                break;
            }
        }

        ++chains_counter_;
        remove_covered_nodes_from_faces();
        return chain;
    }
};

} // namespace edgar::chain_decompositions
