// Layout generation benchmark: loads a YAML map from resources/dungeon_topology_generator_gui (or a path given via
// argv) and measures generation time over N seeds. Prints a single summary line:
//   benchmark map=<file> iterations=<n> min_ms=<..> median_ms=<..> max_ms=<..> rooms=<..>
// Exit code 1 when --threshold-ms <ms> is set and the median exceeds it (regression gate).
//
// Replaces the Windows-only tools/benchmark_layout_generation.ps1 for CI/local checks.

#include "preset_loader.hpp"

#include "dungeon_topology_generator/generator/grid2d/graph_based_generator_configuration.hpp"
#include "dungeon_topology_generator/generator/grid2d/graph_based_generator_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/level_description_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/layout_orchestration.hpp"
#include "dungeon_topology_generator/generator/grid2d/manual_door_mode_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/simple_door_mode_grid2d.hpp"
#include "dungeon_topology_generator/geometry/overlap.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace grid2d = dungeon_topology_generator::generator::grid2d;

fs::path repo_root_from_this_file() {
    return fs::path(__FILE__).parent_path().parent_path().parent_path();
}

void usage() {
    std::fprintf(stderr,
                 "usage: benchmark_layout [--map <name.yml>] [--iterations N] [--threshold-ms MS]\n"
                 "                        [--budget-ms MS] [--stats] [--dump-scenario out.json]\n"
                 "  --map            file inside resources/dungeon_topology_generator_gui/Maps (default: 9vertices.yml)\n"
                 "  --iterations     number of generations, seeds 1..N (default: 20)\n"
                 "  --threshold-ms   fail when median generation time exceeds MS\n"
                 "  --budget-ms      early-stop budget per generation (0 = none, default)\n"
                 "  --stats          print SA event counters (restarts, perturbed, valid, partial_valid)\n"
                 "  --dump-scenario  write the built level as a parity scenario JSON and exit\n");
}

// Full-fidelity scenario dump for tools/parity_runner_cs: per-room template pools with polygon
// outlines (post-scale) and door modes, so the C# reference runs the *identical* level the
// C++ side builds from a YAML preset.
void dump_scenario(const fs::path& out_path, const grid2d::PresetMap& map,
                   const grid2d::PresetCatalog& catalog) {
    const grid2d::LevelDescriptionGrid2D<int> level = grid2d::build_level_from_preset(map, catalog);

    nlohmann::json templates_by_room = nlohmann::json::object();
    for (const auto& [room, desc] : level.rooms()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : desc.room_templates()) {
            nlohmann::json points = nlohmann::json::array();
            for (const auto& p : t.outline().points()) {
                points.push_back({p.x, p.y});
            }
            nlohmann::json entry{{"points", points}};
            if (const auto* simple = dynamic_cast<const grid2d::SimpleDoorModeGrid2D*>(&t.doors())) {
                entry["simple_doors"] = {{"door_length", simple->door_length()},
                                         {"corner_distance", simple->corner_distance()}};
            } else if (const auto* manual = dynamic_cast<const grid2d::ManualDoorModeGrid2D*>(&t.doors())) {
                nlohmann::json doors = nlohmann::json::array();
                for (const auto& d : manual->doors()) {
                    doors.push_back({{"from", {d.from.x, d.from.y}}, {"to", {d.to.x, d.to.y}}});
                }
                entry["manual_doors"] = doors;
            }
            arr.push_back(entry);
        }
        templates_by_room[std::to_string(room)] = arr;
    }

    nlohmann::json rooms = nlohmann::json::array();
    for (const auto& [room, desc] : level.rooms()) {
        rooms.push_back({{"id", room}, {"corridor", desc.is_corridor()}});
    }
    nlohmann::json connections = nlohmann::json::array();
    const auto graph = level.get_graph();
    for (const int v : graph.vertices()) {
        for (const int n : graph.neighbours(v)) {
            if (v < n) {
                connections.push_back({v, n});
            }
        }
    }

    const nlohmann::json out = {{"name", map.filename},     {"seed", 1},
                                {"schema", "full_level"},   {"rooms", rooms},
                                {"connections", connections}, {"templates_by_room", templates_by_room}};
    std::ofstream(out_path) << out.dump(2);
    std::printf("dumped scenario to %s\n", out_path.string().c_str());
}

} // namespace

