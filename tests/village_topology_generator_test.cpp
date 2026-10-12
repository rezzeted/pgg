// village_topology_generator tests: terrain determinism, analysis, anchors,
// A* roads, road skeleton (docs/village_topology_generator/requirements.md §10).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "analysis.h"
#include "anchors.h"
#include "parcels.h"
#include "roads.h"
#include "terrain.h"

using namespace village_topology_generator;

namespace {

Terrain make_flat_terrain(int w, int h, double height = 10.0) {
    Terrain t;
    t.width = w;
    t.height = h;
    t.cell_size = 1.0;
    t.water_level = 5.0;
    t.heights.assign(static_cast<size_t>(w) * static_cast<size_t>(h), height);
    t.forest.assign(t.heights.size(), 0);
    return t;
}

// Nearest non-water cell to (x, y), expanding square ring scan; deterministic.
std::pair<int, int> nearest_land(const Terrain& t, int x, int y) {
    for (int r = 0; r < t.width + t.height; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int nx = x + dx;
                const int ny = y + dy;
                if (t.in_bounds(nx, ny) && !t.is_water(nx, ny)) {
                    return {nx, ny};
                }
            }
        }
    }
    return {x, y};
}

TEST(VillageTerrain, Deterministic) {
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 42;
    const Terrain a = generate_terrain(p);
    const Terrain b = generate_terrain(p);
    EXPECT_EQ(a.heights, b.heights);
    EXPECT_EQ(a.forest, b.forest);

    p.seed = 43;
    const Terrain c = generate_terrain(p);
    EXPECT_NE(a.heights, c.heights);
}

TEST(VillageTerrain, HasReliefVariety) {
    TerrainParams p;
    p.seed = 7;
    const Terrain t = generate_terrain(p);
    const auto mm = std::minmax_element(t.heights.begin(), t.heights.end());
    EXPECT_LT(*mm.first, *mm.second);
}

TEST(VillageTerrain, ProducesWaterAndLand) {
    // Water level at half amplitude: mean fBm height sits right at it, so a
    // 64x64 map reliably shows both water and land.
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 7;
    p.water_level = p.amplitude * 0.5;
    const Terrain t = generate_terrain(p);
    const auto is_water = [&](double v) { return v < t.water_level; };
    EXPECT_TRUE(std::any_of(t.heights.begin(), t.heights.end(), is_water));
    EXPECT_TRUE(std::any_of(t.heights.begin(), t.heights.end(),
                            [&](double v) { return !is_water(v); }));
}

TEST(VillageTerrain, ForestStaysOffWater) {
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 7;
    p.water_level = p.amplitude * 0.5;
    const Terrain t = generate_terrain(p);
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            if (t.is_forest(x, y)) {
                EXPECT_FALSE(t.is_water(x, y));
            }
        }
    }
}

TEST(VillageAnalysis, FlatTerrainZeroSlope) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    for (double s : a.slope) {
        EXPECT_DOUBLE_EQ(s, 0.0);
    }
}

TEST(VillageAnalysis, RampHasSlope) {
    Terrain t = make_flat_terrain(32, 32);
    // 1 m rise per 1 m cell along x.
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 10.0 + x;
        }
    }
    const Analysis a = analyze_terrain(t);
    EXPECT_NEAR(a.slope_at(16, 16), 1.0, 1e-9);
}

TEST(VillageAnalysis, WaterImpassableForestPenalized) {
    Terrain t = make_flat_terrain(16, 16);
    t.heights[3 * t.width + 3] = 0.0;  // water
    t.forest[5 * t.width + 5] = 1;
    const Analysis a = analyze_terrain(t);
    EXPECT_TRUE(std::isinf(a.cost_at(3, 3)));
    EXPECT_GT(a.cost_at(5, 5), a.cost_at(8, 8));
    EXPECT_DOUBLE_EQ(a.cost_at(8, 8), 1.0);  // flat open land far from the dip
}

