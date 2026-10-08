#pragma once

#include <memory>

#include "edgar/geometry/vector2_int.hpp"

namespace edgar::generator::grid2d {

struct DoorGrid2D {
    geometry::Vector2Int from{};
    geometry::Vector2Int to{};
    std::shared_ptr<const void> socket{};
};

} // namespace edgar::generator::grid2d
