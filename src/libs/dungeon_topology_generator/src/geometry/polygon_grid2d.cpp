#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace dungeon_topology_generator::geometry {

namespace {

RectangleGrid2D make_bbox(const std::vector<Vector2Int>& pts) {
    int sx = pts[0].x;
    int bx = pts[0].x;
    int sy = pts[0].y;
    int by = pts[0].y;
    for (const auto& p : pts) {
        sx = std::min(sx, p.x);
        bx = std::max(bx, p.x);
        sy = std::min(sy, p.y);
        by = std::max(by, p.y);
    }
    return RectangleGrid2D({sx, sy}, {bx, by});
}

} // namespace

PolygonGrid2D::PolygonGrid2D(std::vector<Vector2Int> points) : points_(std::move(points)) {
    check_integrity();
    hash_ = compute_hash(points_);
    bounding_ = make_bbox(points_);
}

void PolygonGrid2D::check_integrity() const {
    const auto& points = points_;
    if (points.size() < 4) {
        throw std::invalid_argument("Each polygon must have at least 4 points.");
    }
    Vector2Int previous = points.back();
    for (const auto& point : points) {
        if (point == previous) {
            throw std::invalid_argument("All lines must be parallel to one of the axes.");
        }
        if (point.x != previous.x && point.y != previous.y) {
            throw std::invalid_argument("All lines must be parallel to one of the axes.");
        }
        previous = point;
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[(i + 1) % points.size()];
        const auto& p3 = points[(i + 2) % points.size()];
        if (p1.x == p2.x && p2.x == p3.x) {
            throw std::invalid_argument("No two adjacent lines can be both horizontal or vertical.");
        }
        if (p1.y == p2.y && p2.y == p3.y) {
            throw std::invalid_argument("No two adjacent lines can be both horizontal or vertical.");
        }
    }
    if (!is_clockwise_oriented(points)) {
        throw std::invalid_argument("Points must be in a clockwise order.");
    }
}

bool PolygonGrid2D::is_clockwise_oriented(const std::vector<Vector2Int>& points) {
    Vector2Int previous = points.back();
    long long sum = 0;
    for (const auto& point : points) {
        sum += static_cast<long long>(point.x - previous.x) * (static_cast<long long>(point.y + previous.y));
        previous = point;
    }
    return sum > 0;
}

int PolygonGrid2D::compute_hash(const std::vector<Vector2Int>& pts) {
    int hash = 17;
    for (const auto& x : pts) {
        hash = hash * 23 + x.x + x.y;
    }
    return hash;
}

std::vector<OrthogonalLineGrid2D> PolygonGrid2D::get_lines() const {
    std::vector<OrthogonalLineGrid2D> lines;
    Vector2Int x1 = points_.back();
    for (const auto& point : points_) {
        const Vector2Int x2 = x1;
        x1 = point;
        lines.emplace_back(x2, x1);
    }
    return lines;
}

PolygonGrid2D PolygonGrid2D::scale(Vector2Int factor) const {
    if (factor.x <= 0 || factor.y <= 0) {
        throw std::out_of_range("Both components of factor must be positive");
    }
    std::vector<Vector2Int> out;
    out.reserve(points_.size());
    for (const auto& p : points_) {
        out.push_back(p.element_wise_product(factor));
    }
    return PolygonGrid2D(std::move(out));
}

PolygonGrid2D PolygonGrid2D::rotate(int degrees) const {
    if (degrees % 90 != 0) {
        throw std::invalid_argument("Degrees must be divisible by 90");
    }
    std::vector<Vector2Int> out;
    out.reserve(points_.size());
    for (const auto& p : points_) {
        out.push_back(p.rotate_around_center(degrees));
    }
    return PolygonGrid2D(std::move(out));
}

std::vector<PolygonGrid2D> PolygonGrid2D::get_all_rotations() const {
    std::vector<PolygonGrid2D> r;
    for (int d : possible_rotations) {
        r.push_back(rotate(d));
    }
    return r;
}

PolygonGrid2D PolygonGrid2D::transform(TransformationGrid2D transformation) const {
    std::vector<Vector2Int> new_points;
    new_points.reserve(points_.size());
    for (const auto& p : points_) {
        new_points.push_back(p.transform(transformation));
    }
    const bool reverse =
        transformation == TransformationGrid2D::MirrorX || transformation == TransformationGrid2D::MirrorY ||
        transformation == TransformationGrid2D::Diagonal13 || transformation == TransformationGrid2D::Diagonal24;
    if (reverse && new_points.size() >= 2) {
        const Vector2Int keep_first = new_points.front();
        std::reverse(new_points.begin() + 1, new_points.end());
        std::vector<Vector2Int> reordered;
        reordered.push_back(keep_first);
        reordered.insert(reordered.end(), new_points.begin() + 1, new_points.end());
        new_points = std::move(reordered);
    }
    return PolygonGrid2D(std::move(new_points));
}

std::vector<PolygonGrid2D> PolygonGrid2D::get_all_transformations() const {
    std::vector<PolygonGrid2D> out;
    static constexpr std::array<TransformationGrid2D, 8> vals = {
        TransformationGrid2D::Identity,
        TransformationGrid2D::Rotate90,
        TransformationGrid2D::Rotate180,
        TransformationGrid2D::Rotate270,
        TransformationGrid2D::MirrorX,
        TransformationGrid2D::MirrorY,
        TransformationGrid2D::Diagonal13,
        TransformationGrid2D::Diagonal24,
    };
    for (auto t : vals) {
        out.push_back(transform(t));
    }
    return out;
}

PolygonGrid2D PolygonGrid2D::normalized() const {
    const auto& pts = points_;
    std::size_t start = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].x < pts[start].x || (pts[i].x == pts[start].x && pts[i].y < pts[start].y)) {
            start = i;
        }
    }
    std::vector<Vector2Int> out;
    out.reserve(pts.size());
    for (std::size_t k = 0; k < pts.size(); ++k) {
        out.push_back(pts[(start + k) % pts.size()]);
    }
    return PolygonGrid2D(std::move(out));
}

PolygonGrid2D operator+(const PolygonGrid2D& polygon, Vector2Int position) {
    const auto& src = polygon.points();
    std::vector<Vector2Int> out;
    out.reserve(src.size());
    for (const auto& p : src) {
        out.push_back(p + position);
    }
    return PolygonGrid2D(std::move(out));
}

PolygonGrid2D PolygonGrid2D::get_rectangle(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::out_of_range("width and height must be greater than 0");
    }
    return PolygonGrid2DBuilder()
        .add_point(0, 0)
        .add_point(0, height)
        .add_point(width, height)
        .add_point(width, 0)
        .build();
}

bool PolygonGrid2D::operator==(const PolygonGrid2D& o) const {
    if (points_.size() != o.points_.size()) {
        return false;
    }
    return points_ == o.points_;
}

std::string PolygonGrid2D::to_string() const {
    std::ostringstream oss;
    oss << "Polygon: (";
    for (std::size_t i = 0; i < points_.size(); ++i) {
        if (i) {
            oss << ',';
        }
        oss << '[' << points_[i].x << ',' << points_[i].y << ']';
    }
    oss << ')';
    return oss.str();
}

} // namespace dungeon_topology_generator::geometry
