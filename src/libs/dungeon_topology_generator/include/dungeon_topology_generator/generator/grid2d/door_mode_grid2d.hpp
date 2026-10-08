#pragma once

#include <vector>

#include "dungeon_topology_generator/generator/grid2d/door_line_grid2d.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"

namespace dungeon_topology_generator::generator::grid2d {

class IDoorModeGrid2D {
public:
    virtual ~IDoorModeGrid2D() = default;
    virtual std::vector<DoorLineGrid2D> get_doors(const geometry::PolygonGrid2D& room_shape) const = 0;
};

} // namespace dungeon_topology_generator::generator::grid2d
