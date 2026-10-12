#include "pch.h"

#include "parcels.h"

namespace village_topology_generator {
namespace {

int clamp1(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

}  // namespace

std::vector<Parcel> grow_parcels(const Analysis& analysis,
                                 const RoadNetwork& network,
                                 const ParcelParams& params) {
    const int w = analysis.width;
    const int h = analysis.height;
    std::vector<Parcel> parcels;
    if (w <= 0 || h <= 0 || analysis.cell_size <= 0.0) return parcels;

    const int front_cells = std::max(1, static_cast<int>(std::lround(params.front_width / analysis.cell_size)));
    const int max_depth_cells = std::max(1, static_cast<int>(std::lround(params.max_depth / analysis.cell_size)));
    const int min_depth_cells = std::max(1, static_cast<int>(std::lround(params.min_depth / analysis.cell_size)));
    const int min_front = (front_cells + 1) / 2;

    // Occupancy: roads block both frontage and depth rays; claimed is filled
    // with parcel cells as they are emitted.
    std::vector<uint8_t> road_at(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (const auto& r : network.roads) {
        for (const auto& [x, y] : r.cells) {
            road_at[static_cast<size_t>(y) * w + x] = 1;
        }
    }
    std::vector<uint8_t> claimed(road_at.size(), 0);

    auto buildable = [&](int x, int y) {
        return analysis.in_bounds(x, y)
            && std::isfinite(analysis.cost_at(x, y))
            && analysis.slope_at(x, y) < params.max_slope
            && !road_at[static_cast<size_t>(y) * w + x]
            && !claimed[static_cast<size_t>(y) * w + x];
    };

    for (size_t ri = 0; ri < network.roads.size(); ++ri) {
        const auto& cells = network.roads[ri].cells;
        if (cells.size() < 2) continue;
        for (const int side_sign : {1, -1}) {
            std::vector<std::pair<int, int>> front;   // road cells of the current run
            std::pair<int, int> run_perp{0, 0};       // perpendicular of the run

            auto flush = [&]() {
                if (static_cast<int>(front.size()) < min_front) {
                    front.clear();
                    return;
                }
                // Lot depth: the shallowest perpendicular ray over the front.
                int depth = max_depth_cells;
                for (const auto& c : front) {
                    int d = 0;
                    while (d < max_depth_cells
                           && buildable(c.first + (d + 1) * run_perp.first,
                                        c.second + (d + 1) * run_perp.second)) {
                        ++d;
                    }
                    depth = std::min(depth, d);
                }
                if (depth >= min_depth_cells) {
                    Parcel p;
                    p.road_index = static_cast<int>(ri);
                    p.side = side_sign;
                    p.cells.reserve(front.size() * static_cast<size_t>(depth));
                    for (int k = 1; k <= depth; ++k) {
                        for (const auto& c : front) {
                            const int x = c.first + k * run_perp.first;
                            const int y = c.second + k * run_perp.second;
                            p.cells.emplace_back(x, y);
                            claimed[static_cast<size_t>(y) * w + x] = 1;
                            if (k == 1) p.front.emplace_back(x, y);
                        }
                    }
                    parcels.push_back(std::move(p));
                }
                front.clear();
            };

            for (size_t i = 0; i < cells.size(); ++i) {
                const auto& prev = cells[i > 0 ? i - 1 : 0];
                const auto& next = cells[i + 1 < cells.size() ? i + 1 : cells.size() - 1];
                const std::pair<int, int> dir{clamp1(next.first - prev.first),
                                              clamp1(next.second - prev.second)};
                if (dir.first == 0 && dir.second == 0) continue;  // degenerate path cell
                // Left normal of (dx,dy) is (-dy,dx); side_sign flips to right.
                const std::pair<int, int> perp{-dir.second * side_sign, dir.first * side_sign};
                const int fx = cells[i].first + perp.first;
                const int fy = cells[i].second + perp.second;
                if (!buildable(fx, fy)) {
                    flush();
                    continue;
                }
                if (!front.empty() && perp != run_perp) flush();  // bend in the road
                if (front.empty()) run_perp = perp;
                front.push_back(cells[i]);
                if (static_cast<int>(front.size()) >= front_cells) flush();
            }
            flush();
        }
    }
    return parcels;
}

}  // namespace village_topology_generator
