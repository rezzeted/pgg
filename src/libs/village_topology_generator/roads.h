#pragma once

// V4 Roads, S1 minimal form: least-cost road between two cells
// (docs/village_topology_generator/requirements.md §7).

#include <optional>
#include <utility>
#include <vector>

#include "analysis.h"

namespace village_topology_generator {

// Grid polyline from start to goal, both ends included.
struct RoadPath {
    std::vector<std::pair<int, int>> cells;
    double total_cost = 0.0;
};

// A* over the analysis cost field, 8-connected. Deterministic (N1): strict
// tie-breaking on (f, h, cell index). nullopt when unreachable (water-locked).
std::optional<RoadPath> find_road(const Analysis& analysis,
                                  std::pair<int, int> from,
                                  std::pair<int, int> to);

enum class GrowthStyle { radial, trunk };

struct SkeletonParams {
    double road_discount = 0.3;  // cost of cells already on a road (attracts new roads)
    bool merge_roads = true;     // apply the discount between roads (Y-junctions)
    GrowthStyle style = GrowthStyle::radial;
};

// Road skeleton (requirements.md §7, V4). radial: center to each anchor,
// nearest first. trunk: farthest anchor pair forms the main street, every
// other anchor (and the center) connects to it via least-cost Dijkstra.
// With merge_roads, cells of each built road get a cost discount, so later
// roads stick to existing ones and fork off later — natural Y-junctions
// instead of a pure star. Unreachable anchors are skipped.
struct RoadNetwork {
    std::vector<RoadPath> roads;             // construction order
    std::vector<std::pair<int, int>> reached;  // anchors actually connected, same order
    std::vector<double> cost_with_roads;     // cost field after all discounts (N5 debug)
};

RoadNetwork grow_skeleton(const Analysis& analysis,
                          std::pair<int, int> center,
                          const std::vector<std::pair<int, int>>& anchors,
                          const SkeletonParams& params = {});

}  // namespace village_topology_generator
