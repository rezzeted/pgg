#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "preset_loader.hpp"

namespace {

std::filesystem::path repo_root_from_this_file() {
    // src/tests/preset_loader_tests.cpp -> repo root is three levels up
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

} // namespace

TEST(PresetLoader, MissingPath_returnsError) {
    using namespace edgar::generator::grid2d;
    auto r = load_preset_catalog_with_status("Z:/nonexistent/levelsynth_preset_test_42");
    EXPECT_FALSE(r.error.empty());
}

TEST(PresetLoader, TinyFixture_loads) {
    using namespace edgar::generator::grid2d;
    const std::filesystem::path base = repo_root_from_this_file() / "test_data" / "gui_presets";
    ASSERT_TRUE(std::filesystem::exists(base)) << base.string();

    auto r = load_preset_catalog_with_status(base.string());
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_FALSE(r.catalog.maps.empty());
    EXPECT_EQ(r.catalog.maps.front().display_name, "tiny");

    const auto level = build_level_from_preset(r.catalog.maps.front(), r.catalog);
    EXPECT_GE(level.get_graph().vertex_count(), 1);
}

/// Original Edgar maps use `rooms: [8]:` style keys; yaml-cpp exposes them as sequence keys, not strings.
TEST(PresetLoader, NineVertices_bracketRoomKey_parses) {
    using namespace edgar::generator::grid2d;
    const std::filesystem::path base = repo_root_from_this_file() / "resources" / "edgar_gui";
    if (!std::filesystem::exists(base)) {
        GTEST_SKIP() << "resources/edgar_gui not in tree";
    }

    auto r = load_preset_catalog_with_status(base.string());
    ASSERT_TRUE(r.error.empty()) << r.error;

    const PresetMap* map9 = nullptr;
    for (const auto& m : r.catalog.maps) {
        if (m.filename == "9vertices.yml") {
            map9 = &m;
            break;
        }
    }
    ASSERT_NE(map9, nullptr) << "9vertices.yml should load (bracket room key support)";
    bool has_room_8 = false;
    for (const auto& ov : map9->room_overrides) {
        for (int id : ov.room_ids) {
            if (id == 8) {
                has_room_8 = true;
            }
        }
    }
    EXPECT_TRUE(has_room_8);
    EXPECT_FALSE(map9->room_overrides.empty());
}
