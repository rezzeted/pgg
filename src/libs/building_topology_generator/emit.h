#pragma once

// G8 emit (B2): FloorPlan + DerivedOpenings -> IR spaces / walls / nodes /
// access / anchors on top of the massing IR skeleton (volumes, storeys and
// the roof are already there). Single storey ("ground"), no slabs, stairs
// or chimneys — those arrive with B3/B4.

#include <string>

#include "anneal.h"
#include "ir.h"
#include "openings.h"
#include "plan.h"

namespace building_topology_generator {

bool emit_floor_ir(const FloorPlan& fp, const DerivedOpenings& ops,
                   const ExpandedProgram& prog, const PlanParams& params,
                   const CostBreakdown& cost, const AnnealReport& anneal, Ir& ir,
                   std::string& err);

}  // namespace building_topology_generator
