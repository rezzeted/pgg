#include "edgar/geometry/orthogonal_line_intersection.hpp"

#include <algorithm>
#include <stdexcept>

namespace edgar::geometry {

bool OrthogonalLineIntersection::try_get_intersection(const OrthogonalLineGrid2D& line1,
                                                      const OrthogonalLineGrid2D& line2,
                                                      OrthogonalLineGrid2D& intersection_out) {
    const bool horizontal1 = line1.from.y == line1.to.y;
    const bool horizontal2 = line2.from.y == line2.to.y;

    const OrthogonalLineGrid2D n1 = line1.normalized();
    const OrthogonalLineGrid2D n2 = line2.normalized();

    if (horizontal1 && horizontal2) {
        if (n1.from.y != n2.from.y) {
            return false;
        }
        const int x1 = std::max(n1.from.x, n2.from.x);
        const int x2 = std::min(n1.to.x, n2.to.x);
        if (x1 <= x2) {
            intersection_out = OrthogonalLineGrid2D({x1, n1.from.y}, {x2, n1.from.y});
            return true;
        }
        return false;
    }

    if (!horizontal1 && !horizontal2) {
        if (n1.from.x != n2.from.x) {
            return false;
        }
        const int y1 = std::max(n1.from.y, n2.from.y);
        const int y2 = std::min(n1.to.y, n2.to.y);
        if (y1 <= y2) {
            intersection_out = OrthogonalLineGrid2D({n1.from.x, y1}, {n1.from.x, y2});
            return true;
        }
        return false;
    }

    const OrthogonalLineGrid2D& hline = horizontal1 ? n1 : n2;
    const OrthogonalLineGrid2D& vline = horizontal1 ? n2 : n1;

    if (hline.from.x <= vline.from.x && hline.to.x >= vline.from.x && vline.from.y <= hline.from.y &&
        vline.to.y >= hline.from.y) {
        intersection_out = OrthogonalLineGrid2D({vline.from.x, hline.from.y}, {vline.from.x, hline.from.y});
        return true;
    }
    return false;
}

std::vector<OrthogonalLineGrid2D> OrthogonalLineIntersection::get_intersections(
    const std::vector<OrthogonalLineGrid2D>& lines1, const std::vector<OrthogonalLineGrid2D>& lines2) {
    std::vector<OrthogonalLineGrid2D> out;
    for (const auto& line1 : lines1) {
        for (const auto& line2 : lines2) {
            OrthogonalLineGrid2D inter;
            if (try_get_intersection(line1, line2, inter)) {
                out.push_back(inter);
            }
        }
    }
    return out;
}

std::vector<OrthogonalLineGrid2D> OrthogonalLineIntersection::remove_intersections(
    const std::vector<OrthogonalLineGrid2D>& lines) {
    std::vector<OrthogonalLineGrid2D> lines_without_intersections;
    for (const auto& line : lines) {
        const auto intersection = get_intersections({line}, lines_without_intersections);
        if (intersection.empty()) {
            lines_without_intersections.push_back(line);
        } else {
            auto parts = partition_by_intersection(line, intersection);
            lines_without_intersections.insert(lines_without_intersections.end(), parts.begin(), parts.end());
        }
    }
    return lines_without_intersections;
}

std::vector<OrthogonalLineGrid2D> OrthogonalLineIntersection::partition_by_intersection(
    const OrthogonalLineGrid2D& line, const std::vector<OrthogonalLineGrid2D>& intersection) {
    const int rotation = line.compute_rotation_clockwise_degrees();
    const OrthogonalLineGrid2D rotated_line = line.rotate(rotation, true);
    const Vector2Int direction_vector = rotated_line.direction_vector();

    std::vector<OrthogonalLineGrid2D> rotated_intersection;
    rotated_intersection.reserve(intersection.size());
    for (const auto& x : intersection) {
        rotated_intersection.push_back(x.rotate(rotation).normalized());
    }

    std::sort(rotated_intersection.begin(), rotated_intersection.end(),
              [](const OrthogonalLineGrid2D& a, const OrthogonalLineGrid2D& b) {
                  if (a.from.x != b.from.x) {
                      return a.from.x < b.from.x;
                  }
                  return a.from.y < b.from.y;
              });

    std::vector<OrthogonalLineGrid2D> result;
    Vector2Int last_point = rotated_line.from - direction_vector;

    for (std::size_t i = 0; i < rotated_intersection.size(); ++i) {
        const OrthogonalLineGrid2D& intersection_line = rotated_intersection[i];

        if (intersection_line.from.x < rotated_line.from.x || intersection_line.from.x > rotated_line.to.x) {
            throw std::invalid_argument("partition_by_intersection: intersection not on line");
        }
        if (intersection_line.from.y != rotated_line.from.y || intersection_line.to.y != rotated_line.from.y) {
            throw std::invalid_argument("partition_by_intersection: intersection not collinear");
        }
        if (i + 1 < rotated_intersection.size()) {
            const OrthogonalLineGrid2D& next_line = rotated_intersection[i + 1];
            if (next_line.from.x <= intersection_line.to.x) {
                throw std::invalid_argument("partition_by_intersection: overlapping intersections");
            }
        }

        const bool last_ok = (intersection_line.from != last_point) && (intersection_line.from - direction_vector != last_point);
        if (last_ok) {
            result.emplace_back(last_point + direction_vector, intersection_line.from - direction_vector);
        }
        last_point = intersection_line.to;
    }

    if (rotated_line.to != last_point && rotated_line.to - direction_vector != last_point) {
        result.emplace_back(last_point + direction_vector, rotated_line.to);
    }

    std::vector<OrthogonalLineGrid2D> out;
    out.reserve(result.size());
    for (const auto& seg : result) {
        out.push_back(seg.rotate(-rotation, false));
    }
    return out;
}

} // namespace edgar::geometry
