#pragma once

// Derived from Edgar-DotNet `OrthogonalLineIntersection` (MIT).

#include "edgar/geometry/orthogonal_line_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <vector>

namespace edgar::geometry {

/// C# `OrthogonalLineIntersection` — orthogonal segment intersections and `RemoveIntersections`.
class OrthogonalLineIntersection {
public:
    static bool try_get_intersection(const OrthogonalLineGrid2D& line1, const OrthogonalLineGrid2D& line2,
                                     OrthogonalLineGrid2D& intersection_out);

    static std::vector<OrthogonalLineGrid2D> get_intersections(const std::vector<OrthogonalLineGrid2D>& lines1,
                                                             const std::vector<OrthogonalLineGrid2D>& lines2);

    /// Split segments so no two overlap (except at endpoints); order preserved approximately.
    static std::vector<OrthogonalLineGrid2D> remove_intersections(const std::vector<OrthogonalLineGrid2D>& lines);

    static std::vector<OrthogonalLineGrid2D> partition_by_intersection(const OrthogonalLineGrid2D& line,
                                                                       const std::vector<OrthogonalLineGrid2D>& intersection);
};

} // namespace edgar::geometry
