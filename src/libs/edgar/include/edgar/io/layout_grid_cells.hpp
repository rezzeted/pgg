#pragma once

// Grid cell lattice points for DungeonDrawer-style DrawGrid (Edgar C# DungeonDrawerBase).

#include "edgar/geometry/grid_polygon_partitioning.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace edgar::io {

/// Point-in-polygon for integer grid points (same ray-cast as dungeon_drawer detail).
inline bool point_in_polygon_xy(geometry::Vector2Int pt, const std::vector<geometry::Vector2Int>& poly) {
    bool c = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& pi = poly[i];
        const auto& pj = poly[j];
        if (pj.y == pi.y) {
            continue;
        }
        if (((pi.y > pt.y) != (pj.y > pt.y)) &&
            (static_cast<double>(pt.x) <
             static_cast<double>(pj.x - pi.x) * static_cast<double>(pt.y - pi.y) / static_cast<double>(pj.y - pi.y) +
                 static_cast<double>(pi.x))) {
            c = !c;
        }
    }
    return c;
}


/// All integer grid points covered by the orthogonal polygon partition (same loops as C# DrawGrid).
inline std::vector<geometry::Vector2Int> grid_cell_lattice_points(const geometry::PolygonGrid2D& polygon) {
    const std::vector<geometry::RectangleGrid2D> rects = geometry::partition_orthogonal_polygon_to_rectangles(polygon);
    std::unordered_set<std::int64_t> seen;
    std::vector<geometry::Vector2Int> out;
    seen.reserve(rects.size() * 16 + 8);

    auto pack = [](geometry::Vector2Int p) -> std::int64_t {
        return (static_cast<std::int64_t>(p.x) << 32) ^ (static_cast<std::uint32_t>(p.y));
    };

    for (const auto& rect : rects) {
        for (int i = rect.a.x; i <= rect.b.x; ++i) {
            for (int j = rect.a.y; j <= rect.b.y; ++j) {
                const geometry::Vector2Int p{i, j};
                if (seen.insert(pack(p)).second) {
                    out.push_back(p);
                }
            }
        }
    }
    return out;
}

/// Build a lookup set for neighbor tests (DrawGrid: edges to +X and to -Y like C#).
inline std::unordered_set<std::int64_t> grid_cell_lattice_point_set(const std::vector<geometry::Vector2Int>& points) {
    std::unordered_set<std::int64_t> set;
    set.reserve(points.size() * 2);
    auto pack = [](geometry::Vector2Int p) -> std::int64_t {
        return (static_cast<std::int64_t>(p.x) << 32) ^ (static_cast<std::uint32_t>(p.y));
    };
    for (const auto& p : points) {
        set.insert(pack(p));
    }
    return set;
}

inline bool lattice_set_contains(const std::unordered_set<std::int64_t>& set, geometry::Vector2Int p) {
    const std::int64_t k =
        (static_cast<std::int64_t>(p.x) << 32) ^ (static_cast<std::uint32_t>(p.y));
    return set.find(k) != set.end();
}

} // namespace edgar::io
