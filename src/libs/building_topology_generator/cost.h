#pragma once

// Cost function of the plan search (algorithms §8.2, normalization §8.4):
// hard violations (access, program door links, min dimensions) counted per
// occurrence; soft terms normalized to order 1 and weighted. Rect mode: the
// form term is identically 0. B3 adds the support term (C11) against the
// lower storey's plan when `below` is given.

#include <map>
#include <string>

#include "openings.h"
#include "plan.h"

namespace building_topology_generator {

struct CostBreakdown {
    double soft = 0.0;                       // weighted sum of the soft terms
    int hard = 0;                            // total hard violations
    std::map<std::string, double> terms;     // soft terms, weighted
    std::map<std::string, double> raw;       // soft terms, normalized (report)
    std::map<std::string, int> hard_terms;   // hard violations by name

    double total(double w_hard) const { return soft + w_hard * static_cast<double>(hard); }
};

CostBreakdown evaluate_cost(const FloorPlan& fp, const ExpandedProgram& prog,
                            const DerivedOpenings& ops, const PlanParams& params,
                            const FloorPlan* below = nullptr, bool full_height = true);

}  // namespace building_topology_generator
