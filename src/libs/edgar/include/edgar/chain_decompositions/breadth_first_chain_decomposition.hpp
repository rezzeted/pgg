#pragma once

#include "edgar/chain_decompositions/chain.hpp"
#include "edgar/chain_decompositions/chain_decomposition_configuration.hpp"
#include "edgar/chain_decompositions/i_chain_decomposition.hpp"
#include "edgar/chain_decompositions/tree_component_strategy.hpp"
#include "edgar/graphs/graph_algorithms.hpp"
#include "edgar/graphs/planar_faces.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace edgar::chain_decompositions {

/// C# `BreadthFirstChainDecomposition` (partial decomposition + extend), `int` vertices only (planar faces).
class BreadthFirstChainDecomposition final : public IChainDecomposition<int> {
public:
    explicit BreadthFirstChainDecomposition(ChainDecompositionConfiguration configuration = {})
        : max_tree_size_(configuration.max_tree_size),
          merge_small_chains_(configuration.merge_small_chains),
          start_tree_with_multiple_vertices_(configuration.start_tree_with_multiple_vertices),
          prefer_small_cycles_(configuration.prefer_small_cycles),
          tree_strategy_(configuration.tree_component_strategy) {}

    std::vector<Chain<int>> get_chains(const graphs::UndirectedAdjacencyListGraph<int>& graph) override {
        initialize(graph);

        if (!faces_.empty()) {
            const auto it = std::max_element(faces_.begin(), faces_.end(),
                                             [](const std::vector<int>& a, const std::vector<int>& b) {
                                                 return a.size() < b.size();
                                             });
            if (it != faces_.end()) {
                faces_.erase(it);
            }
        }

        PartialDecomposition decomposition(std::move(faces_));
        decomposition = get_first_component(std::move(decomposition));

        while (static_cast<int>(decomposition.all_covered_vertices().size()) != static_cast<int>(graph_.vertex_count())) {
            decomposition = extend_decomposition(std::move(decomposition));
        }

        return decomposition.final_chains();
    }

private:
    int max_tree_size_;
    bool merge_small_chains_;
    bool start_tree_with_multiple_vertices_;
    bool prefer_small_cycles_;
    TreeComponentStrategy tree_strategy_;

    graphs::UndirectedAdjacencyListGraph<int> graph_;
    std::vector<std::vector<int>> faces_;

    void initialize(const graphs::UndirectedAdjacencyListGraph<int>& graph) {
        if (!graphs::is_connected(graph)) {
            throw std::invalid_argument("The graph must be connected");
        }
        graph_ = graph;
        faces_ = graphs::get_planar_faces(graph_);
    }

    struct GraphComponent {
        std::vector<int> nodes;
        bool is_from_face = false;
        int minimum_neighbor_chain_number = -1;
    };

    class PartialDecomposition {
    public:
        explicit PartialDecomposition(std::vector<std::vector<int>> remaining_faces)
            : remaining_faces_(std::move(remaining_faces)) {}

        PartialDecomposition add_chain(const std::vector<int>& chain, bool is_from_face) {
            PartialDecomposition next = *this;
            const int idx = static_cast<int>(next.chains_.size());
            for (const int v : chain) {
                next.covered_[v] = idx;
            }
            next.remaining_faces_.erase(
                std::remove_if(next.remaining_faces_.begin(), next.remaining_faces_.end(),
                               [&](const std::vector<int>& face) {
                                   return std::all_of(face.begin(), face.end(),
                                                      [&](int n) { return next.covered_.count(n) != 0; });
                               }),
                next.remaining_faces_.end());
            next.chains_.push_back(Chain<int>{chain, idx, is_from_face});
            return next;
        }

        std::vector<int> all_covered_vertices() const {
            std::vector<int> out;
            out.reserve(covered_.size());
            for (const auto& kv : covered_) {
                out.push_back(kv.first);
            }
            return out;
        }

        bool is_covered(int node) const { return covered_.count(node) != 0; }

        int get_chain_number(int node) const {
            const auto it = covered_.find(node);
            if (it == covered_.end()) {
                return -1;
            }
            return it->second;
        }

        const std::vector<std::vector<int>>& remaining_face_list() const { return remaining_faces_; }

        std::vector<Chain<int>> final_chains() const { return chains_; }

    private:
        friend class BreadthFirstChainDecomposition;
        std::map<int, int> covered_;
        std::vector<std::vector<int>> remaining_faces_;
        std::vector<Chain<int>> chains_;
    };

