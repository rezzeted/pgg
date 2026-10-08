#pragma once

#include "edgar/chain_decompositions/chain.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <vector>

namespace edgar::chain_decompositions {

/// Port of C# `IChainDecomposition`.
template <typename TNode>
struct IChainDecomposition {
    virtual ~IChainDecomposition() = default;
    virtual std::vector<Chain<TNode>> get_chains(const graphs::UndirectedAdjacencyListGraph<TNode>& graph) = 0;
};

} // namespace edgar::chain_decompositions
