#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dungeon_topology_generator/geometry/orthogonal_line_grid2d.hpp"
#include "dungeon_topology_generator/geometry/rectangle_grid2d.hpp"
#include "dungeon_topology_generator/geometry/transformation_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"

namespace dungeon_topology_generator::geometry {

class PolygonGrid2D {
public:
    static constexpr std::array<int, 4> possible_rotations = {0, 90, 180, 270};

    PolygonGrid2D() = delete;
    explicit PolygonGrid2D(std::vector<Vector2Int> points);

    const std::vector<Vector2Int>& points() const { return points_; }

    const RectangleGrid2D& bounding_rectangle() const { return bounding_; }

    std::vector<OrthogonalLineGrid2D> get_lines() const;

    PolygonGrid2D scale(Vector2Int factor) const;
    PolygonGrid2D rotate(int degrees) const;
    std::vector<PolygonGrid2D> get_all_rotations() const;

    PolygonGrid2D transform(TransformationGrid2D transformation) const;
    std::vector<PolygonGrid2D> get_all_transformations() const;

    /// C# `GridPolygonUtils.NormalizePolygon`: rotate point order so the smallest-X
    /// (then smallest-Y) point comes first. Geometry unchanged.
    PolygonGrid2D normalized() const;

    friend PolygonGrid2D operator+(const PolygonGrid2D& polygon, Vector2Int position);

    static PolygonGrid2D get_square(int a) { return get_rectangle(a, a); }
    static PolygonGrid2D get_rectangle(int width, int height);

    bool operator==(const PolygonGrid2D& o) const;
    bool operator!=(const PolygonGrid2D& o) const { return !(*this == o); }

    std::size_t hash() const { return static_cast<std::size_t>(hash_); }

    std::string to_string() const;

private:
    std::vector<Vector2Int> points_;
    RectangleGrid2D bounding_;
    int hash_{};

    void check_integrity() const;
    static bool is_clockwise_oriented(const std::vector<Vector2Int>& pts);
    static int compute_hash(const std::vector<Vector2Int>& pts);
};

class PolygonGrid2DBuilder {
public:
    PolygonGrid2DBuilder& add_point(int x, int y) {
        pts_.push_back({x, y});
        return *this;
    }

    PolygonGrid2D build() const { return PolygonGrid2D(pts_); }

private:
    std::vector<Vector2Int> pts_;
};

} // namespace dungeon_topology_generator::geometry

namespace std {

template <>
struct hash<dungeon_topology_generator::geometry::PolygonGrid2D> {
    std::size_t operator()(const dungeon_topology_generator::geometry::PolygonGrid2D& p) const { return p.hash(); }
};

} // namespace std
