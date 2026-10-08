#pragma once

#include "dungeon_topology_generator/graphs/undirected_graph.hpp"

#include <stdexcept>
#include <vector>

namespace dungeon_topology_generator::graphs {

bool is_planar(const UndirectedAdjacencyListGraph<int>& g);

std::vector<std::vector<int>> get_planar_faces(const UndirectedAdjacencyListGraph<int>& g);

std::vector<std::vector<int>> get_cycles(const UndirectedAdjacencyListGraph<int>& g);

} // namespace dungeon_topology_generator::graphs
