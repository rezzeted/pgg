#pragma once

// village_geometry_generator (S6): Layout IR -> PGG building assets ->
// merged world-frame mesh (docs/village_topology_generator/requirements.md
// §7-§8). Each IR building instantiates `<assets_dir>/<type>.pgg`
// (fallback: house.pgg) with bindings w/d (parcel footprint, meters) and
// rng_seed; the asset contract: facade +Z, base y=0, output `mesh`.
// Map meters (x, y-down) map to world (x, 0, z); ground is flat y=0 in v1.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "pgg/eval.h"

namespace village_geometry_generator {

struct GeomParams {
    std::string assets_dir;               // content/pgg/Village/buildings
    std::vector<std::string> import_roots;  // + content/pgg (lib.*)
    uint64_t seed = 1;
};

struct GeomResult {
    pgg::GeoPtr mesh;  // merged world-frame mesh; null when nothing built
    std::vector<std::string> diagnostics;  // human-readable problems/warnings
    int buildings_total = 0;
    int buildings_built = 0;
};

// Deterministic per-building seed mix (splitmix64 of seed and building id).
uint64_t unit_seed(uint64_t seed, int64_t building_id);

// Rotate a mesh around Y (degrees), then translate by (tx, 0, tz).
// Positions and Point attrs get the full affine; Vector/Normal attrs rotate.
pgg::GeoPtr transform_geo(const pgg::GeoPtr& geo, double yaw_deg, double tx, double tz);

// Merge meshes into one (positions/normals/faces reindexed; attr and group
// columns unioned, missing entries zero-filled).
pgg::GeoPtr merge_meshes(const std::vector<pgg::GeoPtr>& parts);

// Run every building of the Layout IR through its PGG asset and merge.
GeomResult generate_geometry(const nlohmann::json& ir, const GeomParams& params);

// Emit the IR as a PGG scene source: one def call per building
// (Village.buildings_lib) placed by transform. Open the result in PggViewer
// for a 3D view, or run it with `PggTool run scene.pgg --lib content/pgg`.
// Deterministic for a deterministic IR. Empty when there are no buildings.
std::string emit_scene_pgg(const nlohmann::json& ir, uint64_t seed);

}  // namespace village_geometry_generator