    PartialDecomposition get_first_component(PartialDecomposition decomposition) {
        const auto& faces = decomposition.remaining_face_list();
        if (!faces.empty()) {
            std::vector<int> first_face;
            if (prefer_small_cycles_) {
                const auto it = std::min_element(faces.begin(), faces.end(),
                                                 [](const std::vector<int>& a, const std::vector<int>& b) {
                                                     return a.size() < b.size();
                                                 });
                first_face = *it;
            } else {
                const auto it = std::max_element(faces.begin(), faces.end(),
                                                 [](const std::vector<int>& a, const std::vector<int>& b) {
                                                     return a.size() < b.size();
                                                 });
                first_face = *it;
            }
            return decomposition.add_chain(first_face, true);
        }

        const auto verts = graph_.vertices();
        int starting_node = verts.front();
        for (const int v : verts) {
            if (graph_.neighbours(v).size() == 1) {
                starting_node = v;
                break;
            }
        }
        const GraphComponent tree = get_tree_component(decomposition, {starting_node});
        return decomposition.add_chain(tree.nodes, false);
    }

    bool face_contains(const std::vector<int>& face, int node) const {
        for (int x : face) {
            if (x == node) {
                return true;
            }
        }
        return false;
    }

    bool node_in_any_remaining_face(const PartialDecomposition& d, int node) const {
        for (const auto& face : d.remaining_face_list()) {
            if (face_contains(face, node)) {
                return true;
            }
        }
        return false;
    }

    PartialDecomposition extend_decomposition(PartialDecomposition decomposition) {
        const auto& remaining_face = decomposition.remaining_face_list();
        std::set<int> blacklist;
        std::vector<GraphComponent> components;

        for (const int node : decomposition.all_covered_vertices()) {
            std::vector<int> tree_starting_nodes;
            for (const int neighbor : graph_.neighbours(node)) {
                if (decomposition.is_covered(neighbor)) {
                    continue;
                }
                if (blacklist.count(neighbor) != 0) {
                    continue;
                }
                bool found_face = false;
                for (const auto& face : remaining_face) {
                    if (!face_contains(face, neighbor)) {
                        continue;
                    }
                    GraphComponent cycle_component = get_cycle_component(decomposition, face);
                    for (int x : cycle_component.nodes) {
                        blacklist.insert(x);
                    }
                    components.push_back(std::move(cycle_component));
                    found_face = true;
                    break;
                }
                if (found_face) {
                    continue;
                }
                tree_starting_nodes.push_back(neighbor);
            }
            if (!tree_starting_nodes.empty()) {
                if (start_tree_with_multiple_vertices_) {
                    GraphComponent tree_component = get_tree_component(decomposition, tree_starting_nodes);
                    for (int x : tree_component.nodes) {
                        blacklist.insert(x);
                    }
                    components.push_back(std::move(tree_component));
                } else {
                    for (const int starting_node : tree_starting_nodes) {
                        GraphComponent tree_component = get_tree_component(decomposition, {starting_node});
                        for (int x : tree_component.nodes) {
                            blacklist.insert(x);
                        }
                        components.push_back(std::move(tree_component));
                    }
                }
            }
        }

        std::vector<GraphComponent> cycle_components;
        for (auto& c : components) {
            if (c.is_from_face) {
                cycle_components.push_back(std::move(c));
            }
        }
        if (!cycle_components.empty()) {
            const auto cmp = [&](const GraphComponent& a, const GraphComponent& b) {
                return a.nodes.size() < b.nodes.size();
            };
            auto best_it = prefer_small_cycles_ ? std::min_element(cycle_components.begin(), cycle_components.end(), cmp)
                                                : std::max_element(cycle_components.begin(), cycle_components.end(), cmp);
            return decomposition.add_chain(best_it->nodes, true);
        }

        std::vector<GraphComponent> tree_components;
        for (auto& c : components) {
            if (!c.is_from_face) {
                tree_components.push_back(std::move(c));
            }
        }
        if (tree_components.empty()) {
            throw std::runtime_error("BreadthFirstChainDecomposition: no tree components");
        }
        std::sort(tree_components.begin(), tree_components.end(),
                  [](const GraphComponent& a, const GraphComponent& b) {
                      if (a.minimum_neighbor_chain_number != b.minimum_neighbor_chain_number) {
                          return a.minimum_neighbor_chain_number < b.minimum_neighbor_chain_number;
                      }
                      return a.nodes.size() > b.nodes.size();
                  });
        GraphComponent biggest_tree = std::move(tree_components.front());
        if (merge_small_chains_ && static_cast<int>(biggest_tree.nodes.size()) < max_tree_size_) {
            for (std::size_t i = 1; i < tree_components.size(); ++i) {
                auto& component = tree_components[i];
                if (static_cast<int>(biggest_tree.nodes.size() + component.nodes.size()) <= max_tree_size_) {
                    biggest_tree.nodes.insert(biggest_tree.nodes.end(), component.nodes.begin(),
                                              component.nodes.end());
                }
            }
        }
        return decomposition.add_chain(biggest_tree.nodes, false);
    }

