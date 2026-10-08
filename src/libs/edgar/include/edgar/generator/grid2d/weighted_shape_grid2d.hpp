#pragma once

#include "edgar/generator/grid2d/room_template_grid2d.hpp"

namespace edgar::generator::grid2d {

/// C# `WeightedShape` — room template with selection weight for configuration-space caching.
struct WeightedShapeGrid2D {
    RoomTemplateGrid2D room_template;
    double weight = 1.0;
};

} // namespace edgar::generator::grid2d
