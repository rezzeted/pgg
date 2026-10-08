#pragma once

// Derived from Edgar-DotNet `PolygonOverlapBase` (MIT) — overlap along line via rectangle partitions + grid sweep.

#include "dungeon_topology_generator/geometry/orthogonal_line_grid2d.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/rectangle_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"

#include <utility>
#include <vector>

namespace dungeon_topology_generator::geometry {

bool rectangles_overlap_open(const RectangleGrid2D& a, const RectangleGrid2D& b);

/// Content-cached rectangle partition of an orthogonal polygon (thread-local; partitions are
/// pure functions of the point list, so entries never go stale). Empty when partitioning fails.
const std::vector<RectangleGrid2D>& cached_partition(const PolygonGrid2D& polygon);

/// Exact interior-overlap test via cached rectangle partitions (falls back to Clipper2 when
/// a polygon does not partition). Much cheaper than a boolean op per query.
bool polygons_overlap_via_partitions(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b,
                                     Vector2Int pos_b);

/// Same semantics as C# `IPolygonOverlap.OverlapAlongLine` for integer grid: events along `line` where overlap toggles.
/// Uses merged axis-aligned rectangle intervals along the scan line when both polygons partition cleanly; otherwise
/// falls back to a per-cell check (same as the historical brute-force path in `detail`).
std::vector<std::pair<Vector2Int, bool>> overlap_along_line_polygon_partition(const PolygonGrid2D& moving_polygon,
                                                                             const PolygonGrid2D& fixed_polygon,
                                                                             const OrthogonalLineGrid2D& line);

namespace detail {

/// Brute-force sweep (one overlap test per grid point on `line`). For tests and regression against merged intervals.
std::vector<std::pair<Vector2Int, bool>> overlap_along_line_polygon_partition_bruteforce(
    const PolygonGrid2D& moving_polygon, const PolygonGrid2D& fixed_polygon, const OrthogonalLineGrid2D& line);

} // namespace detail

} // namespace dungeon_topology_generator::geometry
