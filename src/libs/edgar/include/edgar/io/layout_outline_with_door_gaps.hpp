#pragma once

// Port of Edgar C# AbstractLayoutDrawer.GetOutline — wall segments vs door gaps on orthogonal polygons.

#include "edgar/geometry/orthogonal_line_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace edgar::io {

/// Builds a polyline with per-vertex flags: if `second` is true, draw segment from previous vertex to this one.
/// Coordinates are in the same space as `polygon` (typically room-local; add `position` for world).
/// `door_lines` is consumed (doors matched to edges are removed, as in C#).
inline std::vector<std::pair<geometry::Vector2Int, bool>> layout_outline_with_door_gaps(
    const geometry::PolygonGrid2D& polygon, std::vector<geometry::OrthogonalLineGrid2D> door_lines) {
    using geometry::OrthogonalLineGrid2D;
    using geometry::Vector2Int;

    std::vector<std::pair<Vector2Int, bool>> outline;

    auto add_to_outline = [&](Vector2Int point, bool draw_wall) {
        if (outline.empty()) {
            outline.emplace_back(point, draw_wall);
            return;
        }
        const auto& last = outline.back();
        if (!last.second && draw_wall && last.first == point) {
            return;
        }
        outline.emplace_back(point, draw_wall);
    };

    for (const auto& line : polygon.get_lines()) {
        add_to_outline(line.from, true);

        if (door_lines.empty()) {
            continue;
        }

        struct DoorOnEdge {
            OrthogonalLineGrid2D door{};
            int sort_key{};
        };
        std::vector<DoorOnEdge> door_distances;
        door_distances.reserve(door_lines.size());
        for (const auto& door : door_lines) {
            const int i_from = line.index_of_point(door.from);
            const int i_to = line.index_of_point(door.to);
            const int mn = std::min(i_from, i_to);
            door_distances.push_back({door, mn});
        }
        std::sort(door_distances.begin(), door_distances.end(),
                  [](const DoorOnEdge& a, const DoorOnEdge& b) { return a.sort_key < b.sort_key; });

        for (const auto& pair : door_distances) {
            if (pair.sort_key == -1) {
                continue;
            }
            OrthogonalLineGrid2D door_line = pair.door;
            if (line.index_of_point(door_line.from) != pair.sort_key) {
                door_line = door_line.switch_orientation();
            }

            const auto it = std::find_if(door_lines.begin(), door_lines.end(), [&](const OrthogonalLineGrid2D& d) {
                return (d.from == pair.door.from && d.to == pair.door.to) ||
                       (d.from == pair.door.to && d.to == pair.door.from);
            });
            if (it != door_lines.end()) {
                door_lines.erase(it);
            }

            add_to_outline(door_line.from, true);
            add_to_outline(door_line.to, false);
        }
    }

    return outline;
}

} // namespace edgar::io
