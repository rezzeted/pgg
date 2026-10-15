#pragma once

// L0 anneal for one storey (algorithms §8 moves, §11 schedule): line-shift +
// swap over the rectangle tiling, Metropolis with geometric cooling, one
// reheat, a hard-weight ramp, lexicographic (hard, soft) best snapshot.
// plan_floor is the G5+G6 pipeline for a single-storey massing; while a hard
// term survives, it re-grows the tiling from a split PRNG stream and
// re-anneals (up to PlanParams::max_attempts) — the L0 moves never change
// the grown column/row topology, and some tilings cannot host the program.

#include <string>
#include <vector>

#include "cost.h"
#include "openings.h"
#include "plan.h"
#include "rng.h"

namespace building_topology_generator {

struct AnnealReport {
    int moves_tried = 0;
    int moves_accepted = 0;
    double seconds = 0.0;
    double t0 = 0.0;
    CostBreakdown initial;
    CostBreakdown final;
    std::vector<std::string> log;
};

// The returned openings/cost are the ones derived at the best snapshot
// (memo-consistent), so the emit stage sees exactly the evaluated state.
struct PlanResult {
    bool ok = false;
    std::string error;
    FloorPlan plan;
    DerivedOpenings openings;
    CostBreakdown cost;
    AnnealReport anneal;
};

PlanResult plan_floor(const ExpandedProgram& prog, const MassingCandidate& cand,
                      const PlanParams& params, uint64_t seed);

}  // namespace building_topology_generator
