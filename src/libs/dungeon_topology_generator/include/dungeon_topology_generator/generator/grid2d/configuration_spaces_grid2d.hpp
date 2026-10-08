#pragma once

#include "dungeon_topology_generator/generator/grid2d/configuration_space_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/configuration_spaces_generator.hpp"
#include "dungeon_topology_generator/generator/grid2d/door_line_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/weighted_shape_grid2d.hpp"
#include "dungeon_topology_generator/geometry/overlap.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"

#include <optional>
#include <random>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d {

/// True if `offset` lies on any segment of the configuration space (door-aligned translations, fixed at origin).
bool offset_on_configuration_space(geometry::Vector2Int offset, const ConfigurationSpaceGrid2D& space);

/// Integer points on CS segments (subsampled on very long segments). Used for sampling placements.
std::vector<geometry::Vector2Int> enumerate_configuration_space_offsets(const ConfigurationSpaceGrid2D& space);

/// C# `GetMaximumIntersection` subset: positions for `moving` so doors match all placed `neighbors` and
/// `moving_index` does not overlap other placed rooms. `neighbor_doors[j]` = doors for `outlines[j]`.
/// Corridor rooms use the same regular configuration space as normal rooms: their doors are fixed
/// outline doors (matching validation and door extraction). `moving_is_corridor` /
/// `neighbor_is_corridor_by_index` are kept for signature stability and currently ignored.
std::optional<geometry::Vector2Int> sample_maximum_intersection_position(
    const geometry::PolygonGrid2D& moving, const std::vector<DoorLineGrid2D>& moving_doors,
    const std::vector<int>& neighbor_indices, int moving_index, const std::vector<geometry::PolygonGrid2D>& outlines,
    const std::vector<geometry::Vector2Int>& positions, const std::vector<std::vector<DoorLineGrid2D>>& neighbor_doors,
    const std::vector<bool>& placed, std::mt19937& rng, std::size_t max_point_checks = 80,
    bool moving_is_corridor = false, const std::vector<bool>* neighbor_is_corridor_by_index = nullptr);

/// Facade for configuration-space queries (C# `ConfigurationSpacesGrid2D` subset + door-based generator).
class ConfigurationSpacesGrid2D {
public:
    static bool compatible_non_overlapping(const geometry::PolygonGrid2D& a, geometry::Vector2Int pos_a,
                                           const geometry::PolygonGrid2D& b, geometry::Vector2Int pos_b) {
        return !geometry::polygons_overlap_area(a, pos_a, b, pos_b);
    }

    /// Door-aligned valid translations (uses `ConfigurationSpacesGenerator` + `remove_overlapping_along_lines`).
    static ConfigurationSpaceGrid2D configuration_space_between(const geometry::PolygonGrid2D& moving,
                                                                  const std::vector<DoorLineGrid2D>& moving_doors,
                                                                  const geometry::PolygonGrid2D& fixed,
                                                                  const std::vector<DoorLineGrid2D>& fixed_doors) {
        ConfigurationSpacesGenerator gen;
        return gen.get_configuration_space(moving, moving_doors, fixed, fixed_doors);
    }
};

} // namespace dungeon_topology_generator::generator::grid2d
