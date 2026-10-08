#pragma once

#include "preset_loader.hpp"

#include "dungeon_topology_generator/dungeon_topology_generator.hpp"

#include <vector>

namespace ls {

extern dungeon_topology_generator::generator::grid2d::PresetCatalog g_catalog;
extern bool g_catalog_loaded;
extern int g_selected_preset;

extern dungeon_topology_generator::generator::grid2d::GraphBasedGeneratorConfiguration g_gen_config;

extern std::vector<dungeon_topology_generator::generator::grid2d::LayoutGrid2D<int>> g_layouts;
extern int g_layout_index;
extern bool g_use_random_seed;
extern int g_seed;
extern int g_num_layouts;
extern double g_last_time_ms;
extern int g_last_iterations;
extern int g_last_rooms;

extern bool g_compute_doors;
extern bool g_export_pending;

/// Per-layout early-stop wall-time budget in milliseconds (0 = unlimited). Guards the UI from
/// freezing forever on dense maps; generation returns the best partial layout when exceeded.
extern int g_time_budget_ms;

extern char g_resources_path[1024];
extern bool g_catalog_from_argv;

void reload_catalog_from_resources_dir(const std::string& dir);
void reload_catalog_from_map_file(const std::string& map_path);
/// Resets resource root to <repo>/resources/dungeon_topology_generator_gui (walk up from executable) and reloads catalog.
void reload_catalog_from_default_resources();

void generate_from_preset(int preset_idx, unsigned rng_seed);
void generate_hardcoded(unsigned rng_seed);

} // namespace ls