TEST(VillageRoads, StraightOnFlat) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    const auto path = find_road(a, {1, 1}, {10, 1});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->cells.front(), std::make_pair(1, 1));
    EXPECT_EQ(path->cells.back(), std::make_pair(10, 1));
    // Uniquely optimal on flat ground: straight horizontal line.
    ASSERT_EQ(path->cells.size(), 10u);
    for (size_t i = 0; i < path->cells.size(); ++i) {
        EXPECT_EQ(path->cells[i].first, 1 + static_cast<int>(i));
        EXPECT_EQ(path->cells[i].second, 1);
    }
    EXPECT_NEAR(path->total_cost, 9.0, 1e-9);
}

TEST(VillageRoads, SameCell) {
    const Terrain t = make_flat_terrain(8, 8);
    const Analysis a = analyze_terrain(t);
    const auto path = find_road(a, {3, 3}, {3, 3});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->cells.size(), 1u);
    EXPECT_DOUBLE_EQ(path->total_cost, 0.0);
}

TEST(VillageRoads, DetoursAroundWater) {
    Terrain t = make_flat_terrain(32, 32);
    // Vertical water wall at x=15 with a single gap at y=20.
    for (int y = 0; y < t.height; ++y) {
        if (y != 20) {
            t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + 15] = 0.0;
        }
    }
    const Analysis a = analyze_terrain(t);
    const auto path = find_road(a, {2, 2}, {28, 2});
    ASSERT_TRUE(path.has_value());
    for (const auto& [x, y] : path->cells) {
        EXPECT_FALSE(t.is_water(x, y));
    }
    const auto in_gap = [](const std::pair<int, int>& c) { return c.first == 15 && c.second == 20; };
    EXPECT_TRUE(std::any_of(path->cells.begin(), path->cells.end(), in_gap));
}

TEST(VillageRoads, AvoidsSteepSlope) {
    Terrain t = make_flat_terrain(32, 32);
    // Ridge across the map at x=15: a tall stripe costs more than a detour
    // around its edge... keep it simple: the stripe ends at y=24, so the
    // cheap route crosses below it. Assert the path does not step onto the
    // ridge cells where a flat alternative exists.
    for (int y = 0; y < 24; ++y) {
        t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + 15] = 40.0;
    }
    const AnalysisParams params;  // slope_cost_factor = 6
    const Analysis a = analyze_terrain(t, params);
    const auto path = find_road(a, {2, 2}, {28, 2});
    ASSERT_TRUE(path.has_value());
    for (const auto& [x, y] : path->cells) {
        const bool on_ridge = (x == 15 && y < 24);
        EXPECT_FALSE(on_ridge);
    }
}

TEST(VillageRoads, UnreachableAcrossWaterRing) {
    Terrain t = make_flat_terrain(21, 21);
    // Closed water ring around the center isolates it.
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            const int dx = std::abs(x - 10);
            const int dy = std::abs(y - 10);
            if (std::max(dx, dy) == 5) {
                t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 0.0;
            }
        }
    }
    const Analysis a = analyze_terrain(t);
    EXPECT_FALSE(find_road(a, {10, 10}, {0, 0}).has_value());
}

TEST(VillageRoads, Deterministic) {
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 11;
    p.water_level = 9.0;
    const Terrain t = generate_terrain(p);
    const Analysis a = analyze_terrain(t);
    // Endpoints on land; the generated map at this seed must keep them connected.
    const auto from = nearest_land(t, 2, 2);
    const auto to = nearest_land(t, 60, 60);
    const auto p1 = find_road(a, from, to);
    const auto p2 = find_road(a, from, to);
    ASSERT_TRUE(p1.has_value());
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(p1->cells, p2->cells);
    EXPECT_DOUBLE_EQ(p1->total_cost, p2->total_cost);
}

// --- S2: distance fields, attractiveness, anchors, skeleton ---

TEST(VillageAnalysis, WaterDistanceField) {
    Terrain t = make_flat_terrain(16, 16);
    for (int y = 0; y < t.height; ++y) {
        t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + 0] = 0.0;  // water column x=0
    }
    const Analysis a = analyze_terrain(t);
    EXPECT_DOUBLE_EQ(a.water_dist_at(0, 8), 0.0);
    EXPECT_DOUBLE_EQ(a.water_dist_at(5, 8), 5.0);  // cell_size = 1
}

