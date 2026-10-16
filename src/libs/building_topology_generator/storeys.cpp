// Stage G3 (requirements §7.3, algorithms §4): the greedy storey assignment
// the massing estimated (massing.cpp assign_rooms) re-run on the true storey
// fields and recorded. Net area of a storey = its free cells (attic: the
// h_walk cells only, §5.6) minus the stair shafts, times the partition
// coefficient — the same accounting G2 used, but exact.

#include "pch.h"

#include "storeys.h"

#include <algorithm>
#include <cmath>

#include "codes.h"

#include <cstdio>
#include <cstdlib>

namespace building_topology_generator {

namespace {

constexpr double kEps = 1e-9;
constexpr double kKappaPartitions = 0.08;  // as in G2 (massing) and G5 (grow)

// The storeys the room is allowed on, as indices into Ir::storeys (the
// program mask is over the kStorey* codes, §4.1).
std::vector<int> allowed_storeys(const RoomInstance& r, const Ir& ir) {
    std::vector<int> out;
    for (int k = 0; k < static_cast<int>(ir.storeys.size()); ++k)
        if (r.storeys_mask & (1u << ir.storeys[static_cast<size_t>(k)].kind_code))
            out.push_back(k);
    return out;
}

}  // namespace

bool assign_storeys(const ExpandedProgram& prog, const Ir& ir, const StoreyFields& fields,
                    const std::vector<StairCore>& stairs, double comms_share,
                    std::vector<int>& storey_of, std::string& err) {
    err.clear();
    const int n = static_cast<int>(ir.storeys.size());
    const double s2 = ir.params.cell * ir.params.cell;

    // Exact per-storey net area: free cells (attic: h_walk cells only) minus
    // the shafts of the stairs leaving this storey (the wells of the stairs
    // below are already kCellVoid in the field), times kappa.
    std::vector<double> net(static_cast<size_t>(n), 0.0);
    for (int k = 0; k < n; ++k) {
        const bool is_attic = ir.storeys[static_cast<size_t>(k)].kind_code == kStoreyAttic;
        const auto& mask = fields.mask[static_cast<size_t>(k)];
        const auto& walk = fields.walk[static_cast<size_t>(k)];
        double cells = 0.0;
        for (size_t c = 0; c < mask.size(); ++c) {
            if (mask[c] != kCellFree) continue;
            if (is_attic && !walk[c]) continue;
            cells += 1.0;
        }
        for (const StairCore& st : stairs)
            if (st.from == k) cells -= st.shaft_room.area_cells();
        net[static_cast<size_t>(k)] = std::max(0.0, cells * s2 * (1.0 - kKappaPartitions));
    }

    // Circulation reserve: a storey the stair touches grows an auto-corridor
    // (pipeline.cpp: target max(3, 10% of the free area)) — G3 must not hand
    // that area to rooms, or the corridor squeezes the last admitted room
    // into a corner that cannot meet min_dim.
    std::vector<double> circ_res(static_cast<size_t>(n), 0.0);
    for (const StairCore& st : stairs)
        for (int k : {st.from, st.to})
            circ_res[static_cast<size_t>(k)] =
                std::max(3.0, 0.10 * net[static_cast<size_t>(k)]);

    const int nr = static_cast<int>(prog.rooms.size());
    storey_of.assign(static_cast<size_t>(nr), -1);
    std::vector<double> used(static_cast<size_t>(n), 0.0);

    // Ground-floor guarantees first: an entrance host and a back-door host
    // (§7.1) must sit on the ground storey — force-assign one of each before
    // the greedy so the guarantee cannot be traded away.
    auto force_ground = [&](const std::vector<std::string>& hosts, const char* what) {
        if (hosts.empty()) return true;
        for (const std::string& id : hosts)
            for (size_t i = 0; i < prog.rooms.size(); ++i)
                if (prog.rooms[i].id == id && storey_of[i] == 0)
                    return true;  // already forced by the other role
        const RoomInstance* pick = nullptr;
        for (const std::string& id : hosts) {
            for (const RoomInstance& r : prog.rooms) {
                if (r.id != id || r.optional) continue;
                if (!(r.storeys_mask & (1u << kStoreyGround))) continue;
                if (!pick || r.area_min < pick->area_min ||
                    (r.area_min == pick->area_min && r.id < pick->id))
                    pick = &r;
            }
        }
        if (!pick) {
            err = std::string("G3: no required ") + what +
                  " host allows the ground storey — widen its storeys mask or the "
                  "entrance/back_door 'into' list (requirements §7.1)";
            return false;
        }
        if (net[0] - used[0] - circ_res[0] < pick->area_min - kEps) {
            err = "G3: the ground storey cannot host " + std::string(what) + " room '" +
                  pick->id + "' (needs " + std::to_string(pick->area_min) +
                  " m2 net) — the massing underestimated; report this";
            return false;
        }
        const int idx = static_cast<int>(pick - prog.rooms.data());
        storey_of[static_cast<size_t>(idx)] = 0;
        used[0] += pick->target;
        return true;
    };
    if (!force_ground(prog.entrance_into, "entrance")) return false;
    if (prog.has_back_door && !force_ground(prog.back_door_into, "back-door")) return false;

    // Greedy (algorithms §4): required rooms first, fewest allowed storeys
    // first, biggest target first; each goes to the storey with the most
    // spare. Tie-breaks: private zone up, the rest down (§7.3); a
    // same-storey-linked room prefers the link partner's storey (§4.1).
    std::vector<int> required, optional;
    for (int i = 0; i < nr; ++i) {
        if (storey_of[static_cast<size_t>(i)] >= 0) continue;  // forced above
        (prog.rooms[static_cast<size_t>(i)].optional ? optional : required).push_back(i);
    }
    auto allowed_count = [&](int i) {
        return allowed_storeys(prog.rooms[static_cast<size_t>(i)], ir).size();
    };
    std::sort(required.begin(), required.end(), [&](int a, int b) {
        const RoomInstance& A = prog.rooms[static_cast<size_t>(a)];
        const RoomInstance& B = prog.rooms[static_cast<size_t>(b)];
        const size_t na = allowed_count(a), nb = allowed_count(b);
        if (na != nb) return na < nb;
        if (A.target != B.target) return A.target > B.target;
        return A.id < B.id;
    });

    auto same_storey_pull = [&](int i) -> int {
        const RoomInstance& r = prog.rooms[static_cast<size_t>(i)];
        int pull = -1;
        for (const ProgramLink& l : prog.links) {
            if (l.kind != kLinkSameStorey) continue;
            const std::string* other = nullptr;
            if (l.a == r.id)
                other = &l.b;
            else if (l.b == r.id)
                other = &l.a;
            if (!other) continue;
            for (int q = 0; q < nr; ++q)
                if (prog.rooms[static_cast<size_t>(q)].id == *other &&
                    storey_of[static_cast<size_t>(q)] >= 0) {
                    if (pull < 0 || storey_of[static_cast<size_t>(q)] < pull)
                        pull = storey_of[static_cast<size_t>(q)];
                    break;
                }
        }
        return pull;
    };

    auto pick_storey = [&](int i, bool with_comms) {
        const RoomInstance& r = prog.rooms[static_cast<size_t>(i)];
        const int pull = same_storey_pull(i);
        int best = -1;
        double best_spare = 0.0;
        for (int k : allowed_storeys(r, ir)) {
            const double need = with_comms ? r.target : r.area_min;
            const double avail = net[static_cast<size_t>(k)] - used[static_cast<size_t>(k)] -
                                 circ_res[static_cast<size_t>(k)];
            const double spare = with_comms ? avail / (1.0 + comms_share) - need
                                            : avail - need;
            if (spare < -kEps) continue;
            bool better = best < 0 || spare > best_spare + kEps;
            if (!better && std::abs(spare - best_spare) <= kEps) {
                if (pull >= 0 && (k == pull) != (best == pull))
                    better = (k == pull);
                else
                    better = (r.zone_code == kZonePrivate ? k > best : k < best);
            }
            if (better) {
                best = k;
                best_spare = spare;
            }
        }
        return best;
    };

    for (int i : required) {
        const int k = pick_storey(i, false);
        if (k < 0) {
            err = "G3: required room '" + prog.rooms[static_cast<size_t>(i)].id +
                  "' fits no storey — the massing overestimated the net areas; report this";
            return false;
        }
        storey_of[static_cast<size_t>(i)] = k;
        used[static_cast<size_t>(k)] += prog.rooms[static_cast<size_t>(i)].target;
    }
    for (int i : optional) {
        const int k = pick_storey(i, true);
        if (std::getenv("BTG_DEBUG_DUMP")) {
            std::string d = "G3 optional '" + prog.rooms[static_cast<size_t>(i)].id + "' -> " +
                            std::to_string(k) + " (net:";
            for (int q = 0; q < n; ++q)
                d += " " + std::to_string(net[static_cast<size_t>(q)]) + "/" +
                     std::to_string(used[static_cast<size_t>(q)]);
            d += ")";
            std::fprintf(stderr, "%s\n", d.c_str());
        }
        if (k < 0) continue;  // the optional room is dropped (§4.1)
        storey_of[static_cast<size_t>(i)] = k;
        used[static_cast<size_t>(k)] += prog.rooms[static_cast<size_t>(i)].target;
    }

    // A non-ground storey that exists but hosts nothing makes G5 build an
    // empty floor; donate the smallest required room allowed there that the
    // storey can actually hold (its net minus the circulation reserve must
    // clear area_min — for the attic the net counts the h_walk cells only,
    // §5.6). G2 already filters massings whose attic hosts no room, so a
    // miss here means a hand-written IR or a pin bypassed the gate.
    for (int k = 1; k < n; ++k) {
        bool any = false;
        for (int i = 0; i < nr && !any; ++i) any = storey_of[static_cast<size_t>(i)] == k;
        if (any) continue;
        int donor = -1;
        for (int i = 0; i < nr; ++i) {
            const RoomInstance& r = prog.rooms[static_cast<size_t>(i)];
            if (r.optional || storey_of[static_cast<size_t>(i)] < 0) continue;
            const auto allowed = allowed_storeys(r, ir);
            if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) continue;
            if (net[static_cast<size_t>(k)] - used[static_cast<size_t>(k)] -
                    circ_res[static_cast<size_t>(k)] <
                r.area_min - kEps)
                continue;
            if (donor < 0 || r.area_min < prog.rooms[static_cast<size_t>(donor)].area_min)
                donor = i;
        }
        if (donor < 0) {
            err = "G3: storey '" + ir.storeys[static_cast<size_t>(k)].id +
                  "' hosts no room and no required room it allows fits its net area — "
                  "the massing should have rejected it (requirements §5.6); report this";
            return false;
        }
        used[static_cast<size_t>(storey_of[static_cast<size_t>(donor)])] -=
            prog.rooms[static_cast<size_t>(donor)].target;
        storey_of[static_cast<size_t>(donor)] = k;
        used[static_cast<size_t>(k)] += prog.rooms[static_cast<size_t>(donor)].target;
    }
    return true;
}

