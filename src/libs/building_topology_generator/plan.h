#pragma once

// Floor plans (requirements §7.5 stage G5, algorithms.md §6): a storey plan as
// a rectangle tiling of the storey cell field. The rect invariant is
// structural (a room IS its bbox); the anneal moves (anneal.h) only
// resize/swap whole rectangles. B3 (§16 п. 3): multi-storey fields share one
// grid (stair cores stack, C11 support analysis), pinned stair-shaft rooms,
// slab VOID cells, attic knee/walk masks (§5.6).
//
// Grid frame: cell (i, j) covers [i*s, (i+1)*s) x [j*s, (j+1)*s) in the
// axis frame (wall center lines run on cell borders, §5.2). The facade (+Z)
// is the high-j side. Cell labels: room index >= 0, kCellFree, kCellOutside,
// kCellVoid, kCellRoofVoid.

#include <array>
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
inline constexpr int32_t kCellOutside = -2;  // outside the volumes (L cut, annex above, ...)
inline constexpr int32_t kCellVoid = -3;     // slab opening over a stair (no floor, §9.1)
inline constexpr int32_t kCellRoofVoid = -4;  // attic: floor exists, ceiling < h_knee (§5.6)

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
    double w_support = 1.5;       // C11: upper partitions over walls below (B3)
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
    // Pinned rooms (B3: the stair shaft) are placed before the growth and
    // never touched by the anneal moves.
    bool pinned = false;
    // Door-capable cells of the room's bbox, (j - z0) * (x1 - x0) + (i - x0);
    // empty = every cell may host a door. The stair shaft allows doors only
    // on the full-headroom floor cells (approach pad + the notch, §9.1).
    std::vector<char> door_ok;
    // The rectangle in grid cells: [x0, x1) x [z0, z1).
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    // Phase L (§16 B3, I10): extra wing rects, disjoint from the main rect and
    // 4-connected to it; empty = the room is the plain rectangle. Wings are
    // born in the repair when the field decomposes into more pieces than the
    // storey has rooms (a stair well shatters the attic walk field).
    std::vector<std::array<int, 4>> wings;

    bool l_shaped() const { return !wings.empty(); }
    // The anneal moves never touch a winged room (its shape is the field's)
    // or a pinned one (the stair shaft).
    bool fixed() const { return pinned || !wings.empty(); }

    int area_cells() const {
        int a = (x1 - x0) * (z1 - z0);
        for (const std::array<int, 4>& w : wings) a += (w[2] - w[0]) * (w[3] - w[1]);
        return a;
    }
    bool door_ok_at(int i, int j) const {
        if (door_ok.empty()) return true;
        const int w = x1 - x0;
        if (i < x0 || i >= x1 || j < z0 || j >= z1) return false;
        return door_ok[static_cast<size_t>((j - z0) * w + (i - x0))] != 0;
    }
};

struct FloorPlan {
    int w = 0, h = 0;           // field size in cells
    glm::dvec2 origin{0.0};     // building-frame corner of cell (0,0)
    int storey_index = 0;       // into Ir::storeys
    std::vector<PlanRoom> rooms;
    std::vector<int32_t> outside;  // w*h: kCellOutside / kCellVoid / kCellRoofVoid masks
    std::vector<int32_t> cells;    // w*h, row-major (j rows of i); from rasterize()
    // Attic (§5.6): cells whose worst corner clears h_walk (area_net counts
    // only these). Empty = every cell is useful (non-attic storeys).
    std::vector<char> useful;

    int32_t& at(int i, int j) { return cells[static_cast<size_t>(j) * w + i]; }
    int32_t at(int i, int j) const { return cells[static_cast<size_t>(j) * w + i]; }
    bool inside(int i, int j) const { return i >= 0 && i < w && j >= 0 && j < h; }
    int room_at(int i, int j) const { return inside(i, j) ? at(i, j) : kCellOutside; }

