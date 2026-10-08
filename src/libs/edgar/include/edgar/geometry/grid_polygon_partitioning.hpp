#pragma once

#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/rectangle_grid2d.hpp"

#include <vector>

namespace edgar::geometry {

/// Decomposes a simple orthogonal clockwise polygon into axis-aligned rectangles (C# `GridPolygonPartitioning` /
/// rectangle-decomposition: diagonals, König / independent set, Hopcroft–Karp matching, concave splits).
std::vector<RectangleGrid2D> partition_orthogonal_polygon_to_rectangles(const PolygonGrid2D& polygon);

} // namespace edgar::geometry
