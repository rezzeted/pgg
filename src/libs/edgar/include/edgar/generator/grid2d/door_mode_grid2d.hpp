#pragma once

#include <vector>

#include "edgar/generator/grid2d/door_line_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"

namespace edgar::generator::grid2d {

class IDoorModeGrid2D {
public:
    virtual ~IDoorModeGrid2D() = default;
    virtual std::vector<DoorLineGrid2D> get_doors(const geometry::PolygonGrid2D& room_shape) const = 0;
};

} // namespace edgar::generator::grid2d
