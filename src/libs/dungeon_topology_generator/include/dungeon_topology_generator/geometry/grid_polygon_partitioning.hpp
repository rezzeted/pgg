#pragma once

#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/rectangle_grid2d.hpp"

#include <vector>

namespace dungeon_topology_generator::geometry {

/// Decomposes a simple orthogonal clockwise polygon into axis-aligned rectangles (C# `GridPolygonPartitioning` /
/// rectangle-decomposition: diagonals, König / independent set, Hopcroft–Karp matching, concave splits).
std::vector<RectangleGrid2D> partition_orthogonal_polygon_to_rectangles(const PolygonGrid2D& polygon);

} // namespace dungeon_topology_generator::geometry
