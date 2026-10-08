#pragma once

#include "dungeon_topology_generator/generator/grid2d/room_template_grid2d.hpp"

namespace dungeon_topology_generator::generator::grid2d {

/// C# `WeightedShape` — room template with selection weight for configuration-space caching.
struct WeightedShapeGrid2D {
    RoomTemplateGrid2D room_template;
    double weight = 1.0;
};

} // namespace dungeon_topology_generator::generator::grid2d
