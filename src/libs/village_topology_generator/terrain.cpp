#include "pch.h"

#include "terrain.h"

namespace village_topology_generator {
namespace {

// Lattice hash -> [0, 1). Integer mixing only, platform-independent (N1).
double lattice01(int x, int y, uint64_t seed) {
    uint64_t h = seed * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<uint64_t>(static_cast<int32_t>(x)) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    h ^= static_cast<uint64_t>(static_cast<int32_t>(y)) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 32;
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);  // 2^53
}

double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

double value_noise(double x, double y, uint64_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double tx = smoothstep(x - x0);
    const double ty = smoothstep(y - y0);
    const double v00 = lattice01(x0, y0, seed);
    const double v10 = lattice01(x0 + 1, y0, seed);
    const double v01 = lattice01(x0, y0 + 1, seed);
    const double v11 = lattice01(x0 + 1, y0 + 1, seed);
    const double top = v00 + (v10 - v00) * tx;
    const double bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

// fBm in [0, 1): octaves of value noise, amplitude-normalized. Each octave
// gets its own decorrelated seed stream.
double fbm(double x, double y, int octaves, uint64_t seed) {
    double sum = 0.0, amp = 0.5, norm = 0.0, freq = 1.0;
    for (int i = 0; i < octaves; ++i) {
        const uint64_t octave_seed = seed + static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull;
        sum += value_noise(x * freq, y * freq, octave_seed) * amp;
        norm += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

}  // namespace

bool Terrain::in_bounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}

double Terrain::height_at(int x, int y) const {
    return heights[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

bool Terrain::is_water(int x, int y) const { return height_at(x, y) < water_level; }

bool Terrain::is_forest(int x, int y) const {
    return forest[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] != 0;
}

Terrain generate_terrain(const TerrainParams& params) {
    Terrain t;
    t.width = params.width;
    t.height = params.height;
    t.cell_size = params.cell_size;
    t.water_level = params.water_level;
    t.heights.resize(static_cast<size_t>(params.width) * static_cast<size_t>(params.height));
    t.forest.resize(t.heights.size());
    const uint64_t forest_seed = params.seed ^ 0x5F356495B5C8D3B2ull;
    for (int y = 0; y < params.height; ++y) {
        for (int x = 0; x < params.width; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(params.width) + static_cast<size_t>(x);
            const double nx = static_cast<double>(x) * params.frequency;
            const double ny = static_cast<double>(y) * params.frequency;
            t.heights[i] = fbm(nx, ny, params.octaves, params.seed) * params.amplitude;
            const double fn = fbm(nx, ny, params.octaves, forest_seed);
            t.forest[i] = (fn > params.forest_threshold && t.heights[i] >= params.water_level) ? 1 : 0;
        }
    }
    return t;
}

}  // namespace village_topology_generator
