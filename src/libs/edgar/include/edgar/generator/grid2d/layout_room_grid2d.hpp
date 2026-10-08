#pragma once

#include <optional>
#include <vector>

#include "edgar/generator/grid2d/layout_door_grid2d.hpp"
#include "edgar/generator/grid2d/room_description_grid2d.hpp"
#include "edgar/generator/grid2d/room_template_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

namespace edgar::generator::grid2d {

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

} // namespace edgar::generator::grid2d
