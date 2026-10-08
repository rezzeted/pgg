#pragma once

#include <vector>

#include "edgar/generator/grid2d/door_grid2d.hpp"
#include "edgar/generator/grid2d/door_mode_grid2d.hpp"

namespace edgar::generator::grid2d {

class ManualDoorModeGrid2D : public IDoorModeGrid2D {
public:
    explicit ManualDoorModeGrid2D(std::vector<DoorGrid2D> doors) : doors_(std::move(doors)) {}

    std::vector<DoorLineGrid2D> get_doors(const geometry::PolygonGrid2D& room_shape) const override;

    const std::vector<DoorGrid2D>& doors() const { return doors_; }

private:
    std::vector<DoorGrid2D> doors_;
};

} // namespace edgar::generator::grid2d