    GraphComponent get_tree_component(const PartialDecomposition& decomposition,
                                      const std::vector<int>& starting_nodes) const {
        switch (tree_strategy_) {
        case TreeComponentStrategy::breadth_first:
            return get_bfs_tree_component(decomposition, starting_nodes);
        case TreeComponentStrategy::depth_first:
            return get_dfs_tree_component(decomposition, starting_nodes);
        }
        throw std::invalid_argument("BreadthFirstChainDecomposition: invalid tree strategy");
    }

    GraphComponent get_bfs_tree_component(const PartialDecomposition& decomposition,
                                          const std::vector<int>& starting_nodes) const {
        std::vector<int> nodes = starting_nodes;
        std::queue<int> q;
        for (const int s : starting_nodes) {
            q.push(s);
        }
        while (!q.empty() && static_cast<int>(nodes.size()) < max_tree_size_) {
            const int node = q.front();
            q.pop();
            if (node_in_any_remaining_face(decomposition, node)) {
                continue;
            }
            for (const int neighbor : graph_.neighbours(node)) {
                if (std::find(nodes.begin(), nodes.end(), neighbor) != nodes.end()) {
                    continue;
                }
                if (decomposition.is_covered(neighbor)) {
                    continue;
                }
                nodes.push_back(neighbor);
                q.push(neighbor);
                if (static_cast<int>(nodes.size()) >= max_tree_size_) {
                    break;
                }
            }
        }
        GraphComponent g;
        g.nodes = std::move(nodes);
        g.is_from_face = false;
        g.minimum_neighbor_chain_number = get_minimum_neighbor_chain_number(decomposition, g.nodes);
        return g;
    }

    GraphComponent get_dfs_tree_component(const PartialDecomposition& decomposition,
                                          const std::vector<int>& starting_nodes) const {
        std::vector<int> nodes = starting_nodes;
        std::vector<int> stack = starting_nodes;
        while (!stack.empty() && static_cast<int>(nodes.size()) < max_tree_size_) {
            const int node = stack.back();
            stack.pop_back();
            if (node_in_any_remaining_face(decomposition, node)) {
                continue;
            }
            for (const int neighbor : graph_.neighbours(node)) {
                if (std::find(nodes.begin(), nodes.end(), neighbor) != nodes.end()) {
                    continue;
                }
                if (decomposition.is_covered(neighbor)) {
                    continue;
                }
                nodes.push_back(neighbor);
                stack.push_back(neighbor);
                if (static_cast<int>(nodes.size()) >= max_tree_size_) {
                    break;
                }
            }
        }
        GraphComponent g;
        g.nodes = std::move(nodes);
        g.is_from_face = false;
        g.minimum_neighbor_chain_number = get_minimum_neighbor_chain_number(decomposition, g.nodes);
        return g;
    }

    int get_minimum_neighbor_chain_number(const PartialDecomposition& decomposition,
                                          const std::vector<int>& nodes) const {
        std::vector<int> covered_neighbours;
        for (const int n : nodes) {
            for (const int nb : graph_.neighbours(n)) {
                if (decomposition.is_covered(nb)) {
                    covered_neighbours.push_back(nb);
                }
            }
        }
        if (covered_neighbours.empty()) {
            return -1;
        }
        int m = INT_MAX;
        for (const int nb : covered_neighbours) {
            m = std::min(m, decomposition.get_chain_number(nb));
        }
        return m;
    }

    GraphComponent get_cycle_component(const PartialDecomposition& decomposition,
                                       const std::vector<int>& face) const {
        std::vector<int> not_covered;
        for (int x : face) {
            if (!decomposition.is_covered(x)) {
                not_covered.push_back(x);
            }
        }
        std::vector<int> nodes;
        std::map<int, int> node_order;
        while (!not_covered.empty()) {
            auto best_it = not_covered.begin();
            int best_score = INT_MAX;
            for (auto it = not_covered.begin(); it != not_covered.end(); ++it) {
                const int x = *it;
                int inner_min = INT_MAX;
                for (const int y : graph_.neighbours(x)) {
                    int s = 0;
                    if (decomposition.is_covered(y)) {
                        s = -1;
                    } else if (node_order.count(y) != 0) {
                        s = node_order.at(y);
                    } else {
                        s = INT_MAX;
                    }
                    inner_min = std::min(inner_min, s);
                }
                if (inner_min < best_score) {
                    best_score = inner_min;
                    best_it = it;
                }
            }
            const int chosen = *best_it;
            node_order[chosen] = static_cast<int>(node_order.size());
            nodes.push_back(chosen);
            not_covered.erase(best_it);
        }
        GraphComponent g;
        g.nodes = std::move(nodes);
        g.is_from_face = true;
        g.minimum_neighbor_chain_number = get_minimum_neighbor_chain_number(decomposition, g.nodes);
        return g;
    }
};

} // namespace edgar::chain_decompositions
