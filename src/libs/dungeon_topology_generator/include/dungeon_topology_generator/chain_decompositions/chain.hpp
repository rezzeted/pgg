#pragma once

#include <vector>

namespace dungeon_topology_generator::chain_decompositions {

/// A chain in a chain decomposition (ported from Edgar-DotNet `Chain`).
template <typename TNode>
struct Chain {
    std::vector<TNode> nodes;
    int number = 0;
    bool is_from_face = false;
};

} // namespace dungeon_topology_generator::chain_decompositions
