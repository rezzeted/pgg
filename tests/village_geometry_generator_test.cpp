// village_geometry_generator tests: Layout IR -> PGG assets -> merged mesh
// (S6, docs/village_topology_generator/requirements.md §10).

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "village_geometry.h"

#include "analysis.h"
#include "anchors.h"
#include "layout_ir.h"
#include "parcels.h"
#include "placement.h"
#include "roads.h"
#include "terrain.h"

#ifndef VILLAGE_CONTENT_PGG
#define VILLAGE_CONTENT_PGG "content/pgg"
#endif

using namespace village_topology_generator;

namespace {

nlohmann::json make_ir(uint64_t seed) {
    TerrainParams tp;
    tp.width = 64;
    tp.height = 64;
    tp.seed = seed;
    tp.water_level = 9.0;
    const Terrain t = generate_terrain(tp);
    const Analysis a = analyze_terrain(t);

    const auto center = pick_center(a);
    std::vector<Anchor> anchors = {
        {AnchorKind::center, center, a.attractiveness_at(center.first, center.second)}};
    std::vector<std::pair<int, int>> road_targets;
    if (const auto ws = pick_water_site(t, a, center)) {
        anchors.push_back({AnchorKind::water_site, *ws, a.attractiveness_at(ws->first, ws->second)});
        road_targets.push_back(*ws);
    }
    for (const auto& an : pick_edge_anchors(t, a)) {
        anchors.push_back(an);
        road_targets.push_back(an.cell);
    }
    const auto network = grow_skeleton(a, center, road_targets);
    const auto parcels = grow_parcels(a, network);

    Roster roster;
    roster.push_back({"mill", {SiteReq::near_water}, {}});
    roster.push_back({"bakery", {}, {"mill"}});
    roster.push_back({"tavern", {SiteReq::near_center}, {}});
    roster.push_back({"well", {SiteReq::near_center}, {}});
    for (int i = 0; i < 6; ++i) roster.push_back({"house", {}, {}});
    const auto placement = place_roster(t, a, parcels, roster, center);
    return build_layout_ir(tp, network, parcels, placement, anchors);
}

village_geometry_generator::GeomParams make_params() {
    village_geometry_generator::GeomParams params;
    params.assets_dir = std::string(VILLAGE_CONTENT_PGG) + "/Village/buildings";
    params.import_roots = {VILLAGE_CONTENT_PGG};
    params.seed = 1;
    return params;
}

TEST(VillageGeometry, BuildsEveryBuilding) {
    const auto ir = make_ir(11);
    const auto result = village_geometry_generator::generate_geometry(ir, make_params());
    for (const auto& d : result.diagnostics) {
        ADD_FAILURE() << "diagnostic: " << d;
    }
    ASSERT_TRUE(result.mesh);
    EXPECT_EQ(result.buildings_built, result.buildings_total);
    EXPECT_GT(result.mesh->pointCount(), 0u);
    EXPECT_GT(result.mesh->faceCount(), 0u);
    // World frame: the map is 64 cells * 2 m = 128 m across, centered nowhere
    // near the origin only in the map frame — points stay inside the map.
    for (const auto& p : *result.mesh->positions) {
        EXPECT_GE(p.x, -1.0f);
        EXPECT_LE(p.x, 130.0f);
        EXPECT_GE(p.z, -1.0f);
        EXPECT_LE(p.z, 130.0f);
        EXPECT_GE(p.y, -0.5f);
    }
}

TEST(VillageGeometry, Deterministic) {
    const auto ir = make_ir(11);
    const auto r1 = village_geometry_generator::generate_geometry(ir, make_params());
    const auto r2 = village_geometry_generator::generate_geometry(ir, make_params());
    ASSERT_TRUE(r1.mesh && r2.mesh);
    ASSERT_EQ(r1.mesh->pointCount(), r2.mesh->pointCount());
    ASSERT_EQ(r1.mesh->faceCount(), r2.mesh->faceCount());
    const auto& p1 = *r1.mesh->positions;
    const auto& p2 = *r2.mesh->positions;
    EXPECT_EQ(std::memcmp(p1.data(), p2.data(), p1.size() * sizeof(p1[0])), 0);
}

TEST(VillageGeometry, UnknownTypeFallsBackToHouse) {
    auto ir = make_ir(11);
    ASSERT_FALSE(ir["buildings"].empty());
    ir["buildings"][0]["type"] = "spaceship";
    const auto result = village_geometry_generator::generate_geometry(ir, make_params());
    EXPECT_EQ(result.buildings_built, result.buildings_total);
    bool fallback_logged = false;
    for (const auto& d : result.diagnostics) {
        if (d.find("spaceship") != std::string::npos) fallback_logged = true;
    }
    EXPECT_TRUE(fallback_logged);
}

TEST(VillageGeometry, TransformRotatesFacade) {
    // A single point at local +Z must land at +X after a +90 yaw (+ translation).
    auto g = pgg::makePoints({glm::vec3(0, 0, 2)});
    const auto t = village_geometry_generator::transform_geo(g, 90.0, 10.0, 20.0);
    ASSERT_EQ(t->pointCount(), 1u);
    const auto& p = (*t->positions)[0];
    EXPECT_NEAR(p.x, 12.0f, 1e-4f);
    EXPECT_NEAR(p.y, 0.0f, 1e-4f);
    EXPECT_NEAR(p.z, 20.0f, 1e-4f);
}

}  // namespace
