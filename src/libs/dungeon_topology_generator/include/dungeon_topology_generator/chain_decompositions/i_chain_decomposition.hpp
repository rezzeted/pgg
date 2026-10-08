#pragma once

#include "dungeon_topology_generator/chain_decompositions/chain.hpp"
#include "dungeon_topology_generator/graphs/undirected_graph.hpp"

#include <vector>

namespace dungeon_topology_generator::chain_decompositions {

/// Port of C# `IChainDecomposition`.
template <typename TNode>
struct IChainDecomposition {
    virtual ~IChainDecomposition() = default;
    virtual std::vector<Chain<TNode>> get_chains(const graphs::UndirectedAdjacencyListGraph<TNode>& graph) = 0;
};

} // namespace dungeon_topology_generator::chain_decompositions