ExpandedProgram subprogram_for(const ExpandedProgram& prog,
                               const std::vector<int>& storey_of, int storey) {
    ExpandedProgram sub;
    sub.type = prog.type;
    sub.style = prog.style;
    sub.storeys_min = sub.storeys_max = 1;
    sub.attic = AtticMode::Forbidden;
    sub.h_ground = prog.h_ground;
    for (size_t i = 0; i < prog.rooms.size(); ++i)
        if (storey_of[i] == storey) sub.rooms.push_back(prog.rooms[i]);
    auto on_storey = [&](const std::string& id) {
        for (size_t i = 0; i < prog.rooms.size(); ++i)
            if (prog.rooms[i].id == id) return storey_of[i] == storey;
        return false;
    };
    for (const ProgramLink& l : prog.links)
        if (on_storey(l.a) && on_storey(l.b)) sub.links.push_back(l);
    sub.entrance_facade = prog.entrance_facade;
    if (storey == 0) {
        for (const std::string& id : prog.entrance_into)
            if (on_storey(id)) sub.entrance_into.push_back(id);
        sub.has_back_door = prog.has_back_door;
        sub.back_door_facade = prog.back_door_facade;
        for (const std::string& id : prog.back_door_into)
            if (on_storey(id)) sub.back_door_into.push_back(id);
    }
    return sub;
}

}  // namespace building_topology_generator
