#include "pch.h"

#include "analysis.h"

namespace village_topology_generator {
namespace {

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Multi-source BFS over 4-neighborhood; distances in meters. Cells of `src`
// get 0; empty `src` leaves all cells at infinity.
std::vector<double> bfs_distance_m(const std::vector<uint8_t>& src, int w, int h, double cell_size) {
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(src.size(), inf);
    std::queue<int> q;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i]) {
            dist[i] = 0.0;
            q.push(static_cast<int>(i));
        }
    }
    static constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!q.empty()) {
        const int cur = q.front();
        q.pop();
        const int cx = cur % w;
        const int cy = cur / w;
        for (const auto& d : kDirs) {
            const int nx = cx + d[0];
            const int ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const int ni = ny * w + nx;
            if (dist[ni] == inf) {
                dist[ni] = dist[cur] + cell_size;
                q.push(ni);
            }
        }
    }
    return dist;
}

}  // namespace

bool Analysis::in_bounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}

double Analysis::slope_at(int x, int y) const {
    return slope[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

double Analysis::cost_at(int x, int y) const {
    return cost[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

double Analysis::water_dist_at(int x, int y) const {
    return water_dist[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

double Analysis::forest_dist_at(int x, int y) const {
    return forest_dist[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

double Analysis::attractiveness_at(int x, int y) const {
    return attractiveness[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

Analysis analyze_terrain(const Terrain& terrain, const AnalysisParams& params) {
    Analysis a;
    a.width = terrain.width;
    a.height = terrain.height;
    a.cell_size = terrain.cell_size;
    const int w = terrain.width;
    const int h = terrain.height;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    a.slope.resize(n);
    a.cost.resize(n);

    // Central differences with clamped borders; gradient in rise per meter.
    auto h_at = [&](int x, int y) {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return terrain.height_at(x, y);
    };
    const double inv_2cell = 1.0 / (2.0 * terrain.cell_size);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const double dzdx = (h_at(x + 1, y) - h_at(x - 1, y)) * inv_2cell;
            const double dzdy = (h_at(x, y + 1) - h_at(x, y - 1)) * inv_2cell;
            const double s = std::sqrt(dzdx * dzdx + dzdy * dzdy);
            a.slope[i] = s;
            if (terrain.is_water(x, y)) {
                a.cost[i] = std::numeric_limits<double>::infinity();
            } else {
                a.cost[i] = 1.0 + s * params.slope_cost_factor
                          + (terrain.is_forest(x, y) ? params.forest_cost : 0.0);
            }
        }
    }

    std::vector<uint8_t> water_src(n);
    for (size_t i = 0; i < n; ++i) {
        water_src[i] = std::isinf(a.cost[i]) ? 1 : 0;
    }
    a.water_dist = bfs_distance_m(water_src, w, h, terrain.cell_size);
    a.forest_dist = bfs_distance_m(terrain.forest, w, h, terrain.cell_size);

    // Attractiveness: flat land near water (and mildly near forest) settles
    // first. Water and steep cells are unbuildable.
    a.attractiveness.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (water_src[i] || a.slope[i] >= params.max_build_slope) {
            a.attractiveness[i] = 0.0;
            continue;
        }
        const double flat = 1.0 - a.slope[i] / params.max_build_slope;
        const double water_bonus = std::isfinite(a.water_dist[i])
                                       ? clamp01(1.0 - a.water_dist[i] / params.water_far)
                                       : 0.0;
        const double forest_bonus = std::isfinite(a.forest_dist[i])
                                        ? clamp01(1.0 - a.forest_dist[i] / params.forest_far)
                                        : 0.0;
        a.attractiveness[i] = flat * (0.5 + 0.35 * water_bonus + 0.15 * forest_bonus);
    }
    return a;
}

}  // namespace village_topology_generator
