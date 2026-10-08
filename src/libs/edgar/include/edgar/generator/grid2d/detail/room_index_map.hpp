#pragma once

#include "edgar/generator/grid2d/level_description_mapping_grid2d.hpp"

namespace edgar::generator::grid2d::detail {

template <typename TRoom>
using RoomIndexMap = LevelDescriptionMappingGrid2D<TRoom>;

} // namespace edgar::generator::grid2d::detail
