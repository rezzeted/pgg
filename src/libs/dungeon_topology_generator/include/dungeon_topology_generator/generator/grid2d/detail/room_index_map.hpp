#pragma once

#include "dungeon_topology_generator/generator/grid2d/level_description_mapping_grid2d.hpp"

namespace dungeon_topology_generator::generator::grid2d::detail {

template <typename TRoom>
using RoomIndexMap = LevelDescriptionMappingGrid2D<TRoom>;

} // namespace dungeon_topology_generator::generator::grid2d::detail