int main(int argc, char** argv) {
    std::string map_filename = "9vertices.yml";
    int iterations = 20;
    double threshold_ms = -1.0;
    int budget_ms = 0;
    bool stats = false;
    std::string dump_path;
    std::string repeat_override;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--map" && i + 1 < argc) {
            map_filename = argv[++i];
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--threshold-ms" && i + 1 < argc) {
            threshold_ms = std::atof(argv[++i]);
        } else if (arg == "--budget-ms" && i + 1 < argc) {
            budget_ms = std::atoi(argv[++i]);
        } else if (arg == "--stats") {
            stats = true;
        } else if (arg == "--dump-scenario" && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (arg == "--repeat-override" && i + 1 < argc) {
            repeat_override = argv[++i];
        } else if (arg == "--help") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    const fs::path resources = repo_root_from_this_file() / "resources" / "dungeon_topology_generator_gui";
    if (!fs::exists(resources)) {
        std::fprintf(stderr, "missing resources root: %s\n", resources.string().c_str());
        return 2;
    }

    auto loaded = grid2d::load_preset_catalog_with_status(resources.string());
    if (!loaded.error.empty() || loaded.catalog.maps.empty()) {
        std::fprintf(stderr, "catalog load error: %s\n", loaded.error.c_str());
        return 2;
    }

    const grid2d::PresetMap* map = nullptr;
    for (const auto& m : loaded.catalog.maps) {
        if (m.filename == map_filename) {
            map = &m;
            break;
        }
    }
    if (map == nullptr) {
        std::fprintf(stderr, "map not found in catalog: %s\n", map_filename.c_str());
        return 2;
    }

    if (!dump_path.empty()) {
        dump_scenario(dump_path, *map, loaded.catalog);
        return 0;
    }

    std::vector<double> times_ms;
    times_ms.reserve(static_cast<std::size_t>(iterations));
    std::size_t rooms = 0;
    int restarts = 0;
    int overlapping_layouts = 0;
    int perturbed = 0;
    int valid = 0;
    int partial_valid = 0;
    for (int i = 0; i < iterations; ++i) {
        grid2d::LevelDescriptionGrid2D<int> level = grid2d::build_level_from_preset(*map, loaded.catalog);
        if (!repeat_override.empty()) {
            if (repeat_override == "allow") {
                level.room_template_repeat_mode_override = dungeon_topology_generator::generator::RoomTemplateRepeatMode::AllowRepeat;
            } else if (repeat_override == "no-immediate") {
                level.room_template_repeat_mode_override = dungeon_topology_generator::generator::RoomTemplateRepeatMode::NoImmediate;
            } else if (repeat_override == "no-repeat") {
                level.room_template_repeat_mode_override = dungeon_topology_generator::generator::RoomTemplateRepeatMode::NoRepeat;
            } else {
                std::fprintf(stderr, "unknown repeat override: %s\n", repeat_override.c_str());
                return 2;
            }
        }
        grid2d::GraphBasedGeneratorConfiguration config{};
        if (budget_ms > 0) {
            config.early_stop_max_elapsed = std::chrono::milliseconds(budget_ms);
        }
        grid2d::GraphBasedGeneratorGrid2D<int> generator(level, config);
        std::mt19937 rng(static_cast<unsigned>(i + 1));

        const auto t0 = std::chrono::steady_clock::now();
        generator.inject_random_generator(std::move(rng));
        if (stats) {
            generator.set_on_simulated_annealing_event([&restarts](const grid2d::LayoutYieldInfo& info) {
                if (info.event_type == grid2d::LayoutYieldEvent::RandomRestart) {
                    ++restarts;
                }
            });
            generator.set_on_perturbed([&perturbed](const grid2d::LayoutGrid2D<int>&) { ++perturbed; });
            generator.set_on_valid([&valid](const grid2d::LayoutGrid2D<int>&) { ++valid; });
            generator.set_on_partial_valid([&partial_valid](const grid2d::LayoutGrid2D<int>&) { ++partial_valid; });
        }
        const auto layout = generator.generate_layout();
        const auto t1 = std::chrono::steady_clock::now();

        times_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        rooms = layout.rooms.size();
        if (stats) {
            // Validity check: no pairwise interior overlap
            bool any_overlap = false;
            for (std::size_t a = 0; a < layout.rooms.size() && !any_overlap; ++a) {
                for (std::size_t b = a + 1; b < layout.rooms.size(); ++b) {
                    if (dungeon_topology_generator::geometry::polygons_overlap_area(layout.rooms[a].outline, layout.rooms[a].position,
                                                               layout.rooms[b].outline, layout.rooms[b].position)) {
                        any_overlap = true;
                        break;
                    }
                }
            }
            if (any_overlap) {
                ++overlapping_layouts;
            }
        }
    }

    std::sort(times_ms.begin(), times_ms.end());
    const double median = times_ms[times_ms.size() / 2];
    std::printf("benchmark map=%s iterations=%d min_ms=%.3f median_ms=%.3f max_ms=%.3f rooms=%zu\n",
                map_filename.c_str(), iterations, times_ms.front(), median, times_ms.back(), rooms);
    if (stats) {
        std::printf("stats restarts=%d perturbed=%d valid=%d partial_valid=%d overlapping=%d\n", restarts,
                    perturbed, valid, partial_valid, overlapping_layouts);
    }

    if (threshold_ms >= 0.0 && median > threshold_ms) {
        std::fprintf(stderr, "REGRESSION: median %.3f ms exceeds threshold %.3f ms\n", median, threshold_ms);
        return 1;
    }
    return 0;
}
