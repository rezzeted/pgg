#pragma once

// Massing (requirements §7.2, stage G2; algorithms.md §3): exhaustive scan of
// the candidate grid with cheap O(1) scores and cutoffs. Candidate families:
// rect, L (one volume with an L-shaped outline — valleys/hips come from the
// straight skeleton, C2), main + annex (a second, lower volume against a
// non-facade wall; its roof must stay apron_gap under the main roof, §7.2).
// The attic useful area is analytic for rect footprints and skeleton-sampled
// for L ones (algorithms.md §3: "skeleton only for the top-K and wings").

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "codes.h"
#include "ir.h"
#include "program.h"

namespace building_topology_generator {

struct Site {
    double w = 12.0, d = 12.0;  // plot size, meters (w along X / the facade)
    double setback = 0.0;       // min distance from plot bounds, roof edge incl.
    int max_storeys = 3;
};

struct MassingWeights {
    double fit = 8.0;       // quadratic per-storey deficit (C1)
    double waste = 0.4;     // linear per-storey surplus
    double compact = 0.15;  // footprint perimeter / sqrt(area)
    double roof = 0.3;      // valleys + second roofs + ridge height (C2)
    double prop = 0.5;      // facade proportion off the style center
    double opt = 0.7;       // bonus per included optional room (§8.2 полнота)
};

struct MassingParams {
    IrParams ir;
    MassingWeights w;
    double comms_share = 0.12;     // G1: circulation share of the room area
    double kappa_partitions = 0.08;
    double stair_area = 4.5;       // per storey of a volume with a stair
    double chimney_area = 0.25;    // per channel per storey (§5.7 cell section)
    double overhang = 0.4;
    double apron_gap = 0.3;        // annex roof clearance (§7.2)
    int top_m = 5;
};

struct MassingVolume {
    std::vector<glm::dvec2> outline;  // outer wall face, meters, CCW (§5.2)
    int storeys = 1;                  // full storeys of this volume
    bool attic = false;
    int roof_kind = kRoofGable;       // L volumes always scan as hip (valleys, C2)
    std::vector<double> pitch_by_edge;
    double eave_rise = 0.0;
};

struct MassingScore {
    double fit = 0.0, waste = 0.0, compact = 0.0, roof = 0.0, prop = 0.0, opt = 0.0;
    double total = 0.0;
};

struct MassingCandidate {
    std::string shape;  // rect | l | rect+annex
    std::vector<MassingVolume> volumes;
    int storeys = 1;  // full storeys of the main volume
    bool attic = false;
    double pitch = 0.0, eave_rise = 0.0;  // main volume
    MassingScore score;
    std::vector<double> net_area;   // per global storey (attic last), m²
    std::vector<double> used_area;  // assigned room targets, per storey
    int optionals_in = 0;
};

struct MassingResult {
    bool ok = false;
    std::string error;  // "the program does not fit the plot" with numbers (§7.2)
    MassingCandidate chosen;
    std::vector<MassingCandidate> top;  // diverse top-M (algorithms §3)
    int candidates_total = 0;
    int rejected_site = 0, rejected_prop = 0, rejected_fit = 0, rejected_roof = 0;
    std::string report;  // report.txt: why the winner won (F2)
};

MassingResult generate_massing(const ExpandedProgram& prog, const Site& site,
                               const MassingParams& params, uint64_t seed);

// Pinned massing (§4.5): not enumerated but validated — site fit, program
// fit; an invalid pin is an error with a hint, not a silent re-search.
MassingResult massing_from_pin(const ExpandedProgram& prog, const Site& site,
                               const MassingParams& params, uint64_t seed,
                               std::vector<MassingVolume> volumes, const std::string& pin_desc);

// Chosen candidate -> IR skeleton: params, storeys, volumes, roof spec +
// derived planes/edges. spaces/walls/... stay empty (G3+ fill them later).
bool massing_to_ir(const MassingCandidate& cand, const ExpandedProgram& prog,
                   const MassingParams& params, uint64_t seed, Ir& out, std::string& err);

}  // namespace building_topology_generator
