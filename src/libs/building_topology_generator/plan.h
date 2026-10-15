#pragma once

// Floor plans (requirements §7.5 stage G5, algorithms.md §6): one-storey plan
// as a rectangle tiling of the storey cell field. B2 scope (§18 п. 2): rooms
// are always rectangles — the invariant is structural (a room IS its bbox),
// the anneal moves (anneal.h) only resize/swap whole rectangles.
//
// Grid frame: cell (i, j) covers [i*s, (i+1)*s) x [j*s, (j+1)*s) in the
// axis frame (wall center lines run on cell borders, §5.2). The facade (+Z)
// is the high-j side. Cell labels: room index >= 0, kCellFree, kCellOutside.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ir.h"
#include "massing.h"
#include "program.h"
#include "rng.h"

namespace building_topology_generator {

inline constexpr int32_t kCellFree = -1;     // inside the volumes, unassigned
inline constexpr int32_t kCellOutside = -2;  // outside the volumes (L cut, ...)

struct PlanParams {
    IrParams ir;
    double bay_target = 2.5;      // facade axis bay, m (algorithms §7.2)
    double d_min = 2.0;           // min private depth in the access graph (§8.2)
    double span_max = 5.5;        // unsupported span, m (§7.5)
    int door_cells = 2;           // door width in cells (§5.4)
    int door_margin = 1;          // min cells from a wall end / node (§5.4)
    double door_h = 2.1;          // door top above the storey floor
    double sill_y = 0.9;          // window sill (§5.4)
    double window_h = 1.5;        // window height (§5.4 + frozen house)
    double window_area = 1.5;     // one window's glazing, for the count (§7.2)
    double comms_share = 0.12;    // circulation share in area estimates (§7.1)
    // Anneal (algorithms §8.2, §11; appendix A).
    int budget = 20000;           // moves (B1, per storey)
    int calibrate_moves = 200;    // probe moves for T0
    double q_critic = 0.3;        // share of critic-directed moves
    int max_attempts = 6;         // re-grow + re-anneal while hard > 0 (§11)
    // Soft term weights, order 1 after normalization (§8.2).
    double w_area = 4.0;
    double w_form = 1.0;
    double w_adjacency = 2.0;     // near links
    double w_privacy = 1.5;
    double w_light = 3.0;
    double w_regularity = 1.0;
    double w_span = 1.0;
    double w_hard0 = 10.0;        // phase-0 weight of one hard violation
};

struct PlanRoom {
    std::string id;       // instance id ("bed.1"), = base id for count-less rooms
    std::string base_id;  // program id ("bed")
    int fn_code = 0;
    int zone_code = 0;
    double area_min = 0.0, area_max = 0.0, target = 0.0;  // m² net
    double min_dim = 1.5;                                 // m (fn default, I10)
    bool habitable = false;
    bool optional = false;
    int hearth_code = 0;
    bool entrance_front = false;  // in program entrance.into: allowed to host the facade entrance (G6 picks one)
    bool entrance_back = false;   // in program back_door.into: allowed to host the back door
    // The rectangle in grid cells: [x0, x1) x [z0, z1).
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;

    int area_cells() const { return (x1 - x0) * (z1 - z0); }
};

struct FloorPlan {
    int w = 0, h = 0;           // field size in cells
    glm::dvec2 origin{0.0};     // building-frame corner of cell (0,0)
    std::vector<PlanRoom> rooms;
    std::vector<int32_t> outside;  // w*h: kCellOutside where outside the volumes
    std::vector<int32_t> cells;    // w*h, row-major (j rows of i); from rasterize()

    int32_t& at(int i, int j) { return cells[static_cast<size_t>(j) * w + i]; }
    int32_t at(int i, int j) const { return cells[static_cast<size_t>(j) * w + i]; }
    bool inside(int i, int j) const { return i >= 0 && i < w && j >= 0 && j < h; }

    // Rooms -> cells over the outside mask. Requires a valid rectangle tiling
    // (I10 in rect mode).
    void rasterize();
    // Every inside cell is covered by exactly one room, every room's cells
    // are exactly its bbox (rectangle tiling). Diagnostics in err.
    bool check_rectangle_tiling(std::string& err) const;
    // Room index by id; -1 when absent.
    int find_room(const std::string& id) const;
    // Net area in m² (§5.2: cells minus half thicknesses of the bounding
    // walls — the class is known per side from the neighbors).
    double room_area_net(int r, const IrParams& p) const;
    // Net plan dimensions in m² components (same half-thickness rule).
    void room_dims_net(int r, const IrParams& p, double& w_m, double& d_m) const;
};

// Cell field of the storey from the massing volumes (kCellFree inside,
// kCellOutside out) and the building-frame origin. Single-storey volumes
// only (B2). Fails when a volume has more than one full storey or an attic.
bool storey_field(const MassingCandidate& cand, const IrParams& params, int& w_out,
                  int& h_out, glm::dvec2& origin, std::vector<int32_t>& cells,
                  std::string& err);

// G5 initialization (algorithms §6.1/§6.2, rect phase; §6.4 corridors do not
// exist in B2 — access is repaired by the anneal instead). Rooms: required
// instances + the optional instances that fit by the massing estimate.
// The result is always a complete rectangle tiling (a guillotine re-slice
// repairs any growth leftover).
FloorPlan grow_floor(const ExpandedProgram& prog, const MassingCandidate& cand,
                     const PlanParams& params, Rng& rng, std::string& err);

}  // namespace building_topology_generator
