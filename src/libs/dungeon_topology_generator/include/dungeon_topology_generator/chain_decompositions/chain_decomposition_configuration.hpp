#pragma once

#include "dungeon_topology_generator/chain_decompositions/tree_component_strategy.hpp"

namespace dungeon_topology_generator::chain_decompositions {

/// Settings for `BreadthFirstChainDecomposition` (new algorithm).
struct ChainDecompositionConfiguration {
    int max_tree_size = 8;
    bool merge_small_chains = true;
    bool start_tree_with_multiple_vertices = true;
    TreeComponentStrategy tree_component_strategy = TreeComponentStrategy::breadth_first;
    bool prefer_small_cycles = true;
};

} // namespace dungeon_topology_generator::chain_decompositions
