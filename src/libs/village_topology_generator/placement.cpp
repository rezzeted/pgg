#include "pch.h"

#include "placement.h"

namespace village_topology_generator {
namespace {

// Parcel centroid in meters (cell centers averaged).
std::pair<double, double> parcel_centroid(const Parcel& p, double cell_size) {
    double sx = 0.0;
    double sy = 0.0;
    for (const auto& c : p.cells) {
        sx += c.first + 0.5;
        sy += c.second + 0.5;
    }
    const double n = static_cast<double>(p.cells.size());
    return {sx * cell_size / n, sy * cell_size / n};
}

std::pair<int, int> parcel_centroid_cell(const Parcel& p) {
    double sx = 0.0;
    double sy = 0.0;
    for (const auto& c : p.cells) {
        sx += c.first;
        sy += c.second;
    }
    const double n = static_cast<double>(p.cells.size());
    return {static_cast<int>(std::lround(sx / n)), static_cast<int>(std::lround(sy / n))};
}

double parcel_min_field(const std::vector<double>& field, int w, const Parcel& p) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& c : p.cells) {
        best = std::min(best, field[static_cast<size_t>(c.second) * w + c.first]);
    }
    return best;
}

bool satisfies_req(const Terrain& terrain,
                   const Analysis& analysis,
                   const Parcel& p,
                   SiteReq req,
                   std::pair<int, int> center,
                   const PlacementParams& params) {
    const int w = analysis.width;
    switch (req) {
        case SiteReq::near_water:
            return parcel_min_field(analysis.water_dist, w, p) <= params.water_near;
        case SiteReq::near_forest:
            return parcel_min_field(analysis.forest_dist, w, p) <= params.forest_near;
        case SiteReq::near_center: {
            const auto [cx, cy] = parcel_centroid(p, analysis.cell_size);
            const double dx = cx - (center.first + 0.5) * analysis.cell_size;
            const double dy = cy - (center.second + 0.5) * analysis.cell_size;
            return std::sqrt(dx * dx + dy * dy) <= params.center_near;
        }
        case SiteReq::on_hill: {
            const auto [cx, cy] = parcel_centroid_cell(p);
            if (!terrain.in_bounds(cx, cy) || !terrain.in_bounds(center.first, center.second)) {
                return false;
            }
            return terrain.height_at(cx, cy) >= terrain.height_at(center.first, center.second);
        }
    }
    return false;
}

}  // namespace

std::vector<std::string> validate_roster(const Roster& roster) {
    std::vector<std::string> violations;
    for (const auto& item : roster) {
        for (const auto& req : item.requires_types) {
            bool found = false;
            for (const auto& other : roster) {
                if (other.type == req) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                violations.push_back(item.type + " requires missing type: " + req);
            }
        }
    }
    return violations;
}

std::vector<int> candidate_parcels(const Terrain& terrain,
                                   const Analysis& analysis,
                                   const std::vector<Parcel>& parcels,
                                   const RosterItem& item,
                                   std::pair<int, int> center,
                                   const PlacementParams& params) {
    std::vector<int> out;
    for (size_t i = 0; i < parcels.size(); ++i) {
        bool ok = true;
        for (const SiteReq req : item.site) {
            if (!satisfies_req(terrain, analysis, parcels[i], req, center, params)) {
                ok = false;
                break;
            }
        }
        if (ok) out.push_back(static_cast<int>(i));
    }
    return out;
}

PlacementResult place_roster(const Terrain& terrain,
                             const Analysis& analysis,
                             const std::vector<Parcel>& parcels,
                             const Roster& roster,
                             std::pair<int, int> center,
                             const PlacementParams& params) {
    PlacementResult result;
    result.buildings.resize(roster.size());

    // Candidates per item; items with none are reported unplaced upfront.
    std::vector<std::vector<int>> cand(roster.size());
    std::vector<int> order;  // search order: roster indices, constrained first
    for (size_t i = 0; i < roster.size(); ++i) {
        cand[i] = candidate_parcels(terrain, analysis, parcels, roster[i], center, params);
        if (cand[i].empty()) {
            result.unplaced.push_back(static_cast<int>(i));
        } else {
            order.push_back(static_cast<int>(i));
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return cand[a].size() < cand[b].size();
    });

    std::vector<int> assignment(roster.size(), -1);
    std::vector<uint8_t> used(parcels.size(), 0);
    const auto search = [&](auto&& self, size_t depth) -> bool {
        if (depth == order.size()) return true;
        const int item = order[depth];
        for (const int pi : cand[item]) {
            if (used[pi]) continue;
            used[pi] = 1;
            assignment[item] = pi;
            if (self(self, depth + 1)) return true;
            assignment[item] = -1;
            used[pi] = 0;
        }
        return false;
    };
    search(search, 0);

    for (size_t i = 0; i < roster.size(); ++i) {
        if (assignment[i] < 0) {
            if (std::find(result.unplaced.begin(), result.unplaced.end(),
                          static_cast<int>(i)) == result.unplaced.end()) {
                result.unplaced.push_back(static_cast<int>(i));
            }
            continue;
        }
        const Parcel& p = parcels[assignment[i]];
        PlacedBuilding b;
        b.roster_index = static_cast<int>(i);
        b.type = roster[i].type;
        b.parcel_index = assignment[i];
        b.cell = p.front[p.front.size() / 2];
        // Facing: from the lot centroid to the front row centroid (= the road).
        const auto [cx, cy] = parcel_centroid(p, analysis.cell_size);
        double fx = 0.0;
        double fy = 0.0;
        for (const auto& c : p.front) {
            fx += c.first + 0.5;
            fy += c.second + 0.5;
        }
        fx = fx * analysis.cell_size / p.front.size() - cx;
        fy = fy * analysis.cell_size / p.front.size() - cy;
        const double len = std::sqrt(fx * fx + fy * fy);
        if (len > 0.0) b.facing = {fx / len, fy / len};
        result.buildings[i] = std::move(b);
    }
    std::sort(result.unplaced.begin(), result.unplaced.end());
    result.complete = result.unplaced.empty();
    return result;
}

std::vector<std::string> validate_placement(const Roster& roster,
                                            const PlacementResult& result) {
    std::vector<std::string> violations;
    for (const auto& item : roster) {
        for (const auto& req : item.requires_types) {
            bool placed = false;
            for (size_t i = 0; i < roster.size(); ++i) {
                if (roster[i].type == req
                    && result.buildings[i].parcel_index >= 0
                    && std::find(result.unplaced.begin(), result.unplaced.end(),
                                 static_cast<int>(i)) == result.unplaced.end()) {
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                violations.push_back(item.type + " requires unplaced type: " + req);
            }
        }
    }
    return violations;
}

}  // namespace village_topology_generator
