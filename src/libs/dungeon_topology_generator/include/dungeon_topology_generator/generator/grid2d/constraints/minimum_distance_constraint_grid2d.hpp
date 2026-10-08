#pragma once

#include "dungeon_topology_generator/generator/common/energy_data.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/rectangle_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"
#include "dungeon_topology_generator/graphs/undirected_graph.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d::constraints {

struct MinimumDistanceConstraintGrid2D {
    /// Graph neighbours (rooms sharing a passage) are exempt: door-connected
    /// rooms must touch, so the minimum distance applies to non-neighbours only.
    /// A null graph keeps the legacy behaviour (penalize every pair).
    static common::EnergyData evaluate_pair(std::size_t i, std::size_t j,
                                            const std::vector<geometry::PolygonGrid2D>& outlines,
                                            const std::vector<geometry::Vector2Int>& positions,
                                            int minimum_room_distance,
                                            const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        if (minimum_room_distance <= 0) {
            return out;
        }
        if (graph != nullptr &&
            graph->has_edge(static_cast<int>(i), static_cast<int>(j))) {
            return out;
        }

        const geometry::RectangleGrid2D ra = outlines[i].bounding_rectangle() + positions[i];
        const geometry::RectangleGrid2D rb = outlines[j].bounding_rectangle() + positions[j];
        const int d = axis_aligned_rect_min_distance(ra, rb);
        if (d < minimum_room_distance) {
            out.minimum_distance_penalty =
                static_cast<double>(minimum_room_distance - d) / static_cast<double>(minimum_room_distance);
        }

        return out;
    }

private:
    static int axis_aligned_rect_min_distance(const geometry::RectangleGrid2D& ra,
                                              const geometry::RectangleGrid2D& rb) {
        const int dx = std::max(0, std::max(ra.a.x - rb.b.x, rb.a.x - ra.b.x));
        const int dy = std::max(0, std::max(ra.a.y - rb.b.y, rb.a.y - ra.b.y));
        return dx + dy;
    }
};

} // namespace dungeon_topology_generator::generator::grid2d::constraints