    // Rooms -> cells over the outside mask. Requires a valid tiling (I10; a
    // room is its main rect plus its wing rects, if any).
    void rasterize();
    // Every inside cell is covered by exactly one room, every room's cells are
    // exactly its rect + wings and are 4-connected (I10, phase L). VOID /
    // ROOFVOID cells are masked, not tiled. Diagnostics in err.
    bool check_rectangle_tiling(std::string& err) const;
    // Room index by id; -1 when absent.
    int find_room(const std::string& id) const;    // Net area in m² (§5.2: cells minus half thicknesses of the bounding
    // walls — the class is known per side from the neighbors). Attic rooms
    // (useful mask set): only the h_walk cells count, minus the wall
    // allowance along the useful boundary (§5.6).
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

// B3: one shared grid for every storey (the stair stack and the C11 support
// analysis need aligned cells). The grid frame comes from the union bbox of
// ALL volumes (the ground footprint); per-storey masks mark the cells covered
// by a volume spanning that storey. The attic mask trims cells whose worst
// corner is below h_knee to kCellRoofVoid (they keep the slab but no room),
// and the walk mask (ceiling >= h_walk) feeds area_net (§5.6). Roof heights
// come from the same skeleton model the geometry uses (roof.h parity).
struct StoreyFields {
    int w = 0, h = 0;
    glm::dvec2 origin{0.0};
    std::vector<std::vector<int32_t>> mask;  // per storey: kCellFree / kCellOutside / kCellRoofVoid
    std::vector<std::vector<char>> walk;     // attic storeys only; empty elsewhere
};

bool storey_fields(const Ir& ir, StoreyFields& out, std::string& err);

// Decompose the mask's inside cells (kCellFree or room indices; outside /
// void / roof-void labels are blocked) into disjoint maximal cell rects
// [x0, z0, x1, z1). Used for the stair well rects (G4) and the slab rects
// (G8 emit).
std::vector<std::array<int, 4>> decompose_cells(const std::vector<int32_t>& mask, int w,
                                                int h);

// Merge rect pairs whose union is again a rectangle (equal x-span adjacent in
// z, or equal z-span adjacent in x), iteratively. Around a hole (stair well,
// pinned shaft) the maximal-run decomposition shatters the ring into more
// rects than a rectangle tiling can afford; the union-merge restores the
// tileable pieces.
void merge_rect_pairs(std::vector<std::array<int, 4>>& rects);

// Number of rectangle pieces the mask's inside cells tile into (decompose +
// union-merge). G4 scores stair placements by it: a mid-field shaft or well
// that shatters a storey into more pieces than the storey has rooms makes the
// G5 repair unrecoverable (I10).
int tiled_piece_count(const std::vector<int32_t>& mask, int w, int h);

// Pieces + how many of them are un-hostable strips (a side under 3 cells:
// even a corridor needs 1.0 m clear, and 2 cells net 1.0 - t_partition =
// 0.88 m — such a piece can only ever wing onto a neighbor by the covering
// borrow (I10), so G4 penalizes them extra).
std::pair<int, int> tiled_piece_stats(const std::vector<int32_t>& mask, int w, int h);

// Half-thickness the boundary against `label` shaves off a room (§5.2):
// outside -> an exterior wall, roofvoid -> a knee wall, void -> a railing
// (no wall body), a room -> a partition.
double side_half_t(int32_t label, const IrParams& p);

// G5 initialization (algorithms §6.1/§6.2, rect phase; §6.4 corridors do not
// exist in B2 — access is repaired by the anneal instead). Rooms: required
// instances + the optional instances that fit by the massing estimate.
// The result is always a complete rectangle tiling (a guillotine re-slice
// repairs any growth leftover).
FloorPlan grow_floor(const ExpandedProgram& prog, const MassingCandidate& cand,
                     const PlanParams& params, Rng& rng, std::string& err);

// B3 grow over a prepared field: pre-seeded pinned rooms (the stair shaft),
// VOID/ROOFVOID masks, the optional entrance override (the stair arrival
// seeds the upper-storey's desirability field instead of the facade center).
struct GrowInput {
    int w = 0, h = 0;
    glm::dvec2 origin{0.0};
    std::vector<int32_t> outside;
    std::vector<PlanRoom> pinned;      // placed verbatim, tiled around
    std::vector<char> useful;          // attic walk mask (empty = all useful)
    int storey_index = 0;
    glm::dvec2 entrance_cell{-1.0, -1.0};  // < 0: the facade center (default)
};

FloorPlan grow_floor_cells(const GrowInput& in, const ExpandedProgram& prog,
                           const PlanParams& params, Rng& rng, std::string& err);

// I10 covering test for a (possibly winged) room, two tiers: every cell of
// the MAIN rect must be covered by an m x m square of the room's own cells
// (m = ceil(min_dim / s) — the furniture core), while a wing cell only needs
// a kWingCoverCells x kWingCoverCells square — a wing is alcove/passage area
// (a bay, a closet niche, a corridor spur), never the room's core. For a
// plain rectangle the test reduces to "min side >= m". Requires a rasterized
// cells grid (or one where the room's cells are already stamped).
constexpr int kWingCoverCells = 2;  // 1.0 m gross at the 0.5 m cell
bool room_covering_ok(const FloorPlan& fp, int r, int m_cells);

// Do the rects share a boundary segment (>= 1 cell)? A wing needs a real
// edge against its main rect, not a corner touch (4-connectedness, I10).
bool rects_edge_adjacent(const std::array<int, 4>& a, const std::array<int, 4>& b);

// I10 covering test over a union of cell rects (parts[0] = the main rect,
// the rest its wings + the candidate piece), two tiers as in
// room_covering_ok: main-rect cells need m_main x m_main, wing cells
// m_wing x m_wing (clamped to m_main). Works on a local grid over the union
// bbox. G4 uses it to tell a wing-capable strip from a dead pocket.
bool rects_covering_ok(const std::vector<std::array<int, 4>>& parts, int m_main, int m_wing);

}  // namespace building_topology_generator
