// BuildingGenerator B0: fill the frozen house IR through the PGG slot assets —
// every unit builds, the merged mesh is non-empty, the §14.2 checks pass and
// anchors.json carries the IR anchors (docs/building_generator/requirements.md
// sections 11, 14.2, 15).

#include <gtest/gtest.h>

#include <string>

#include <nlohmann/json.hpp>

#include "building_geometry.h"
#include "ir.h"

namespace {

building_topology_generator::Ir loadFrozen() {
    building_topology_generator::Ir ir;
    std::string err;
    const std::string path = std::string(BUILDING_FROZEN_DIR) + "/house_b0.ir.json";
    const bool ok = building_topology_generator::load_ir_file(path, ir, err);
    EXPECT_TRUE(ok) << err;
    return ir;
}

building_geometry_generator::FillParams fillParams() {
    building_geometry_generator::FillParams params;
    params.assets_dir = BUILDING_ASSETS_DIR;
    params.import_roots = {BUILDING_PGG_CONTENT_DIR};
    params.seed = 1;
    return params;
}

TEST(BuildingFill, FrozenHouseFills) {
    const building_topology_generator::Ir ir = loadFrozen();
    const building_geometry_generator::FillResult fill =
        building_geometry_generator::fill_building(ir, fillParams());
    for (const std::string& d : fill.diagnostics) ADD_FAILURE() << d;
    EXPECT_EQ(fill.units_total, 126);
    EXPECT_EQ(fill.units_built, fill.units_total);
    ASSERT_NE(fill.mesh, nullptr);
    EXPECT_EQ(fill.mesh->kind, pgg::GeoKind::Mesh);
    EXPECT_GT(fill.mesh->pointCount(), 0u);
    for (const building_geometry_generator::FillUnit& u : fill.units) {
        SCOPED_TRACE(u.id);
        ASSERT_NE(u.mesh, nullptr);
        EXPECT_GT(u.mesh->pointCount(), 0u);
    }
}

TEST(BuildingFill, FrozenHouseChecksPass) {
    const building_topology_generator::Ir ir = loadFrozen();
    const building_geometry_generator::FillResult fill =
        building_geometry_generator::fill_building(ir, fillParams());
    const building_geometry_generator::CheckResult check =
        building_geometry_generator::check_building(ir, fill);
    for (const std::string& f : check.failures) ADD_FAILURE() << f;
    EXPECT_TRUE(check.ok());
    EXPECT_FALSE(check.notes.empty()) << "B0 notes the checks that do not apply yet";
}

TEST(BuildingFill, AnchorsJson) {
    const building_topology_generator::Ir ir = loadFrozen();
    const nlohmann::json anchors = building_geometry_generator::anchors_json(ir);
    EXPECT_EQ(anchors.at("format").get<std::string>(),
              "building-geometry-generator-anchors/0");
    // Entrance on the hall front, two ground-floor hearths, three bed slots.
    ASSERT_EQ(anchors.at("anchors").size(), 6u);
    int entrance = 0, hearth = 0, bed = 0;
    for (const nlohmann::json& a : anchors.at("anchors")) {
        const std::string kind = a.at("kind").get<std::string>();
        if (kind == "entrance") ++entrance;
        if (kind == "hearth") ++hearth;
        if (kind == "bed_slot") ++bed;
        EXPECT_TRUE(a.at("p").is_array());
        EXPECT_TRUE(a.at("dir").is_array());
        EXPECT_GT(a.at("kind_code").get<int>(), 0);
    }
    EXPECT_EQ(entrance, 1);
    EXPECT_EQ(hearth, 2);
    EXPECT_EQ(bed, 3);
}

TEST(BuildingFill, EmitSceneDeterministic) {
    const building_topology_generator::Ir ir = loadFrozen();
    const std::string a = building_geometry_generator::emit_scene_pgg(ir, fillParams());
    const std::string b = building_geometry_generator::emit_scene_pgg(ir, fillParams());
    EXPECT_EQ(a, b);
    EXPECT_NE(a.find("param cut_y"), std::string::npos);
    EXPECT_NE(a.find("output mesh"), std::string::npos);
}

}  // namespace
