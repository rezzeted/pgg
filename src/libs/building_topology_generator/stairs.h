#pragma once

// Stair cores (requirements §5.5, §9.1; stage G4, algorithms §15). B3 fills
// L-stairs only (the geometry stage builds nothing else). Each stair is a
// pinned "stairwell" rectangle on its from-storey plus a slab opening (the
// well) on the to-storey; stacking is not required — a stair is placed on
// its from-storey field minus the voids of the stairs below.
//
// Canonical layout (dir = +x, turn = +1 -> dir2 = +z; other orientations are
// rotations of this one). Cell size s = 0.5, flight width w = 2 cells = 1 m.
//
//   columns [0]          : approach pad (full headroom, door-ok)
//   columns [1, m1]      : march 1 strip (rows 0..1), l1 = (risers_low-1)*g
//   columns [m1+1, m1+2] : landing (rows 0..1), then march 2 (rows 2..2+m2-1)
//   row 2+m2             : arrival row (floor on the to-storey; the open edge)
//   columns [1, m1], rows 2.. : the notch (floor beside march 1, door-ok)
//
// The shaft room = the whole bbox (m1+3) x (m2+3). The well voids, on the
// to-storey, the cells intersecting the §14.2 headroom bands: the landing,
// march 2, and march 1 from step i0 = floor((H - slab_t - h_walk)/rise) + 1.

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ir.h"
#include "plan.h"
#include "rng.h"

namespace building_topology_generator {

struct StairCore {
    std::string id;        // "stair1", "stair2" (frozen convention)
    int from = 0, to = 0;  // storey indices into Ir::storeys
    // Dimensions (§5.5; I2 by construction).
    int risers = 0, risers_low = 0;
    double rise = 0.0, going = 0.0, width = 1.0;
    // World placement (building frame; start = center of the first riser
    // line at the from-storey floor, per the stair_l asset contract).
    glm::dvec2 start{0.0};
    glm::dvec2 dir{1.0, 0.0}, dir2{0.0, 1.0};
    IrSlabRect landing;                    // exact meters (parser requires it)
    std::vector<IrSlabRect> well_rects;    // the void cells, decomposed, meters
    // Grid data on the shared storey grid.
    PlanRoom shaft_room;                   // pinned, with the door_ok mask
    std::vector<int> void_cells;           // flat j*w+i on the to-storey
    std::vector<int> arrival_cells;        // flat on the to-storey (open edge)
};

// n = ceil(H / 0.19), r = H/n, g = 0.25: Blondel 2r+g lands in [0.606, 0.63]
// for every full-storey H (r > 0.19*(n-1)/n by the minimality of n).
void size_stair(double h, int& risers, double& rise, double& going);

// The well + arrival cells of an L-stair split in canonical coords (u along
// dir, v along dir2; the shaft spans [0, W) x [0, D) canonical cells): the
// §14.2 headroom bands (march 1 from step i0, the landing, march 2),
// cell-quantized. Shared by G4 (place_stairs maps them onto the storey grid)
// and the G2 massing filter (attic_walk_hosts_stair), so the two never drift.
struct StairWellShape {
    int W = 0, D = 0;  // canonical shaft bbox, cells
    std::vector<glm::ivec2> well;     // voids on the to-storey
    std::vector<glm::ivec2> arrival;  // open-edge row on the to-storey
};

StairWellShape stair_well_shape(int risers, int risers_low, double rise, double going,
                                double slab_t, const IrParams& p);

// G2 feasibility filter (requirements §9.1: the exit into the attic must land
// in the h_walk zone): can a stair over this gap place its well + arrival
// inside the attic walk mask? Exact scan of the G4 shapes over the 8
// orientations and every grid position (the from-storey side is not checked —
// a full storey almost always hosts the shaft; G4 decides for real).
bool attic_walk_hosts_stair(const std::vector<char>& walk, int w, int h, double h_gap,
                            double slab_t, const IrParams& p);

// Places one core per storey gap, bottom-up; voids are stamped into
// fields.mask (kCellVoid) as they are decided, so an upper stair never
// overlaps the opening of the one below. Deterministic modulo the rng jitter
// in the candidate scoring (the driver's outer retries re-roll it).
bool place_stairs(const Ir& ir, StoreyFields& fields, const PlanParams& params,
                  Rng& rng, std::vector<StairCore>& out, std::string& err);

// The auto-corridor's seed cells on storey k (algorithms §6.4): the free
// 4-neighbors of the k-starting shafts along their fully door-ok sides (the
// long flight side and the far end, §9.1 — a contact anywhere else cannot
// host the corridor~stairwell door). mask gates freeness (a cell !=
// kCellFree is excluded): the pipeline passes the storey field, the G4b
// chimney gate the field with the candidate stack and the shafts stamped
// out. When no fully door-ok side is exposed (shouldn't happen), falls back
// to any free shaft neighbor — an empty result then truly means the shaft is
// walled in and the storey cannot seed its corridor.
std::vector<glm::ivec2> corridor_seed_cells(const std::vector<int32_t>& mask, int w, int h,
                                            const std::vector<StairCore>& stairs, int k);

}  // namespace building_topology_generator
