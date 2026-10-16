// The B3 driver. See pipeline.h.

#include "pch.h"

#include "pipeline.h"

#include <algorithm>

#include <cstdio>
#include <cstdlib>

#include "codes.h"

namespace building_topology_generator {

BuildingPlanResult plan_building(const ExpandedProgram& prog, const Ir& ir,
                                 const PlanParams& params, uint64_t seed, int max_retries) {
    BuildingPlanResult best;
    std::string err;
    StoreyFields base;
    if (!storey_fields(ir, base, err)) {
        best.error = err;
        return best;
    }
    const int n = static_cast<int>(ir.storeys.size());
    const double s = params.ir.cell;
    Rng rng = Rng::from_seed(seed);
    int best_hard = -1;
    double best_soft = 0.0;

    for (int attempt = 0; attempt < std::max(1, max_retries); ++attempt) {
        Rng arng = attempt == 0 ? rng : rng.split(0xB3330000ull + static_cast<uint64_t>(attempt));
        BuildingPlanResult res;
        res.retries_used = attempt;
        res.fields = base;  // place_stairs stamps the voids into the copy
        if (!place_stairs(ir, res.fields, params, arng, res.stairs, err)) {
            best.error = err;
            continue;
        }
        if (!assign_storeys(prog, ir, res.fields, res.stairs, params.comms_share,
                            res.storey_of, err)) {
            best.error = err;
            continue;
        }
        res.storeys.resize(static_cast<size_t>(n));

        int hard_sum = 0;
        double soft_sum = 0.0;
        bool failed = false;
        for (int k = 0; k < n && !failed; ++k) {
            ExpandedProgram sub = subprogram_for(prog, res.storey_of, k);
            // Auto-corridor (algorithms §6.4): a storey the stair touches
            // needs a circulation hub — the frozen programs carry explicit
            // corridor instances, an author-written program generally does
            // not. Injected per storey so the growth has a deficit-absorbing
            // service room and the arrival has a seed host.
            bool has_shaft = false, has_arrival = false, corridor_present = false;
            for (const StairCore& st : res.stairs) {
                has_shaft = has_shaft || st.from == k;
                has_arrival = has_arrival || st.to == k;
            }
            for (const RoomInstance& r : sub.rooms)
                corridor_present = corridor_present || r.fn_code == kFnCorridor;
            if ((has_shaft || has_arrival) && !corridor_present) {
                const auto& mask = res.fields.mask[static_cast<size_t>(k)];
                const auto& walk = res.fields.walk[static_cast<size_t>(k)];
                double cells = 0.0;
                for (size_t c = 0; c < mask.size(); ++c) {
                    if (mask[c] != kCellFree) continue;
                    if (!walk.empty() && !walk[c]) continue;  // attic: h_walk cells only
                    cells += 1.0;
                }
                for (const StairCore& st : res.stairs)
                    if (st.from == k) cells -= st.shaft_room.area_cells();
                const double free_area = std::max(0.0, cells * s * s);
                RoomInstance cor;
                cor.id = cor.base_id = "corridor";
                cor.fn_code = kFnCorridor;
                cor.zone_code = kZoneService;
                cor.area_min = 2.0;
                cor.target = std::max(3.0, 0.10 * free_area);
                cor.area_max = 2.0 * cor.target;
                cor.storeys_mask = ~0u;
                sub.rooms.push_back(cor);
                // The arrival storey seeds the corridor at the stair head
                // (the facade-row restriction is off without a street root);
                // the departure storey ties it to the shaft with a door link.
                if (has_arrival) sub.entrance_into.push_back("corridor");
                for (const StairCore& st : res.stairs)
                    if (st.from == k)
                        sub.links.push_back({"corridor", st.shaft_room.id, kLinkDoor, "door"});
            }
            GrowInput in;
            in.w = res.fields.w;
            in.h = res.fields.h;
            in.origin = res.fields.origin;
            in.outside = res.fields.mask[static_cast<size_t>(k)];
            in.useful = res.fields.walk[static_cast<size_t>(k)];
            in.storey_index = k;
            OpeningRules rules;
            rules.street_doors = k == 0;
            for (const StairCore& st : res.stairs) {
                if (st.from == k) in.pinned.push_back(st.shaft_room);
                if (st.to == k) {
                    rules.arrival_cells.insert(rules.arrival_cells.end(),
                                               st.arrival_cells.begin(),
                                               st.arrival_cells.end());
                    // The arrival seeds the growth desirability (§7.1).
                    glm::dvec2 c{0.0};
                    for (const int cell : st.arrival_cells)
                        c += glm::dvec2(cell % in.w + 0.5, cell / in.w + 0.5);
                    if (!st.arrival_cells.empty())
                        in.entrance_cell = c / static_cast<double>(st.arrival_cells.size());
                }
            }

            // Per-storey re-grow loop (as plan_floor): the pinned shaft is
            // re-placed verbatim by the growth, the anneal moves never touch
            // it; keep the lexicographic best over the attempts.
            auto plan_storey = [&](const ExpandedProgram& sub_prog) {
                PlanResult best_s;
                const int attempts = std::max(1, params.max_attempts);
                for (int a = 0; a < attempts; ++a) {
                    Rng grng = arng.split(0xB3331000ull + static_cast<uint64_t>(k * 64 + a));
                    FloorPlan grown = grow_floor_cells(in, sub_prog, params, grng, err);
                    if (!err.empty()) {
                        if (best_s.error.empty()) best_s.error = err;
                        continue;
                    }
                    const FloorPlan* below =
                        k > 0 ? &res.storeys[static_cast<size_t>(k - 1)].plan : nullptr;
                    PlanResult r =
                        anneal_plan(std::move(grown), sub_prog, params, rules, below, grng);
                    if (!r.ok) {
                        if (best_s.error.empty()) best_s.error = r.error;
                        continue;
                    }
                    if (!best_s.ok || lex_better(r.cost, best_s.cost)) best_s = std::move(r);
                    if (best_s.ok && best_s.cost.hard == 0) break;
                }
                return best_s;
            };

            PlanResult best_k = plan_storey(sub);
            // Shape fallback (B3): the area-level G3 can over-admit optional
            // rooms to a storey whose field (the attic walk mask, the stair
            // well voids) cannot host their shapes — the anneal then ends
            // with hard terms no retry resolves. Drop the trailing optional
            // rooms one at a time and re-plan the storey (§4.1: an optional
            // room exists only where it fits).
            while (best_k.ok && best_k.cost.hard > 0) {
                int opt = -1;
                for (size_t qi = 0; qi < sub.rooms.size(); ++qi)
                    if (sub.rooms[static_cast<size_t>(qi)].optional) opt = static_cast<int>(qi);
                if (opt < 0) break;
                const std::string dropped = sub.rooms[static_cast<size_t>(opt)].id;
                sub.rooms.erase(sub.rooms.begin() + opt);
                PlanResult retry = plan_storey(sub);
                if (!retry.ok) break;  // keep the hard-but-placed result
                for (size_t qi = 0; qi < prog.rooms.size(); ++qi)
                    if (prog.rooms[qi].id == dropped && res.storey_of[qi] == k) {
                        res.storey_of[qi] = -1;
                        break;
                    }
                retry.anneal.log.push_back("dropped optional '" + dropped +
                                           "' (hard terms after retries)");
                best_k = std::move(retry);
            }
            if (!best_k.ok) {
                res.error = "storey '" + ir.storeys[static_cast<size_t>(k)].id +
                            "': " + (best_k.error.empty() ? "G5/G6: no valid plan" : best_k.error);
                failed = true;
                break;
            }
            hard_sum += best_k.cost.hard;
            soft_sum += best_k.cost.soft;
            res.storeys[static_cast<size_t>(k)] = std::move(best_k);
        }
        if (failed) {
            best.error = res.error;
            if (std::getenv("BTG_DEBUG_DUMP")) {
                std::string d = "attempt failed: " + res.error + "\nstoreys:";
                for (const auto& st : ir.storeys) d += " " + st.id;
                d += "\nassignment:";
                for (size_t qi = 0; qi < prog.rooms.size(); ++qi)
                    d += " " + prog.rooms[qi].id + "=" + std::to_string(res.storey_of[qi]);
                d += "\nstairs:";
                for (const auto& st : res.stairs)
                    d += " " + st.id + "(" + std::to_string(st.from) + "->" +
                         std::to_string(st.to) + ")";
                std::fprintf(stderr, "%s\n", d.c_str());
            }
            continue;
        }
        res.ok = true;
        const bool better =
            best_hard < 0 || hard_sum < best_hard ||
            (hard_sum == best_hard && soft_sum < best_soft - 1e-12);
        if (better) {
            best_hard = hard_sum;
            best_soft = soft_sum;
            best = std::move(res);
        }
        if (best_hard == 0) break;
    }
    return best;
}

}  // namespace building_topology_generator
