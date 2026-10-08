#pragma once

#include <optional>
#include <vector>

#include "dungeon_topology_generator/generator/grid2d/layout_door_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/room_description_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/room_template_grid2d.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/transformation_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"

namespace dungeon_topology_generator::generator::grid2d {

template <typename TRoom>
struct LayoutRoomGrid2D {
    TRoom room{};
    geometry::PolygonGrid2D outline;
    geometry::Vector2Int position{};
    bool is_corridor{};
    RoomTemplateGrid2D room_template;
    std::optional<RoomDescriptionGrid2D> room_description;
    geometry::TransformationGrid2D transformation{geometry::TransformationGrid2D::Identity};
    std::vector<LayoutDoorGrid2D<TRoom>> doors;
};

} // namespace dungeon_topology_generator::generator::grid2d
