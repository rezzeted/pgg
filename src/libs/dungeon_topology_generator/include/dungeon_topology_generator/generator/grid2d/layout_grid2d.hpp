#pragma once

#include <vector>

#include "dungeon_topology_generator/generator/grid2d/layout_room_grid2d.hpp"

namespace dungeon_topology_generator::generator::grid2d {

template <typename TRoom>
struct LayoutGrid2D {
    std::vector<LayoutRoomGrid2D<TRoom>> rooms;
};

} // namespace dungeon_topology_generator::generator::grid2d
