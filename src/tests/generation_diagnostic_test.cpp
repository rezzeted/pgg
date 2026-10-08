// Generation trace: same pipeline as the app (GraphBasedGeneratorGrid2D + compute_layout_doors), seed 46.
// Logs are JSON Lines (one JSON object per line) on stderr via spdlog — easy for Cursor / scripts to filter.
// Default map: tutorial_basic.yml. Override: LEVELSYNTH_DIAG_MAP_FILENAME, LEVELSYNTH_DIAG_PRESET_INDEX.

#include <gtest/gtest.h>

#include "preset_loader.hpp"

#include "edgar/generator/grid2d/configuration_spaces_generator.hpp"
#include "edgar/generator/grid2d/graph_based_generator_configuration.hpp"
#include "edgar/generator/grid2d/graph_based_generator_grid2d.hpp"
#include "edgar/generator/grid2d/layout_door_computation.hpp"
#include "edgar/graphs/undirected_graph.hpp"
#include "edgar/geometry/overlap.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace {

namespace fs = std::filesystem;
using edgar::generator::grid2d::ConfigurationSpacesGenerator;
using edgar::generator::grid2d::LayoutGrid2D;
using edgar::generator::grid2d::LayoutRoomGrid2D;
using edgar::geometry::Vector2Int;
using edgar::graphs::UndirectedAdjacencyListGraph;

fs::path repo_root_from_this_file() {
    return fs::path(__FILE__).parent_path().parent_path().parent_path();
}

/// spdlog -> stderr, no timestamps; each message is one JSON line (NDJSON).
std::shared_ptr<spdlog::logger> make_jsonl_logger() {
    // ostream_sink stores a reference to std::cerr (valid for test lifetime).
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cerr, false);
    auto logger = std::make_shared<spdlog::logger>("generation_diag", sink);
    logger->set_pattern("%v");
    logger->set_level(spdlog::level::info);
    return logger;
}

void log_json(const std::shared_ptr<spdlog::logger>& L, const nlohmann::json& j) {
    L->info("{}", j.dump());
}

const LayoutRoomGrid2D<int>* find_layout_room(const LayoutGrid2D<int>& layout, int room_id) {
    for (const auto& r : layout.rooms) {
        if (r.room == room_id) {
            return &r;
        }
    }
    return nullptr;
}

void world_bbox(const LayoutRoomGrid2D<int>& r, int& min_x, int& min_y, int& max_x, int& max_y) {
    min_x = std::numeric_limits<int>::max();
    min_y = std::numeric_limits<int>::max();
    max_x = std::numeric_limits<int>::min();
    max_y = std::numeric_limits<int>::min();
    for (const auto& p : r.outline.points()) {
        const int wx = p.x + r.position.x;
        const int wy = p.y + r.position.y;
        min_x = std::min(min_x, wx);
        min_y = std::min(min_y, wy);
        max_x = std::max(max_x, wx);
        max_y = std::max(max_y, wy);
    }
}

void trace_door_pipeline(const std::shared_ptr<spdlog::logger>& L, const LayoutGrid2D<int>& layout,
                         const UndirectedAdjacencyListGraph<int>& graph) {
    using namespace edgar::generator::grid2d;
    ConfigurationSpacesGenerator cs_gen;
    std::unordered_set<int> processed;
    for (std::size_t ai = 0; ai < layout.rooms.size(); ++ai) {
        const auto& room_a = layout.rooms[ai];
        for (const int neighbour : graph.neighbours(room_a.room)) {
            if (processed.count(neighbour) != 0) {
                continue;
            }
            std::size_t bi = 0;
            for (; bi < layout.rooms.size(); ++bi) {
                if (layout.rooms[bi].room == neighbour) {
                    break;
                }
            }
            if (bi == layout.rooms.size()) {
                log_json(L, nlohmann::json{{"event", "door_pipeline.skip_missing_room"},
                                            {"neighbour", neighbour}});
                continue;
            }
            const auto& room_b = layout.rooms[bi];

            auto doors_a = room_a.room_template.doors().get_doors(room_a.outline);
            auto doors_b = room_b.room_template.doors().get_doors(room_b.outline);
            const auto cs = cs_gen.get_configuration_space(room_b.outline, doors_b, room_a.outline, doors_a);

            // Same as Edgar `BasicLayoutConverterGrid2D`: neighbour - vertex for CS(neighbour, vertex).
            const Vector2Int rel = room_b.position - room_a.position;
            auto choices = detail::door_choices_from_config_space(rel, cs);

            nlohmann::json j;
            j["event"] = "door_pipeline.edge";
            j["room_a"] = room_a.room;
            j["room_b"] = room_b.room;
            j["rel_dx"] = rel.x;
            j["rel_dy"] = rel.y;
            j["door_sockets_a"] = doors_a.size();
            j["door_sockets_b"] = doors_b.size();
            j["cs_lines"] = cs.lines.size();
            j["cs_reverse_doors"] = cs.reverse_doors.size();
            j["choices"] = choices.size();
            j["door_would_place"] = !choices.empty();
            log_json(L, j);
        }
        processed.insert(room_a.room);
    }
}

} // namespace

