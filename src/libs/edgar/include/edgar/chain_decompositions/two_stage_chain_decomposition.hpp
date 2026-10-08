#pragma once

// Derived from Edgar-DotNet `TwoStageChainDecomposition` (MIT).

#include "edgar/chain_decompositions/chain.hpp"
#include "edgar/chain_decompositions/i_chain_decomposition.hpp"
#include "edgar/generator/grid2d/detail/room_index_map.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace edgar::chain_decompositions {

/// Embeds stage-two rooms into chains produced on `get_stage_one_graph()` (C# `TwoStageChainDecomposition`).
template <typename TRoom>
class TwoStageChainDecomposition {
public:
    TwoStageChainDecomposition(const generator::grid2d::LevelDescriptionGrid2D<TRoom>& level,
                               const generator::grid2d::detail::RoomIndexMap<TRoom>& room_index_map,
                               IChainDecomposition<int>& stage_one_decomposition)
        : level_(level), rmap_(room_index_map), inner_(stage_one_decomposition) {}

    std::vector<Chain<int>> get_chains(const graphs::UndirectedAdjacencyListGraph<int>& full_int_graph) {
        const graphs::UndirectedAdjacencyListGraph<int> g1 = build_stage_one_int_graph();
        std::vector<Chain<int>> chains = inner_.get_chains(g1);

        std::unordered_set<int> used;
        std::vector<int> not_used_stage_two;
        for (const auto& kv : level_.rooms()) {
            if (kv.second.stage() == 2) {
                not_used_stage_two.push_back(rmap_.room_to_index.at(kv.first));
            }
        }

        for (auto& chain : chains) {
            for (const int n : chain.nodes) {
                used.insert(n);
            }
            bool progress = true;
            while (progress) {
                progress = false;
                for (auto it = not_used_stage_two.begin(); it != not_used_stage_two.end();) {
                    const int s2 = *it;
                    bool all_neighbours_used = true;
                    for (const int nb : full_int_graph.neighbours(s2)) {
                        if (!used.count(nb)) {
                            all_neighbours_used = false;
                            break;
                        }
                    }
                    if (all_neighbours_used) {
                        chain.nodes.push_back(s2);
                        used.insert(s2);
                        it = not_used_stage_two.erase(it);
                        progress = true;
                    } else {
                        ++it;
                    }
                }
            }
        }

        if (!not_used_stage_two.empty()) {
            throw std::invalid_argument("TwoStageChainDecomposition: not all stage-two rooms could be embedded");
        }
        return chains;
    }

private:
    const generator::grid2d::LevelDescriptionGrid2D<TRoom>& level_;
    const generator::grid2d::detail::RoomIndexMap<TRoom>& rmap_;
    IChainDecomposition<int>& inner_;

    graphs::UndirectedAdjacencyListGraph<int> build_stage_one_int_graph() const {
        const auto g1 = level_.get_stage_one_graph();
        graphs::UndirectedAdjacencyListGraph<int> g;
        for (const auto& kv : rmap_.room_to_index) {
            const TRoom& v = kv.first;
            if (level_.get_room_description(v).stage() != 1) {
                continue;
            }
            g.add_vertex(kv.second);
        }
        for (const TRoom& v : g1.vertices()) {
            const int vi = rmap_.room_to_index.at(v);
            for (const TRoom& n : g1.neighbours(v)) {
                const int ni = rmap_.room_to_index.at(n);
                if (vi < ni) {
                    g.add_edge(vi, ni);
                }
            }
        }
        return g;
    }
};

} // namespace edgar::chain_decompositions
