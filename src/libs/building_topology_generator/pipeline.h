#pragma once

// The B3 driver (requirements §16 п. 3, §9.1): the full G3→G4→G5/G6 chain for
// a multi-storey massing. One attempt: place the stair cores bottom-up (G4,
// voids stamped into the shared storey fields), assign the rooms to storeys
// (G3, exact net areas), then grow + anneal each storey bottom-up (G5/G6) —
// the ground storey rooted at the street, the upper ones at the stair
// arrival (§7.1), the lower plan feeding the support term (C11). The outer
// retries re-roll the whole chain while a hard term survives and keep the
// lexicographic best on the summed (hard, soft).

#include <string>
#include <vector>

#include "anneal.h"
#include "ir.h"
#include "plan.h"
#include "program.h"
#include "stairs.h"
#include "storeys.h"

namespace building_topology_generator {

struct BuildingPlanResult {
    bool ok = false;
    std::string error;
    StoreyFields fields;             // after the stair voids are stamped
    std::vector<StairCore> stairs;
    std::vector<int> storey_of;      // per ExpandedProgram::rooms entry
    std::vector<PlanResult> storeys; // per Ir::storeys
    int retries_used = 0;
};

BuildingPlanResult plan_building(const ExpandedProgram& prog, const Ir& ir,
                                 const PlanParams& params, uint64_t seed,
                                 int max_retries = 3);

}  // namespace building_topology_generator
