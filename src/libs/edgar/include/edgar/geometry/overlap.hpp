#pragma once

#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

namespace edgar::geometry {

bool polygons_overlap_area(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b);

double polygons_overlap_area_exact(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b);

bool polygons_touch(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b, int min_shared_length = 1);

bool polygons_have_minimum_distance(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b, int min_distance);

PolygonGrid2D normalize_polygon(const PolygonGrid2D& polygon);

} // namespace edgar::geometry
