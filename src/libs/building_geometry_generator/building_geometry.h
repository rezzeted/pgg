#pragma once

// building_geometry_generator (B0): Building IR -> PGG slot assets ->
// merged world-frame mesh + anchors (docs/building_generator/requirements.md
// section 11). Each IR unit instantiates its slot asset under
// content/pgg/Building (slot contract: mesh + anchors outputs, B0 anchors
// stay empty — the anchors.json comes from IrAnchor). Placement math
// (yaw/translate, local frames) lives here; the assets stay frame-free.
// Own copy of the DGG/Village unit-runner pattern per requirements
// section 18 decision 4 (shared runner after the schema stabilizes, ~B6).

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "pgg/eval.h"

#include "codes.h"
#include "ir.h"

namespace building_geometry_generator {

struct FillParams {
    std::string assets_dir;                 // content/pgg/Building
    std::vector<std::string> import_roots;  // + content/pgg (Building.*, lib.*)
    uint64_t seed = 1;
};

struct FillUnit {
    std::string id;    // IR entity id ("w_g_003", "slab:ground>upper", ...)
    std::string slot;  // asset path used
    pgg::GeoPtr mesh;  // world-frame mesh (null when the unit failed)
};

struct FillResult {
    pgg::GeoPtr mesh;  // merged world-frame mesh; null when nothing built
    std::vector<FillUnit> units;
    std::vector<std::string> diagnostics;  // human-readable problems/warnings
    int units_total = 0;
    int units_built = 0;
};

// §14.2 geometric checks, mesh- and IR-level. `failures` are hard violations
// (each names the rule and the entity), `notes` list the checks that are not
// applicable in B0 (facings) so the report stays honest.
struct CheckResult {
    std::vector<std::string> failures;
    std::vector<std::string> notes;
    bool ok() const { return failures.empty(); }
};

// Deterministic per-unit seed mix (splitmix64 over seed + FNV-1a of unit id).
uint64_t unit_seed(uint64_t seed, const std::string& unit_id);

// Rotate a mesh around Y (degrees), then translate by (tx, ty, tz).
// Positions and Point attrs get the full affine; Vector/Normal attrs rotate.
pgg::GeoPtr transform_geo(const pgg::GeoPtr& geo, double yaw_deg, double tx, double ty,
                          double tz);

// Merge meshes into one (positions/normals/faces reindexed; attr and group
// columns unioned, missing entries zero-filled).
pgg::GeoPtr merge_meshes(const std::vector<pgg::GeoPtr>& parts);

// Run every unit of the IR through its PGG asset and merge (world frame).
FillResult fill_building(const building_topology_generator::Ir& ir, const FillParams& params);

// §14.2 (B0 subset): stair riser/going norms and slab opening coverage,
// chimney height rule and tile clearance, gable walls below the roof planes.
CheckResult check_building(const building_topology_generator::Ir& ir, const FillResult& fill);

// anchors.json content (IrAnchor passthrough, quantized like the IR).
nlohmann::json anchors_json(const building_topology_generator::Ir& ir);

// Emit the IR as a PGG scene source: one builder call per unit (Building.lib)
// placed by transform, plus a `cut_y` param clipping everything above the
// mark (the storey section view, requirements section 15). Deterministic for
// a deterministic IR. Import root: content/pgg.
std::string emit_scene_pgg(const building_topology_generator::Ir& ir,
                           const FillParams& params);

}  // namespace building_geometry_generator
