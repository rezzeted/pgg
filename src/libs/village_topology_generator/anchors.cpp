#include "pch.h"

#include "anchors.h"

namespace village_topology_generator {

std::pair<int, int> pick_center(const Analysis& analysis) {
    const int w = analysis.width;
    const int h = analysis.height;
    const double margin = 0.25 * std::min(w, h);  // cells; core plateau inside
    double best = -1.0;
    std::pair<int, int> best_cell{w / 2, h / 2};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double a = analysis.attractiveness_at(x, y);
            if (a <= 0.0) continue;
            const int d = std::min({x, y, w - 1 - x, h - 1 - y});
            const double falloff = margin > 0.0 ? std::min(1.0, d / margin) : 1.0;
            const double score = a * falloff;
            if (score > best) {
                best = score;
                best_cell = {x, y};
            }
        }
    }
    return best_cell;
}

std::optional<std::pair<int, int>> pick_water_site(const Terrain& terrain,
                                                   const Analysis& analysis,
                                                   std::pair<int, int> center) {
    const int w = terrain.width;
    const int h = terrain.height;
    static constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    bool found = false;
    long long best_d2 = 0;
    double best_attr = 0.0;
    std::pair<int, int> best_cell{0, 0};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (terrain.is_water(x, y)) continue;
            bool waterfront = false;
            for (const auto& d : kDirs) {
                const int nx = x + d[0];
                const int ny = y + d[1];
                if (terrain.in_bounds(nx, ny) && terrain.is_water(nx, ny)) {
                    waterfront = true;
                    break;
                }
            }
            if (!waterfront) continue;
            // No buildability filter: waterfront sites (mill, dock) sit on the
            // shore slope by definition. Attractiveness is the tie-break only.
            const double attr = analysis.attractiveness_at(x, y);
            const long long dx = x - center.first;
            const long long dy = y - center.second;
            const long long d2 = dx * dx + dy * dy;
            if (!found || d2 < best_d2 || (d2 == best_d2 && attr > best_attr)) {
                found = true;
                best_d2 = d2;
                best_attr = attr;
                best_cell = {x, y};
            }
        }
    }
    if (!found) return std::nullopt;
    return best_cell;
}

std::vector<Anchor> pick_edge_anchors(const Terrain& terrain, const Analysis& analysis) {
    const int w = terrain.width;
    const int h = terrain.height;
    std::vector<Anchor> out;

    // Scan one border side; pick max attractiveness, tie-break by closeness
    // to the side's middle cell, then by scan order.
    auto scan_side = [&](const std::vector<std::pair<int, int>>& cells) {
        bool found = false;
        double best_attr = 0.0;
        int best_mid = 0;
        std::pair<int, int> best_cell{0, 0};
        for (size_t i = 0; i < cells.size(); ++i) {
            const auto [x, y] = cells[i];
            if (terrain.is_water(x, y)) continue;
            const double attr = analysis.attractiveness_at(x, y);
            if (attr <= 0.0) continue;
            // Distance to the side's middle measured along the side.
            const int mid = std::abs(static_cast<int>(i) - static_cast<int>(cells.size() / 2));
            if (!found || attr > best_attr || (attr == best_attr && mid < best_mid)) {
                found = true;
                best_attr = attr;
                best_mid = mid;
                best_cell = {x, y};
            }
        }
        if (found) out.push_back({AnchorKind::edge, best_cell, best_attr});
    };

    std::vector<std::pair<int, int>> north, south, west, east;
    for (int x = 0; x < w; ++x) {
        north.emplace_back(x, 0);
        south.emplace_back(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        west.emplace_back(0, y);
        east.emplace_back(w - 1, y);
    }
    scan_side(north);
    scan_side(south);
    scan_side(west);
    scan_side(east);
    return out;
}

}  // namespace village_topology_generator
