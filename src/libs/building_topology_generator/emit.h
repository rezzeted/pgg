#pragma once

// G8 emit (B2): FloorPlan + DerivedOpenings -> IR spaces / walls / nodes /
// access / anchors on top of the massing IR skeleton (volumes, storeys and
// the roof are already there). Single storey ("ground"), no slabs or stairs
// — those arrive with B3.
//
// B3: emit_building_ir emits the whole multi-storey building (§9.1): per-
// storey spaces/walls/openings with id prefixes (g_/u_/a_), attic knee and
// gable walls with the roof clip planes (§5.6), slabs with the stair wells
// cut out, railings around the wells, L-stairs and the stair access edges.
//
// B4 (§5.7, §9.2): the chimney stacks — one IrChimney per stack (section
// from the cell span, cells_by_storey, the §5.7 top, the exit plane index),
// the hearth refs (the nearest wall of the hearth room, s clamped off the
// wall ends) and the kAnchorHearth anchors (0.9 m off the wall into the
// room). Core walls are not emitted: the stack asset builds the face (§6);
// a core cell against the outside emits an exterior wall.

#include <string>

#include "anneal.h"
#include "ir.h"
#include "openings.h"
#include "pipeline.h"
#include "plan.h"

namespace building_topology_generator {

bool emit_floor_ir(const FloorPlan& fp, const DerivedOpenings& ops,
                   const ExpandedProgram& prog, const PlanParams& params,
                   const CostBreakdown& cost, const AnnealReport& anneal, Ir& ir,
                   std::string& err);

bool emit_building_ir(const BuildingPlanResult& bp, const ExpandedProgram& prog,
                      const PlanParams& params, Ir& ir, std::string& err);

}  // namespace building_topology_generator
