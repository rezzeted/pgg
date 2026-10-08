// Golden parity harness (C++ side): runs scenarios from test_data/parity/scenarios through
// the C++ generator, dumps the logical layout structure to test_data/parity/actual/*.cpp.json
// and, when the reference C# output (*.cs.json, produced by tools/parity_runner_cs) is present,
// checks both engines against the same logical invariants (room counts, no overlaps, every
// graph connection realized by a door, outlines only from allowed template pools).
//
// Byte-level equality of layouts is NOT required: RNG streams differ between the ports.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "dungeon_topology_generator/generator/grid2d/graph_based_generator_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/layout_door_computation.hpp"
#include "dungeon_topology_generator/generator/grid2d/level_description_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/simple_door_mode_grid2d.hpp"
#include "dungeon_topology_generator/geometry/polygon_overlap_grid2d.hpp"

#ifndef PARITY_DATA_DIR
#define PARITY_DATA_DIR "test_data/parity"
#endif

namespace {

using dungeon_topology_generator::generator::RoomTemplateRepeatMode;
using dungeon_topology_generator::generator::grid2d::GraphBasedGeneratorGrid2D;
using dungeon_topology_generator::generator::grid2d::LayoutGrid2D;
using dungeon_topology_generator::generator::grid2d::LevelDescriptionGrid2D;
using dungeon_topology_generator::generator::grid2d::RoomDescriptionGrid2D;
using dungeon_topology_generator::generator::grid2d::RoomTemplateGrid2D;
using dungeon_topology_generator::generator::grid2d::SimpleDoorModeGrid2D;
using dungeon_topology_generator::geometry::PolygonGrid2D;
using dungeon_topology_generator::geometry::TransformationGrid2D;
using dungeon_topology_generator::geometry::Vector2Int;
using json = nlohmann::json;

struct TemplateSpec {
    int width;
    int height;
    int door_length;
    int corner_distance;
};

struct Scenario {
    std::string name;
    int seed = 0;
    int expected_rooms = 0;
    int expected_corridors = 0;
    std::vector<TemplateSpec> templates;
    std::vector<TemplateSpec> corridor_templates;
    std::vector<std::pair<int, bool>> rooms; // id, is_corridor
    std::vector<std::pair<int, int>> connections;
};

TemplateSpec parse_template(const json& t) {
    return TemplateSpec{t.at("rect")[0].get<int>(), t.at("rect")[1].get<int>(), t.at("door_length").get<int>(),
                        t.at("corner_distance").get<int>()};
}

Scenario load_scenario(const std::filesystem::path& path) {
    const json j = json::parse(std::ifstream(path));
    Scenario s;
    s.name = j.at("name").get<std::string>();
    s.seed = j.at("seed").get<int>();
    s.expected_rooms = j.at("expected_rooms").get<int>();
    s.expected_corridors = j.at("expected_corridors").get<int>();
    for (const auto& t : j.at("templates")) {
        s.templates.push_back(parse_template(t));
    }
    if (j.contains("corridor_templates")) {
        for (const auto& t : j.at("corridor_templates")) {
            s.corridor_templates.push_back(parse_template(t));
        }
    }
    for (const auto& r : j.at("rooms")) {
        s.rooms.emplace_back(r.at("id").get<int>(), r.value("corridor", false));
    }
    for (const auto& c : j.at("connections")) {
        s.connections.emplace_back(c[0].get<int>(), c[1].get<int>());
    }
    return s;
}

std::vector<TransformationGrid2D> all_transformations() {
    return {TransformationGrid2D::Identity,  TransformationGrid2D::Rotate90, TransformationGrid2D::Rotate180,
            TransformationGrid2D::Rotate270, TransformationGrid2D::MirrorX,  TransformationGrid2D::MirrorY,
            TransformationGrid2D::Diagonal13, TransformationGrid2D::Diagonal24};
}

RoomTemplateGrid2D make_template(const TemplateSpec& spec, const std::string& name) {
    return RoomTemplateGrid2D(PolygonGrid2D::get_rectangle(spec.width, spec.height),
                              std::make_shared<SimpleDoorModeGrid2D>(spec.door_length, spec.corner_distance),
                              name, std::nullopt, all_transformations());
}

LevelDescriptionGrid2D<int> build_level(const Scenario& s) {
    std::vector<RoomTemplateGrid2D> basic;
    for (std::size_t i = 0; i < s.templates.size(); ++i) {
        basic.push_back(make_template(s.templates[i], "basic" + std::to_string(i)));
    }
    std::vector<RoomTemplateGrid2D> corridor;
    for (std::size_t i = 0; i < s.corridor_templates.size(); ++i) {
        corridor.push_back(make_template(s.corridor_templates[i], "corridor" + std::to_string(i)));
    }

    RoomDescriptionGrid2D basic_desc(false, basic);
    LevelDescriptionGrid2D<int> level;
    for (const auto& [id, is_corridor] : s.rooms) {
        if (is_corridor) {
            // C# CorridorRoomDescription is always stage 2
            level.add_room(id, RoomDescriptionGrid2D(true, corridor, 2));
        } else {
            level.add_room(id, basic_desc);
        }
    }
    for (const auto& [a, b] : s.connections) {
        level.add_connection(a, b);
    }
    return level;
}

LayoutGrid2D<int> generate_layout(const Scenario& s, const LevelDescriptionGrid2D<int>& level) {
    GraphBasedGeneratorGrid2D<int> generator(level);
    std::mt19937 rng(static_cast<unsigned>(s.seed));
    generator.inject_random_generator(std::move(rng));
    auto layout = generator.generate_layout();
    std::mt19937 door_rng(static_cast<unsigned>(s.seed) + 1u);
    dungeon_topology_generator::generator::grid2d::compute_layout_doors(layout, level, level.get_graph(), door_rng);
    return layout;
}

json dump_layout(const Scenario& s, const LayoutGrid2D<int>& layout) {
    json rooms = json::array();
    for (const auto& room : layout.rooms) {
        json outline = json::array();
        for (const auto& p : room.outline.points()) {
            outline.push_back({p.x + room.position.x, p.y + room.position.y});
        }
        json doors = json::array();
        for (const auto& d : room.doors) {
            doors.push_back({{"from", d.from_room},
                             {"to", d.to_room},
                             {"line", {{d.door_line.from.x, d.door_line.from.y},
                                       {d.door_line.to.x, d.door_line.to.y}}}});
        }
        rooms.push_back({{"id", room.room},
                         {"is_corridor", room.is_corridor},
                         {"position", {room.position.x, room.position.y}},
                         {"outline", outline},
                         {"doors", doors}});
    }
    return json{{"scenario", s.name}, {"seed", s.seed}, {"engine", "cpp"}, {"rooms", rooms}};
}

// --- Logical layout view shared by both engines ---------------------------------------------

struct RoomView {
    int id = 0;
    bool is_corridor = false;
    PolygonGrid2D absolute_outline;
};

std::vector<RoomView> views_from_json(const json& layout_json) {
    std::vector<RoomView> out;
    for (const auto& r : layout_json.at("rooms")) {
        std::vector<Vector2Int> points;
        for (const auto& p : r.at("outline")) {
            points.emplace_back(p[0].get<int>(), p[1].get<int>());
        }
        out.push_back(RoomView{r.at("id").get<int>(), r.at("is_corridor").get<bool>(), PolygonGrid2D(points)});
    }
    return out;
}

std::set<std::pair<int, int>> door_pairs_from_json(const json& layout_json) {
    std::set<std::pair<int, int>> pairs;
    for (const auto& r : layout_json.at("rooms")) {
        if (!r.contains("doors")) {
            continue;
        }
        for (const auto& d : r.at("doors")) {
            const int a = d.at("from").get<int>();
            const int b = d.at("to").get<int>();
            pairs.insert({std::min(a, b), std::max(a, b)});
        }
    }
    return pairs;
}

bool dims_in_pool(const RoomView& room, const std::vector<TemplateSpec>& pool) {
    const auto bbox = room.absolute_outline.bounding_rectangle();
    const int w = bbox.b.x - bbox.a.x;
    const int h = bbox.b.y - bbox.a.y;
    for (const auto& t : pool) {
        if ((t.width == w && t.height == h) || (t.width == h && t.height == w)) {
            return true;
        }
    }
    return false;
}

void expect_layout_valid(const std::vector<RoomView>& rooms, const std::set<std::pair<int, int>>& door_pairs,
                         const Scenario& s, const std::string& engine_label) {
    ASSERT_EQ(static_cast<int>(rooms.size()), s.expected_rooms) << engine_label;
    int corridors = 0;
    for (const auto& room : rooms) {
        if (room.is_corridor) {
            ++corridors;
            EXPECT_TRUE(dims_in_pool(room, s.corridor_templates))
                << engine_label << ": corridor room " << room.id << " outline not from corridor pool";
        } else {
            EXPECT_TRUE(dims_in_pool(room, s.templates))
                << engine_label << ": room " << room.id << " outline not from basic pool";
        }
    }
    EXPECT_EQ(corridors, s.expected_corridors) << engine_label;

    for (std::size_t i = 0; i < rooms.size(); ++i) {
        for (std::size_t j = i + 1; j < rooms.size(); ++j) {
            EXPECT_FALSE(dungeon_topology_generator::geometry::polygons_overlap_area(rooms[i].absolute_outline, {0, 0},
                                                                rooms[j].absolute_outline, {0, 0}))
                << engine_label << ": rooms " << rooms[i].id << " and " << rooms[j].id << " overlap";
        }
    }

    for (const auto& [a, b] : s.connections) {
        EXPECT_TRUE(door_pairs.count({std::min(a, b), std::max(a, b)}))
            << engine_label << ": no door for connection " << a << "-" << b;
    }
}

} // namespace

