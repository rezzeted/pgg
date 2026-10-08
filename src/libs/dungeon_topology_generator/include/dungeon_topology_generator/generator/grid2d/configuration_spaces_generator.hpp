#pragma once

#include "dungeon_topology_generator/generator/grid2d/configuration_space_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/door_line_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/room_template_grid2d.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/transformation_grid2d.hpp"

#include <utility>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d {

/// Port of C# `RoomTemplateInstanceGrid2D`: distinct (normalized outline + door lines) produced
/// by the allowed transformations of a template; symmetric transformations are deduplicated.
struct RoomTemplateInstanceGrid2D {
    geometry::PolygonGrid2D outline;
    std::vector<DoorLineGrid2D> door_lines;
    std::vector<geometry::TransformationGrid2D> transformations;
};

/// Clears the process-wide content-addressed configuration space cache (memory is bounded by
/// an internal cap; entries never go stale because keys carry full geometry).
void clear_configuration_space_cache();

/// Port of C# `ConfigurationSpacesGenerator` (doors, `RemoveOverlapping`, `RemoveIntersections`, corridors).
class ConfigurationSpacesGenerator {
public:    /// C# `GetRoomTemplateInstances(RoomTemplateGrid2D)`: transform outline and door lines for
    /// every allowed transformation, shift to the origin (first quadrant) and normalize; instances
    /// with equal outline and equal (unordered) door lines are merged.
    std::vector<RoomTemplateInstanceGrid2D> get_room_template_instances(const RoomTemplateGrid2D& room_template);

    ConfigurationSpaceGrid2D get_configuration_space(const geometry::PolygonGrid2D& polygon,
                                                     const std::vector<DoorLineGrid2D>& door_lines,
                                                     const geometry::PolygonGrid2D& fixed_center,
                                                     const std::vector<DoorLineGrid2D>& door_lines_fixed,
                                                     const std::vector<int>* offsets = nullptr);

    /// C# `GetConfigurationSpaceOverCorridor` — CS for `polygon` vs fixed room using corridor as movable door carrier.
    ConfigurationSpaceGrid2D get_configuration_space_over_corridor(
        const geometry::PolygonGrid2D& polygon, const std::vector<DoorLineGrid2D>& door_lines,
        const geometry::PolygonGrid2D& fixed_polygon, const std::vector<DoorLineGrid2D>& fixed_door_lines,
        const geometry::PolygonGrid2D& corridor, const std::vector<DoorLineGrid2D>& corridor_door_lines);

    /// C# `GetConfigurationSpaceOverCorridors` — union of per-corridor CS lines, then `RemoveIntersections`.
    ConfigurationSpaceGrid2D get_configuration_space_over_corridors(
        const geometry::PolygonGrid2D& polygon, const std::vector<DoorLineGrid2D>& door_lines,
        const geometry::PolygonGrid2D& fixed_polygon, const std::vector<DoorLineGrid2D>& fixed_door_lines,
        const std::vector<std::pair<geometry::PolygonGrid2D, std::vector<DoorLineGrid2D>>>& corridors);
};

} // namespace dungeon_topology_generator::generator::grid2d
