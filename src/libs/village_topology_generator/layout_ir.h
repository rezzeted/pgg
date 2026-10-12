#pragma once

// Layout IR: versioned JSON output of the pipeline
// (docs/village_topology_generator/requirements.md §8). Deterministic
// canonical dump + FNV-1a fingerprint for golden tests (N1, N3).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "anchors.h"
#include "parcels.h"
#include "placement.h"
#include "roads.h"
#include "terrain.h"

namespace village_topology_generator {

inline constexpr const char* kLayoutFormat = "village-topology-generator-layout/0";

// Assemble the Layout IR document. Roads/parcels/buildings are emitted in
// metric coordinates (§5); the terrain section carries generation params,
// not the heightfield itself. `zones` is an empty placeholder in v1.
nlohmann::json build_layout_ir(const TerrainParams& terrain_params,
                               const RoadNetwork& network,
                               const std::vector<Parcel>& parcels,
                               const PlacementResult& placement,
                               const std::vector<Anchor>& anchors);

// Canonical text: sorted keys (nlohmann default), no whitespace.
std::string canonical_layout_json(const nlohmann::json& ir);

// FNV-1a 64 over the canonical text — the fingerprint stored by goldens.
uint64_t layout_fingerprint(const nlohmann::json& ir);

std::string fingerprint_hex(uint64_t fingerprint);

}  // namespace village_topology_generator