class DungeonTopologyGeneratorGoldenParity : public testing::TestWithParam<std::string> {};

TEST_P(DungeonTopologyGeneratorGoldenParity, Scenario_GeneratesAndMatchesReferenceLogically) {
    const std::filesystem::path data_dir = PARITY_DATA_DIR;
    const auto scenario = load_scenario(data_dir / "scenarios" / (GetParam() + ".json"));

    const auto level = build_level(scenario);
    const auto layout = generate_layout(scenario, level);
    const auto cpp_json = dump_layout(scenario, layout);

    const auto actual_dir = data_dir / "actual";
    std::filesystem::create_directories(actual_dir);
    std::ofstream(actual_dir / (scenario.name + ".cpp.json")) << cpp_json.dump(2);

    // C++ layout must satisfy the invariants on its own
    expect_layout_valid(views_from_json(cpp_json), door_pairs_from_json(cpp_json), scenario, "cpp");

    const auto cs_path = actual_dir / (scenario.name + ".cs.json");
    if (!std::filesystem::exists(cs_path)) {
        GTEST_SKIP() << "reference C# output missing (run tools/parity_runner_cs first): " << cs_path;
    }
    const auto cs_json = json::parse(std::ifstream(cs_path));

    // The reference layout must satisfy the same logical invariants
    expect_layout_valid(views_from_json(cs_json), door_pairs_from_json(cs_json), scenario, "csharp");

    // Cross-engine logical agreement: room/corridor counts (layout equality is RNG-dependent)
    int cs_corridors = 0;
    for (const auto& r : cs_json.at("rooms")) {
        if (r.at("is_corridor").get<bool>()) {
            ++cs_corridors;
        }
    }
    EXPECT_EQ(static_cast<int>(cs_json.at("rooms").size()), scenario.expected_rooms);
    EXPECT_EQ(cs_corridors, scenario.expected_corridors);
}

INSTANTIATE_TEST_SUITE_P(Parity, DungeonTopologyGeneratorGoldenParity,
                         testing::Values("four_room_cycle", "three_room_corridor_line", "six_room_star"));
