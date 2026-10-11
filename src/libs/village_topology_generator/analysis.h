#pragma once

// V2 Analysis: slope map, distance fields, road cost field, settle
// attractiveness (docs/village_topology_generator/requirements.md §7).

#include <vector>

#include "terrain.h"

namespace village_topology_generator {

struct AnalysisParams {
    double slope_cost_factor = 6.0;  // added road cost per unit of gradient (rise/run)
    double forest_cost = 0.5;        // added road cost inside forest
    double max_build_slope = 0.5;    // gradient above this is unbuildable (attractiveness 0)
    double water_far = 60.0;         // meters; water proximity bonus fades to 0 here
    double forest_far = 40.0;        // meters; forest proximity bonus fades to 0 here
};

// Per-cell maps, same row-major indexing as Terrain.
struct Analysis {
    int width = 0;
    int height = 0;
    double cell_size = 1.0;
    std::vector<double> slope;          // gradient magnitude, rise over run
    std::vector<double> cost;           // road cost field; inf = impassable (water)
    std::vector<double> water_dist;     // meters to nearest water cell (0 on water, inf if none)
    std::vector<double> forest_dist;    // meters to nearest forest cell (0 on forest, inf if none)
    std::vector<double> attractiveness; // ~0..1; 0 on water and unbuildable slope

    bool in_bounds(int x, int y) const;
    double slope_at(int x, int y) const;
    double cost_at(int x, int y) const;
    double water_dist_at(int x, int y) const;
    double forest_dist_at(int x, int y) const;
    double attractiveness_at(int x, int y) const;
};

Analysis analyze_terrain(const Terrain& terrain, const AnalysisParams& params = {});

}  // namespace village_topology_generator