TEST(GenerationDiagnostic, Seed46_LogsGraphGeometryAndDoors) {
    namespace grid2d = edgar::generator::grid2d;
    const auto L = make_jsonl_logger();

    const fs::path resources = repo_root_from_this_file() / "resources" / "edgar_gui";
    ASSERT_TRUE(fs::exists(resources)) << "Missing " << resources.string();

    auto loaded = grid2d::load_preset_catalog_with_status(resources.string());
    ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    ASSERT_FALSE(loaded.catalog.maps.empty());

    int preset_index = -1;
    if (const char* ev = std::getenv("LEVELSYNTH_DIAG_PRESET_INDEX")) {
        preset_index = std::atoi(ev);
    }
    if (preset_index < 0) {
        const char* fn = std::getenv("LEVELSYNTH_DIAG_MAP_FILENAME");
        const std::string want = fn ? std::string(fn) : std::string("tutorial_basic.yml");
        for (std::size_t i = 0; i < loaded.catalog.maps.size(); ++i) {
            if (loaded.catalog.maps[i].filename == want) {
                preset_index = static_cast<int>(i);
                break;
            }
        }
    }
    if (preset_index < 0) {
        preset_index = 0;
    }
    preset_index = std::clamp(preset_index, 0, static_cast<int>(loaded.catalog.maps.size()) - 1);
    const auto& map = loaded.catalog.maps[static_cast<std::size_t>(preset_index)];

    constexpr unsigned k_seed = 46;
    grid2d::GraphBasedGeneratorConfiguration gen_config{};
    grid2d::LevelDescriptionGrid2D<int> level = grid2d::build_level_from_preset(map, loaded.catalog);
    const UndirectedAdjacencyListGraph<int> graph = level.get_graph();

    log_json(L, nlohmann::json{{"event", "generation_diag.schema"},
                                 {"version", 1},
                                 {"format", "ndjson"},
                                 {"note", "One JSON object per line; filter by .event field."}});

    log_json(L, nlohmann::json{{"event", "generation_diag.begin"},
                                 {"seed", k_seed},
                                 {"map_file", map.filename},
                                 {"display_name", map.display_name},
                                 {"preset_index", preset_index},
                                 {"room_from", map.room_from},
                                 {"room_to", map.room_to},
                                 {"passage_count", map.passages.size()},
                                 {"corridors_enabled", map.corridors_enabled}});

    log_json(L, nlohmann::json{{"event", "graph.meta"},
                                 {"vertex_count", static_cast<int>(graph.vertex_count())}});

    for (const int v : graph.vertices()) {
        nlohmann::json j;
        j["event"] = "graph.vertex";
        j["id"] = v;
        j["neighbours"] = nlohmann::json::array();
        for (int n : graph.neighbours(v)) {
            j["neighbours"].push_back(n);
        }
        log_json(L, j);
    }

    grid2d::GraphBasedGeneratorGrid2D<int> generator(level, gen_config);
    std::mt19937 rng(k_seed);
    generator.inject_random_generator(std::move(rng));
    LayoutGrid2D<int> layout = generator.generate_layout();

    log_json(L, nlohmann::json{{"event", "layout.meta"}, {"placed_room_count", layout.rooms.size()}});

    std::unordered_map<int, int> touching_neighbours_count;
    for (const auto& r : layout.rooms) {
        touching_neighbours_count[r.room] = 0;
    }

    for (const auto& r : layout.rooms) {
        int mn_x, mn_y, mx_x, mx_y;
        world_bbox(r, mn_x, mn_y, mx_x, mx_y);
        log_json(L, nlohmann::json{{"event", "layout.room"},
                                     {"room", r.room},
                                     {"pos_x", r.position.x},
                                     {"pos_y", r.position.y},
                                     {"bbox_min_x", mn_x},
                                     {"bbox_min_y", mn_y},
                                     {"bbox_max_x", mx_x},
                                     {"bbox_max_y", mx_y},
                                     {"is_corridor", r.is_corridor}});
    }

    for (const int v : graph.vertices()) {
        for (const int u : graph.neighbours(v)) {
            if (v >= u) {
                continue;
            }
            const auto* ra = find_layout_room(layout, v);
            const auto* rb = find_layout_room(layout, u);
            ASSERT_NE(ra, nullptr);
            ASSERT_NE(rb, nullptr);
            const bool touch = edgar::geometry::polygons_touch(ra->outline, ra->position, rb->outline, rb->position);
            const bool overlap =
                edgar::geometry::polygons_overlap_area(ra->outline, ra->position, rb->outline, rb->position);
            log_json(L, nlohmann::json{{"event", "geometry.edge"},
                                         {"u", v},
                                         {"v", u},
                                         {"polygons_touch", touch},
                                         {"polygons_overlap_area", overlap}});
            if (touch) {
                ++touching_neighbours_count[v];
                ++touching_neighbours_count[u];
            }
        }
    }

    nlohmann::json detached_list = nlohmann::json::array();
    for (const int v : graph.vertices()) {
        const int deg = static_cast<int>(graph.neighbours(v).size());
        const int t = touching_neighbours_count[v];
        const bool detached = (deg > 0 && t == 0);
        log_json(L, nlohmann::json{{"event", "room.connectivity"},
                                     {"room", v},
                                     {"graph_degree", deg},
                                     {"touching_neighbours", t},
                                     {"detached", detached}});
        if (detached) {
            detached_list.push_back(v);
        }
    }

    trace_door_pipeline(L, layout, graph);
    std::mt19937 door_rng(k_seed);
    grid2d::compute_layout_doors(layout, level, graph, door_rng);

    int total_doors = 0;
    for (const auto& r : layout.rooms) {
        total_doors += static_cast<int>(r.doors.size());
    }
    for (const auto& r : layout.rooms) {
        log_json(L, nlohmann::json{{"event", "doors.per_room"},
                                     {"room", r.room},
                                     {"door_count", r.doors.size()}});
    }

    log_json(L, nlohmann::json{{"event", "generation_diag.summary"},
                                 {"total_door_records", total_doors},
                                 {"detached_room_ids", detached_list}});

    log_json(L, nlohmann::json{{"event", "generation_diag.end"}, {"total_door_records", total_doors}});

    SUCCEED();
}
