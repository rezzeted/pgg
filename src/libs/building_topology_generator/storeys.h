#pragma once

// Storey assignment (stage G3, requirements §7.3; algorithms §4): which room
// instance lives on which storey. Runs after G4 on the true storey fields
// (the stair shafts/wells are already known), records the greedy decision the
// massing only estimated, and cuts per-storey subprograms for G5/G6.

#include <string>
#include <vector>

#include "ir.h"
#include "plan.h"
#include "program.h"
#include "stairs.h"

namespace building_topology_generator {

// storey_of[i] = index into Ir::storeys for prog.rooms[i]; -1 = the optional
// room does not fit and is dropped. Fails (with an F1-style hint) when a
// required room fits nowhere or no entrance/back-door host can sit on the
// ground storey. Deterministic (no rng): the greedy order and the tie-breaks
// are fixed (private zone prefers up, the others down, §7.3).
bool assign_storeys(const ExpandedProgram& prog, const Ir& ir, const StoreyFields& fields,
                    const std::vector<StairCore>& stairs, double comms_share,
                    std::vector<int>& storey_of, std::string& err);

// The per-storey view for G5/G6: the rooms assigned to the storey (same
// instance ids), the links with both ends on it; entrance/back-door data
// survives only on the ground storey (the upper storeys are rooted at the
// stair arrival instead, §7.1).
ExpandedProgram subprogram_for(const ExpandedProgram& prog,
                               const std::vector<int>& storey_of, int storey);

}  // namespace building_topology_generator
