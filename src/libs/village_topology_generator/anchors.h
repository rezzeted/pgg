#pragma once

// V3 Anchors: settlement center, waterfront site, edge ("roads to the world")
// anchors (docs/village_topology_generator/requirements.md §7).

#include <optional>
#include <utility>
#include <vector>

#include "analysis.h"
#include "terrain.h"

namespace village_topology_generator {

enum class AnchorKind { center, water_site, edge };

struct Anchor {
    AnchorKind kind = AnchorKind::center;
    std::pair<int, int> cell{0, 0};
    double score = 0.0;  // attractiveness at pick time
};

// Best attractiveness with a falloff toward map borders: full score inside
// the core region (quarter-map margin), fading to 0 at the border.
// Deterministic: first max in row-major scan order. Falls back to the map
// middle when nothing is buildable.
std::pair<int, int> pick_center(const Analysis& analysis);

// Land cell adjacent to water, nearest to `center` (mill/dock site).
// Tie-break: higher attractiveness, then scan order. nullopt when the map
// has no waterfront.
std::optional<std::pair<int, int>> pick_water_site(const Terrain& terrain,
                                                   const Analysis& analysis,
                                                   std::pair<int, int> center);

// Best-attractiveness land cell on each border — where roads leave the map.
// Tie-break: closest to the side's middle. Sides without a buildable border
// cell are skipped. Order: N, S, W, E.
std::vector<Anchor> pick_edge_anchors(const Terrain& terrain, const Analysis& analysis);

}  // namespace village_topology_generator