TEST(VillageAnalysis, WaterDistanceInfiniteWithoutWater) {
    const Terrain t = make_flat_terrain(8, 8);
    const Analysis a = analyze_terrain(t);
    EXPECT_TRUE(std::isinf(a.water_dist_at(4, 4)));
}

TEST(VillageAnalysis, AttractivenessPrefersFlatNearWater) {
    // Gentle shore: 3 shallow water columns, then flat land — no cliff, so the
    // first fully-flat land cell (x=4) must beat the far-from-water cell (x=31).
    Terrain t = make_flat_terrain(32, 32, 5.4);
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < 3; ++x) {
            t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 4.0;
        }
    }
    const Analysis a = analyze_terrain(t);
    EXPECT_DOUBLE_EQ(a.attractiveness_at(0, 16), 0.0);          // water itself
    EXPECT_GT(a.attractiveness_at(4, 16), a.attractiveness_at(31, 16));  // near > far
}

TEST(VillageAnalysis, SteepSlopeUnbuildable) {
    Terrain t = make_flat_terrain(32, 32);
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 10.0 + x;
        }
    }
    const Analysis a = analyze_terrain(t);  // slope = 1.0 > max_build_slope = 0.5
    EXPECT_DOUBLE_EQ(a.attractiveness_at(16, 16), 0.0);
}

TEST(VillageAnchors, CenterOnFlatInsideCoreRegion) {
    const Terrain t = make_flat_terrain(33, 33);
    const Analysis a = analyze_terrain(t);
    const auto c = pick_center(a);
    // Quarter-map margin: plateau at d >= 8.25 -> x, y in [9, 23].
    EXPECT_GE(c.first, 9);
    EXPECT_LE(c.first, 23);
    EXPECT_GE(c.second, 9);
    EXPECT_LE(c.second, 23);
    EXPECT_GT(a.attractiveness_at(c.first, c.second), 0.0);
}

TEST(VillageAnchors, CenterFollowsWater) {
    // Same flat map with a pond west vs east of the middle: center must follow.
    auto make_with_pond = [](int pond_x) {
        Terrain t = make_flat_terrain(41, 41);
        for (int y = 18; y <= 22; ++y) {
            for (int x = pond_x; x <= pond_x + 2; ++x) {
                t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 0.0;
            }
        }
        return t;
    };
    const auto ca = pick_center(analyze_terrain(make_with_pond(8)));
    const auto cb = pick_center(analyze_terrain(make_with_pond(30)));
    EXPECT_LT(ca.first, 20);
    EXPECT_GT(cb.first, 20);
}

TEST(VillageAnchors, WaterSiteNextToWaterNearCenter) {
    Terrain t = make_flat_terrain(21, 21);
    for (int y = 9; y <= 11; ++y) {
        for (int x = 5; x <= 7; ++x) {
            t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 0.0;
        }
    }
    const Analysis a = analyze_terrain(t);
    const auto site = pick_water_site(t, a, {10, 10});
    ASSERT_TRUE(site.has_value());
    EXPECT_EQ(*site, std::make_pair(8, 10));
    EXPECT_FALSE(t.is_water(site->first, site->second));
}

TEST(VillageAnchors, WaterSiteAbsentWithoutWater) {
    const Terrain t = make_flat_terrain(16, 16);
    const Analysis a = analyze_terrain(t);
    EXPECT_FALSE(pick_water_site(t, a, {8, 8}).has_value());
}

TEST(VillageAnchors, EdgeAnchorsOnePerSide) {
    const Terrain t = make_flat_terrain(16, 16);
    const Analysis a = analyze_terrain(t);
    const auto anchors = pick_edge_anchors(t, a);
    ASSERT_EQ(anchors.size(), 4u);
    EXPECT_EQ(anchors[0].cell, std::make_pair(8, 0));    // N
    EXPECT_EQ(anchors[1].cell, std::make_pair(8, 15));   // S
    EXPECT_EQ(anchors[2].cell, std::make_pair(0, 8));    // W
    EXPECT_EQ(anchors[3].cell, std::make_pair(15, 8));   // E
}

