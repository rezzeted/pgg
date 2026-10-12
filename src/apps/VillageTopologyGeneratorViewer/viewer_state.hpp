#pragma once

// Shared viewer state (main/UI thread only) + the app log.

#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "analysis.h"
#include "anchors.h"
#include "layout_ir.h"
#include "parcels.h"
#include "placement.h"
#include "roads.h"
#include "terrain.h"

namespace vv {

// --- app log ---
inline std::vector<std::string> g_app_log;
inline constexpr size_t k_app_log_max_lines = 2000;

inline void app_log_clear() { g_app_log.clear(); }

inline void app_log_push(const std::string& line) {
    g_app_log.push_back(line);
    while (g_app_log.size() > k_app_log_max_lines) {
        g_app_log.erase(g_app_log.begin());
    }
}

inline void app_log_push_fmt(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    app_log_push(std::string(buf));
}

// --- inputs ---
inline village_topology_generator::TerrainParams g_terrain_params;
inline village_topology_generator::SkeletonParams g_skeleton_params;
inline village_topology_generator::ParcelParams g_parcel_params;
inline village_topology_generator::PlacementParams g_placement_params;
inline int g_house_count = 6;
inline int g_style = 0;  // 0 = radial, 1 = trunk

// --- layer toggles ---
inline bool g_show_terrain = true;
inline bool g_show_attractiveness = false;
inline bool g_show_slope = false;
inline bool g_show_roads = true;
inline bool g_show_parcels = true;
inline bool g_show_buildings = true;
inline bool g_show_anchors = true;

// --- pipeline outputs (valid when g_generated) ---
inline bool g_generated = false;
inline village_topology_generator::Terrain g_terrain;
inline village_topology_generator::Analysis g_analysis;
inline std::pair<int, int> g_center{0, 0};
inline std::vector<village_topology_generator::Anchor> g_anchors;
inline village_topology_generator::RoadNetwork g_network;
inline std::vector<village_topology_generator::Parcel> g_parcels;
inline village_topology_generator::PlacementResult g_placement;
inline nlohmann::json g_ir;
inline std::string g_fingerprint;
inline double g_last_ms = 0.0;
inline uint64_t g_generation_version = 0;  // bumped by run_generation (texture rebuild)

// Run V1..V6 with the current inputs; fills the outputs above, logs timings.
void run_generation();

}  // namespace vv
