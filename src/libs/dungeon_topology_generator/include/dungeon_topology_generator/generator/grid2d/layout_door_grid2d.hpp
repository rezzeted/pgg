#pragma once

#include "dungeon_topology_generator/geometry/orthogonal_line_grid2d.hpp"

namespace dungeon_topology_generator::generator::grid2d {

template <typename TRoom>
struct LayoutDoorGrid2D {
    TRoom from_room{};
    TRoom to_room{};
    geometry::OrthogonalLineGrid2D door_line{};
};

} // namespace dungeon_topology_generator::generator::grid2d