TEST(VillageAnchors, EdgeAnchorsSkipWaterSide) {
    Terrain t = make_flat_terrain(16, 16);
    for (int y = 0; y < t.height; ++y) {
        t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width)] = 0.0;  // west column water
    }
    const Analysis a = analyze_terrain(t);
    const auto anchors = pick_edge_anchors(t, a);
    EXPECT_EQ(anchors.size(), 3u);
    for (const auto& an : anchors) {
        EXPECT_GT(an.cell.first, 0);
        EXPECT_FALSE(t.is_water(an.cell.first, an.cell.second));
    }
}

size_t count_shared_cells(const RoadPath& a, const RoadPath& b) {
    const std::set<std::pair<int, int>> sa(a.cells.begin(), a.cells.end());
    size_t n = 0;
    for (const auto& c : b.cells) {
        if (sa.count(c)) ++n;
    }
    return n;
}

TEST(VillageSkeleton, ReachesAnchorsNearestFirst) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    const std::vector<std::pair<int, int>> anchors = {{28, 16}, {6, 16}, {16, 28}};
    const auto net = grow_skeleton(a, {16, 16}, anchors);
    // dist2: (6,16)=100, (28,16)=144, (16,28)=144; tie broken by (y, x).
    const std::vector<std::pair<int, int>> expected = {{6, 16}, {28, 16}, {16, 28}};
    ASSERT_EQ(net.reached, expected);
    ASSERT_EQ(net.roads.size(), 3u);
    for (size_t i = 0; i < net.roads.size(); ++i) {
        EXPECT_EQ(net.roads[i].cells.front(), std::make_pair(16, 16));
        EXPECT_EQ(net.roads[i].cells.back(), expected[i]);
    }
}

TEST(VillageSkeleton, MergesBranches) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    const std::vector<std::pair<int, int>> anchors = {{28, 10}, {28, 22}};

    SkeletonParams merge_off;
    merge_off.merge_roads = false;
    const auto plain = grow_skeleton(a, {2, 16}, anchors, merge_off);
    ASSERT_EQ(plain.roads.size(), 2u);

    const auto merged = grow_skeleton(a, {2, 16}, anchors);
    ASSERT_EQ(merged.roads.size(), 2u);

    const size_t shared_plain = count_shared_cells(plain.roads[0], plain.roads[1]);
    const size_t shared_merged = count_shared_cells(merged.roads[0], merged.roads[1]);
    EXPECT_GE(shared_plain, 1u);  // at least the common center cell
    EXPECT_GT(shared_merged, shared_plain);
}

TEST(VillageSkeleton, SkipsUnreachableAnchor) {
    Terrain t = make_flat_terrain(21, 21);
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            const int dx = std::abs(x - 10);
            const int dy = std::abs(y - 10);
            if (std::max(dx, dy) == 5) {
                t.heights[static_cast<size_t>(y) * static_cast<size_t>(t.width) + static_cast<size_t>(x)] = 0.0;
            }
        }
    }
    const Analysis a = analyze_terrain(t);
    // (2,2) is outside the water ring, (10,7) inside with the center.
    const auto net = grow_skeleton(a, {10, 10}, {{2, 2}, {10, 7}});
    ASSERT_EQ(net.roads.size(), 1u);
    ASSERT_EQ(net.reached.size(), 1u);
    EXPECT_EQ(net.reached[0], std::make_pair(10, 7));
}

TEST(VillageSkeleton, Deterministic) {
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 21;
    p.water_level = 9.0;
    const Terrain t = generate_terrain(p);
    const Analysis a = analyze_terrain(t);
    const auto center = pick_center(a);
    std::vector<std::pair<int, int>> anchors;
    for (const auto& an : pick_edge_anchors(t, a)) {
        anchors.push_back(an.cell);
    }
    if (const auto ws = pick_water_site(t, a, center)) {
        anchors.push_back(*ws);
    }
    const auto n1 = grow_skeleton(a, center, anchors);
    const auto n2 = grow_skeleton(a, center, anchors);
    ASSERT_EQ(n1.roads.size(), n2.roads.size());
    for (size_t i = 0; i < n1.roads.size(); ++i) {
        EXPECT_EQ(n1.roads[i].cells, n2.roads[i].cells);
    }
}

