#pragma once

#include "edgar/geometry/orthogonal_line_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <clipper2/clipper.h>

#include <utility>
#include <vector>

namespace edgar::geometry {

/// Converts a clockwise orthogonal polygon to a Clipper2 path in world space.
Clipper2Lib::Path64 polygon_to_path64_world(const PolygonGrid2D& poly, Vector2Int world_origin);

/// Discrete sweep along `line`: overlap state of `moving_polygon` at each grid position vs fixed polygon at origin.
/// Port of C# `IPolygonOverlap.OverlapAlongLine` via `GridPolygonPartitioning` + open rectangle overlap (Clipper2
/// fallback in `polygon_overlap_grid2d` if decomposition is empty).
std::vector<std::pair<Vector2Int, bool>> overlap_along_line(const PolygonGrid2D& moving_polygon,
                                                            const PolygonGrid2D& fixed_polygon,
                                                            const OrthogonalLineGrid2D& line);

/// C# `ConfigurationSpacesGenerator.RemoveOverlapping` — keeps sub-segments of each line where interiors do not overlap.
std::vector<OrthogonalLineGrid2D> remove_overlapping_along_lines(const PolygonGrid2D& moving_polygon,
                                                               const PolygonGrid2D& fixed_polygon,
                                                               const std::vector<OrthogonalLineGrid2D>& lines);

} // namespace edgar::geometry
