#include "edgar/geometry/clipper2_util.hpp"
#include "edgar/geometry/overlap.hpp"
#include "edgar/geometry/polygon_overlap_grid2d.hpp"

#include <optional>

namespace edgar::geometry {

Clipper2Lib::Path64 polygon_to_path64_world(const PolygonGrid2D& poly, Vector2Int world_origin) {
    Clipper2Lib::Path64 path;
    path.reserve(poly.points().size());
    for (const auto& p : poly.points()) {
        path.push_back(Clipper2Lib::Point64(p.x + world_origin.x, p.y + world_origin.y));
    }
    return path;
}

std::vector<std::pair<Vector2Int, bool>> overlap_along_line(const PolygonGrid2D& moving_polygon,
                                                           const PolygonGrid2D& fixed_polygon,
                                                           const OrthogonalLineGrid2D& line) {
    return overlap_along_line_polygon_partition(moving_polygon, fixed_polygon, line);
}

std::vector<OrthogonalLineGrid2D> remove_overlapping_along_lines(const PolygonGrid2D& moving_polygon,
                                                                  const PolygonGrid2D& fixed_polygon,
                                                                  const std::vector<OrthogonalLineGrid2D>& lines) {
    std::vector<OrthogonalLineGrid2D> non_overlapping;
    non_overlapping.reserve(lines.size());
    for (const auto& line : lines) {
        const auto overlap_events = overlap_along_line(moving_polygon, fixed_polygon, line);

        bool last_overlap = false;
        Vector2Int last_point = line.from;

        for (const auto& event : overlap_events) {
            const Vector2Int point = event.first;
            const bool overlap = event.second;

            if (overlap && !last_overlap) {
                const Vector2Int end_point = point - line.direction_vector();
                if (line.contains_point(end_point)) {
                    non_overlapping.emplace_back(last_point, end_point);
                }
            }

            last_overlap = overlap;
            last_point = point;
        }

        if (overlap_events.empty()) {
            non_overlapping.push_back(line);
        } else if (!last_overlap && last_point != line.to) {
            non_overlapping.emplace_back(last_point, line.to);
        }
    }
    return non_overlapping;
}

} // namespace edgar::geometry
