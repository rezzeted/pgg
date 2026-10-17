#pragma once

// Chimney stacks (requirements §5.7, §9.2; stage G4b): groups the program's
// hearths into vertical stacks and places them on the shared storey grid
// before the floor plans grow (the stacks are hard inter-storey constraints,
// like the stair cores). A stack owns its cells (kCellCore) on every storey
// from the ground to the containing volume's top — the geometry asset rises
// from y = 0, so the masonry needs support cells even under an upper-storey
// first hearth (v1; §9.2's "starts at the hearth storey" would float).
//
// Grouping (§9.2): same-storey hearth pairs share one 1x2 stack (back to
// back); an upper-storey hearth joins an existing passing stack (the "second
// channel" — v1 does NOT widen the section for it, §5.7 caps sections at
// 1x2 anyway); anything left gets a new stack. Archetypes (style.chimney):
// central (interior, within the 1.5 m ridge band), gable (a boundary cell
// against a ~90 deg outline edge), free (anything feasible). The requested
// archetype falls back central -> gable -> free when no candidate of the
// class is feasible.
//
// The exit obeys §5.7 at placement time: the top is
// chimney_top_required(d_to_ridge, ridge_y, exit_y), the footprint keeps
// valley_margin from every valley and hip, and the exit face must contain
// the stack center (BGG's check_chimney re-verifies all three numerically).
// A shed roof (an annex) has no ridge arcs — no stack exits it in v1.

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ir.h"
#include "plan.h"
#include "program.h"
#include "rng.h"
#include "stairs.h"

namespace building_topology_generator {

struct ChimneyCore {
    std::string id;                 // "chimney1", ... (frozen convention)
    int archetype = 0;              // 1 central, 2 gable, 3 free (as placed)
    std::vector<glm::ivec2> cells;  // plan cells, identical on every crossed storey
    int storey0 = 0, storey1 = 0;   // crossed storey range (support from the ground)
    std::vector<int> hearths;       // indices into ExpandedProgram::rooms
    glm::dvec3 top{0.0};            // footprint center + the §5.7 top height
    int roof_plane = -1;            // exit plane: global index into Ir::roof.planes
};

// Places one stack per hearth group; stamps kCellCore into fields.mask on the
// crossed storeys. storey_of: the G3 assignment (-1 = dropped optional — no
// stack is built for it; a dropped pair half leaves its sibling on the
// stack). Deterministic modulo the rng jitter in the candidate scoring.
// tried: stack cell-sets (sorted) chosen by earlier failed retries of the
// driver — an exact re-pick can only repeat the same failure (the scoring is
// dominated by geometry, the rng jitter rarely flips the argmin), so such a
// candidate is rejected and the retry explores the next-best position.
bool place_chimneys(const Ir& ir, StoreyFields& fields, const std::vector<StairCore>& stairs,
                    const ExpandedProgram& prog, const std::vector<int>& storey_of,
                    const PlanParams& params, Rng& rng,
                    const std::vector<std::vector<glm::ivec2>>& tried,
                    std::vector<ChimneyCore>& out, std::string& err);

// The cell set, sorted — the identity of a placement across driver retries.
// The driver feeds place_chimneys's `tried` ban list with
// normalize_stack_cells(core.cells) of the failed attempts.
std::vector<glm::ivec2> normalize_stack_cells(std::vector<glm::ivec2> cells);

// The G5 seed set of a hearth room (§9.2: the hearth sits on the stack): the
// free 4-neighbors of the stack cells on storey k, minus the stair shaft
// rects pinned there. Empty = the room cannot touch its stack (the plan
// attempt fails the hearth hard term and the driver retries).
std::vector<glm::ivec2> hearth_seed_cells(const ChimneyCore& core, const StoreyFields& fields,
                                          int k, const std::vector<StairCore>& stairs);

}  // namespace building_topology_generator
