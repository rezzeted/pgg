#pragma once

#include "edgar/geometry/orthogonal_line_grid2d.hpp"

namespace edgar::generator::grid2d {

template <typename TRoom>
struct LayoutDoorGrid2D {
    TRoom from_room{};
    TRoom to_room{};
    geometry::OrthogonalLineGrid2D door_line{};
};

} // namespace edgar::generator::grid2d
