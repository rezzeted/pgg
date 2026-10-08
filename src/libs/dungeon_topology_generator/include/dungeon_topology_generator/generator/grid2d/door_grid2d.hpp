#pragma once

#include <memory>

#include "dungeon_topology_generator/geometry/vector2_int.hpp"

namespace dungeon_topology_generator::generator::grid2d {

struct DoorGrid2D {
    geometry::Vector2Int from{};
    geometry::Vector2Int to{};
    std::shared_ptr<const void> socket{};
};

} // namespace dungeon_topology_generator::generator::grid2d
