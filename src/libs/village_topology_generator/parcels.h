#pragma once

// V5 Parcels: burgage lots along road frontages
// (docs/village_topology_generator/requirements.md §7). Narrow front to the
// road, deep backyard; growth stops at water, steep slope, other roads and
// already-claimed lots.

#include <utility>
#include <vector>

#include "analysis.h"
#include "roads.h"

namespace village_topology_generator {

struct ParcelParams {
    double front_width = 10.0;  // meters of road frontage per lot
    double max_depth = 24.0;    // meters perpendicular from the road
    double min_depth = 8.0;     // lots shallower than this are discarded
    double max_slope = 0.5;     // buildable gradient limit (rise/run)
};

// One burgage lot. front: first row of cells, adjacent to the road (same
// order as the road path). cells: all lot cells, front row first.
struct Parcel {
    int road_index = -1;  // index into RoadNetwork::roads
    int side = 0;         // +1 = left of travel direction, -1 = right
    std::vector<std::pair<int, int>> front;
    std::vector<std::pair<int, int>> cells;
};

// Grow lots along both sides of every road, in network order. Fronts are
// grouped along the path into front_width chunks (a bend or an unbuildable
// gap flushes the current front; a leftover front shorter than half the
// width is dropped). Depth is ray-marched perpendicular to the road and
// truncated at the shallowest front cell, keeping every lot a rectangle.
std::vector<Parcel> grow_parcels(const Analysis& analysis,
                                 const RoadNetwork& network,
                                 const ParcelParams& params = {});

}  // namespace village_topology_generator
