#pragma once

#include <memory>

#include "edgar/geometry/orthogonal_line_grid2d.hpp"

namespace edgar::generator::grid2d {

struct DoorLineGrid2D {
    geometry::OrthogonalLineGrid2D line{};
    int length{};
    geometry::OrthogonalDirection direction{geometry::OrthogonalDirection::Undefined};
    std::shared_ptr<const void> socket{};

    geometry::OrthogonalDirection get_direction() const {
        return direction != geometry::OrthogonalDirection::Undefined ? direction : line.get_direction();
    }
};

} // namespace edgar::generator::grid2d