TEST(VillageSkeleton, TrunkConnectsFarthestPair) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    SkeletonParams params;
    params.style = GrowthStyle::trunk;
    // Farthest pair: (2,16)-(28,16). (16,4) branches off, center (16,16) is
    // on the trunk already.
    const std::vector<std::pair<int, int>> anchors = {{2, 16}, {28, 16}, {16, 4}};
    const auto net = grow_skeleton(a, {16, 16}, anchors, params);
    ASSERT_EQ(net.roads.size(), 2u);  // trunk + one branch
    // Trunk is the first road, endpoints are the farthest pair (order by
    // distance from (0,0): (2,16) first).
    EXPECT_EQ(net.roads[0].cells.front(), std::make_pair(2, 16));
    EXPECT_EQ(net.roads[0].cells.back(), std::make_pair(28, 16));
    // Branch starts at the off-trunk anchor and ends on the trunk.
    EXPECT_EQ(net.roads[1].cells.front(), std::make_pair(16, 4));
    EXPECT_EQ(net.roads[1].cells.back(), std::make_pair(16, 16));
    // reached holds trunk endpoints, then connected branches.
    ASSERT_EQ(net.reached.size(), 3u);
}

TEST(VillageSkeleton, TrunkBranchFollowsTrunk) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    SkeletonParams params;
    params.style = GrowthStyle::trunk;
    // Two off-trunk anchors on the same side: the farther branch must join
    // the nearer branch's path (Y-junction) instead of reaching the trunk.
    const std::vector<std::pair<int, int>> anchors = {{2, 16}, {28, 16}, {16, 10}, {16, 4}};
    const auto net = grow_skeleton(a, {16, 16}, anchors, params);
    ASSERT_EQ(net.roads.size(), 3u);  // trunk + 2 branches (center on trunk)
    // Branches are built nearest-cost first: (16,10) before (16,4).
    EXPECT_EQ(net.roads[1].cells.front(), std::make_pair(16, 10));
    EXPECT_EQ(net.roads[2].cells.front(), std::make_pair(16, 4));
    // The (16,4) branch merges into the (16,10) branch at its junction cell...
    const std::set<std::pair<int, int>> branch1(net.roads[1].cells.begin(), net.roads[1].cells.end());
    EXPECT_TRUE(branch1.count(net.roads[2].cells.back()) != 0);
    // ...and never reaches the trunk row itself.
    for (const auto& [x, y] : net.roads[2].cells) {
        EXPECT_NE(y, 16);
    }
}

TEST(VillageSkeleton, TrunkDeterministic) {
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 33;
    p.water_level = 9.0;
    const Terrain t = generate_terrain(p);
    const Analysis a = analyze_terrain(t);
    const auto center = pick_center(a);
    std::vector<std::pair<int, int>> anchors;
    for (const auto& an : pick_edge_anchors(t, a)) {
        anchors.push_back(an.cell);
    }
    SkeletonParams params;
    params.style = GrowthStyle::trunk;
    const auto n1 = grow_skeleton(a, center, anchors, params);
    const auto n2 = grow_skeleton(a, center, anchors, params);
    ASSERT_EQ(n1.roads.size(), n2.roads.size());
    for (size_t i = 0; i < n1.roads.size(); ++i) {
        EXPECT_EQ(n1.roads[i].cells, n2.roads[i].cells);
    }
}

RoadNetwork make_straight_road_network(int y = 16, int x0 = 2, int x1 = 28) {
    RoadNetwork net;
    RoadPath road;
    for (int x = x0; x <= x1; ++x) {
        road.cells.emplace_back(x, y);
    }
    net.roads.push_back(std::move(road));
    return net;
}

