#pragma once

// G6 (B2): openings and the access graph derived from a FloorPlan
// (algorithms §7): doors by Dijkstra from the street over pass-priced room
// adjacency (§7.1), windows by the daylight need on the facade bay axes
// (§7.2). Everything here is a pure function of the plan + the stickiness
// memo: after an anneal move the openings are re-derived, and the memo keeps
// the surviving positions stable (§7.1 "позиции липкие").

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "plan.h"

namespace building_topology_generator {

// One door or window on a wall line. The wall itself is rebuilt from the
// plan at emit time; here the opening anchors to (vertical, line) and the
// span along the wall in meters (building frame).
struct DerivedOpening {
    int kind_code = 0;  // kOpeningDoor | kOpeningWindow
    int room_a = -1;    // index into FloorPlan::rooms
    int room_b = -1;    // -2 = outside (entrance door, exterior window)
    bool vertical = false;  // the wall runs along z at x = line
    int line = 0;           // grid line index
    double m0 = 0.0, m1 = 0.0;  // span along the wall, meters (frame coords)
    double y0 = 0.0, y1 = 0.0;  // above the storey floor
    bool on_axis = false;       // window: centered on a bay axis (C6)
    std::string type;           // terminal id: "door_plain" | "win_flat"
};

struct DerivedOpenings {
    std::vector<DerivedOpening> openings;  // doors first (derivation order), then windows
    std::vector<std::pair<int, int>> adjacency;  // room pairs sharing >= 1 cell edge
    std::vector<double> depth;                   // Dijkstra depth from outside (§7.1)
    std::vector<char> reachable;                 // per room
    bool entrance_ok = false;                    // entrance door on the front facade
    bool back_door_ok = true;                    // program has none, or it is placed
    std::vector<std::pair<int, int>> missing_door_links;  // program door links w/o a door

    int door_count() const;
};

// Stickiness between re-derivations (§7.1): door key "idA|idB" (sorted;
// "outside" for the street) -> (vertical, line, m0); window key
// "roomId|side" (side 0=-x 1=+x 2=-z 3=+z) -> axis position along the wall.
struct OpeningMemo {
    std::map<std::string, std::tuple<bool, int, double>> doors;
    std::map<std::string, double> window_axes;
};

// B3: per-storey overrides of the street-rooted derivation (§7.1). Upper
// storeys have no street doors; their access graph is rooted at the rooms
// covering the stair arrival cells (depth 0, no door to "outside").
struct OpeningRules {
    bool street_doors = true;        // ground storey
    std::vector<int> arrival_cells;  // flat j*w+i on this storey's grid (G4)
};

DerivedOpenings derive_openings(const FloorPlan& fp, const ExpandedProgram& prog,
                                const PlanParams& params, OpeningMemo& memo,
                                const OpeningRules& rules = {});

}  // namespace building_topology_generator
