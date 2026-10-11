#pragma once

// V1 Terrain: deterministic terrain input (docs/village_topology_generator/requirements.md §7).
// Heightfield + water/forest masks; metric conventions per §5.

#include <cstdint>
#include <vector>

namespace village_topology_generator {

struct TerrainParams {
    int width = 256;                    // cells
    int height = 256;
    double cell_size = 2.0;             // meters per cell
    double water_level = 8.0;           // meters; height below this is water
    uint64_t seed = 1;
    int octaves = 4;
    double frequency = 1.0 / 48.0;      // base lattice density, cycles per cell
    double amplitude = 20.0;            // meters, total relief height
    double forest_threshold = 0.62;     // forest noise above this (and not water) is forest
};

// Row-major grids, y down; cell centers at (x + 0.5) * cell_size meters.
struct Terrain {
    int width = 0;
    int height = 0;
    double cell_size = 1.0;
    double water_level = 0.0;
    std::vector<double> heights;  // meters, width * height
    std::vector<uint8_t> forest;  // 0/1, width * height

    bool in_bounds(int x, int y) const;
    double height_at(int x, int y) const;
    bool is_water(int x, int y) const;
    bool is_forest(int x, int y) const;
};

// Deterministic (N1): same seed and params -> identical terrain.
Terrain generate_terrain(const TerrainParams& params);

}  // namespace village_topology_generator