TEST(VillageParcels, StraightRoadBothSides) {
    const Terrain t = make_flat_terrain(32, 32);
    const Analysis a = analyze_terrain(t);
    const auto net = make_straight_road_network();  // 27 cells at y=16
    ParcelParams params;
    params.front_width = 5;
    params.max_depth = 5;
    params.min_depth = 2;
    const auto parcels = grow_parcels(a, net, params);
    // 27 front cells / 5 per lot = 5 lots per side; leftover 2 < min_front 3.
    ASSERT_EQ(parcels.size(), 10u);
    for (const auto& p : parcels) {
        EXPECT_EQ(p.road_index, 0);
        EXPECT_EQ(p.front.size(), 5u);
        EXPECT_EQ(p.cells.size(), 25u);
        const int front_y = p.side == 1 ? 17 : 15;
        for (const auto& [x, y] : p.front) {
            EXPECT_EQ(y, front_y);
        }
    }
    // Side +1 fronts come first, in path order: x chunks 2..6, 7..11, ...
    EXPECT_EQ(parcels[0].side, 1);
    EXPECT_EQ(parcels[0].front.front(), std::make_pair(2, 17));
    EXPECT_EQ(parcels[0].front.back(), std::make_pair(6, 17));
    EXPECT_EQ(parcels[4].front.front(), std::make_pair(22, 17));
    EXPECT_EQ(parcels[5].side, -1);
    EXPECT_EQ(parcels[5].front.front(), std::make_pair(2, 15));
}

TEST(VillageParcels, DepthStopsAtWater) {
    Terrain t = make_flat_terrain(32, 32);
    for (int x = 0; x < 32; ++x) {
        t.heights[static_cast<size_t>(20) * 32 + x] = 0.0;  // water row y=20
    }
    const Analysis a = analyze_terrain(t);
    const auto net = make_straight_road_network();
    ParcelParams params;
    params.front_width = 5;
    params.max_depth = 5;
    params.min_depth = 2;
    params.max_slope = 1000.0;  // isolate water blocking from shore slope
    const auto parcels = grow_parcels(a, net, params);
    ASSERT_EQ(parcels.size(), 10u);
    int shallow = 0;
    int deep = 0;
    for (const auto& p : parcels) {
        if (p.side == 1) {
            // Towards water: y=17..19 buildable, y=20 blocks -> depth 3.
            EXPECT_EQ(p.cells.size(), 15u);
            ++shallow;
            for (const auto& [x, y] : p.cells) {
                EXPECT_LT(y, 20);
            }
        } else {
            EXPECT_EQ(p.cells.size(), 25u);
            ++deep;
        }
    }
    EXPECT_EQ(shallow, 5);
    EXPECT_EQ(deep, 5);
}

TEST(VillageParcels, NoOverlapAndDeterministic) {
    TerrainParams p;
    p.width = 64;
    p.height = 64;
    p.seed = 7;
    p.water_level = 9.0;
    const Terrain t = generate_terrain(p);
    const Analysis a = analyze_terrain(t);
    const auto center = pick_center(a);
    std::vector<std::pair<int, int>> anchors;
    for (const auto& an : pick_edge_anchors(t, a)) {
        anchors.push_back(an.cell);
    }
    const auto net = grow_skeleton(a, center, anchors);

    const auto run = [&]() { return grow_parcels(a, net); };
    const auto parcels1 = run();
    const auto parcels2 = run();
    ASSERT_EQ(parcels1.size(), parcels2.size());
    ASSERT_FALSE(parcels1.empty());
    for (size_t i = 0; i < parcels1.size(); ++i) {
        EXPECT_EQ(parcels1[i].road_index, parcels2[i].road_index);
        EXPECT_EQ(parcels1[i].side, parcels2[i].side);
        EXPECT_EQ(parcels1[i].cells, parcels2[i].cells);
    }

    // No cell is claimed by two lots; no lot cell is water or road.
    std::set<std::pair<int, int>> road_cells;
    for (const auto& r : net.roads) {
        road_cells.insert(r.cells.begin(), r.cells.end());
    }
    std::set<std::pair<int, int>> seen;
    for (const auto& parcel : parcels1) {
        ASSERT_FALSE(parcel.cells.empty());
        for (const auto& c : parcel.cells) {
            EXPECT_TRUE(seen.insert(c).second) << "overlap at " << c.first << "," << c.second;
            EXPECT_FALSE(t.is_water(c.first, c.second));
            EXPECT_EQ(road_cells.count(c), 0u);
        }
    }
}

}  // namespace
