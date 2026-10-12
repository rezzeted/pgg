#pragma once

// V6 Placement: roster model and constraint-based assignment of buildings
// to parcels (docs/village_topology_generator/requirements.md §4.2, §7).
// Backtracking search, most-constrained-first order, deterministic.

#include <string>
#include <utility>
#include <vector>

#include "analysis.h"
#include "parcels.h"
#include "roads.h"
#include "terrain.h"

namespace village_topology_generator {

// Site requirement predicates (§4.2). Distances in meters (§5).
enum class SiteReq {
    near_water,   // parcel within water_near of water
    near_forest,  // parcel within forest_near of forest
    near_center,  // parcel centroid within center_near of the settlement center
    on_hill,      // parcel centroid at or above the center's terrain height
};

struct RosterItem {
    std::string type;                 // "house", "mill", "bakery", ...
    std::vector<SiteReq> site;        // all must hold on the assigned parcel
    std::vector<std::string> requires_types;  // "requires:<type>" links (§4.2)
};

using Roster = std::vector<RosterItem>;

struct PlacementParams {
    double water_near = 20.0;   // meters
    double forest_near = 20.0;  // meters
    double center_near = 40.0;  // meters
};

struct PlacedBuilding {
    int roster_index = -1;
    std::string type;
    int parcel_index = -1;
    std::pair<int, int> cell;    // middle cell of the parcel front row
    std::pair<double, double> facing{0.0, 0.0};  // unit vector towards the road
};

struct PlacementResult {
    std::vector<PlacedBuilding> buildings;  // roster order
    std::vector<int> unplaced;              // roster indices without a parcel
    bool complete = false;
};

// Static roster check: every requires-link must name a type present in the
// roster itself. Returns human-readable violations (empty = ok).
std::vector<std::string> validate_roster(const Roster& roster);

// Indices of parcels satisfying the item's site requirements, ascending.
std::vector<int> candidate_parcels(const Terrain& terrain,
                                   const Analysis& analysis,
                                   const std::vector<Parcel>& parcels,
                                   const RosterItem& item,
                                   std::pair<int, int> center,
                                   const PlacementParams& params = {});

// Assign each roster item a distinct parcel. Search order: fewest candidates
// first (ties by roster order); parcels tried in ascending index; first
// complete assignment wins. Items with zero candidates land in `unplaced`
// (the rest is still placed). Deterministic (N1).
PlacementResult place_roster(const Terrain& terrain,
                             const Analysis& analysis,
                             const std::vector<Parcel>& parcels,
                             const Roster& roster,
                             std::pair<int, int> center,
                             const PlacementParams& params = {});

// Post-placement checks (§7): requires-links resolve to placed buildings.
// Returns violations (empty = ok).
std::vector<std::string> validate_placement(const Roster& roster,
                                            const PlacementResult& result);

}  // namespace village_topology_generator
