#pragma once

#include <vector>

#include "edgar/generator/grid2d/layout_room_grid2d.hpp"

namespace edgar::generator::grid2d {

template <typename TRoom>
struct LayoutGrid2D {
    std::vector<LayoutRoomGrid2D<TRoom>> rooms;
};

} // namespace edgar::generator::grid2d
