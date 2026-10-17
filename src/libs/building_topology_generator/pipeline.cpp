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
    // Stack cell-sets (normalize_stack_cells keys) of the attempts that did
    // not reach hard == 0: an exact re-pick can only repeat the same failure
    // (place_chimneys's scoring is dominated by geometry), so the next attempt
    // is banned from it and explores the next-best position.
    std::vector<std::vector<glm::ivec2>> tried_stacks;
    const auto ban_stacks = [&tried_stacks](const std::vector<ChimneyCore>& cores) {
        for (const ChimneyCore& cc : cores) {
            std::vector<glm::ivec2> key = normalize_stack_cells(cc.cells);
            if (std::find(tried_stacks.begin(), tried_stacks.end(), key) == tried_stacks.end())
                tried_stacks.push_back(std::move(key));
        }
    };

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
        // G4b (§9.2): the chimney stacks — after the storey assignment (the
        // hearth rooms' storeys are known), before the floor plans (the cores
        // eat cells on every crossed storey and seed the hearth rooms).
        if (!place_chimneys(ir, res.fields, res.stairs, prog, res.storey_of, params, arng,
                            tried_stacks, res.chimneys, err)) {
            ban_stacks(res.chimneys);  // partial picks led to the failure
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
            bool has_shaft = false, has_arrival = false, corridor_present = false,
                 corridor_shaft_linked = false;
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
                    if (st.from == k) {
                        sub.links.push_back({"corridor", st.shaft_room.id, kLinkDoor, "door"});
                        corridor_shaft_linked = true;
                    }
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
            rules.full_height =
                ir.storeys[static_cast<size_t>(k)].kind_code != kStoreyAttic;
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
            // B4 (§9.2): a hearth room seeds only next to its chimney stack.
            // A cell that is also a neighbor of an EARLIER stack is claimed
            // by that stack's rooms (place_chimneys reserves the frontage) —
            // two rooms after the same cell end with a seed failure.
            for (size_t ci = 0; ci < res.chimneys.size(); ++ci) {
                const ChimneyCore& cc = res.chimneys[ci];
                if (k < cc.storey0 || k > cc.storey1) continue;
                std::vector<glm::ivec2> seed_set;
                for (const int qi : cc.hearths) {
                    if (res.storey_of[qi] != k) continue;
                    if (seed_set.empty()) {
                        seed_set = hearth_seed_cells(cc, res.fields, k, res.stairs);
                        for (size_t ej = 0; ej < ci; ++ej) {
                            const ChimneyCore& other = res.chimneys[ej];
                            if (k < other.storey0 || k > other.storey1) continue;
                            const std::vector<glm::ivec2> claim =
                                hearth_seed_cells(other, res.fields, k, res.stairs);
                            seed_set.erase(
                                std::remove_if(seed_set.begin(), seed_set.end(),
                                               [&](const glm::ivec2& c) {
                                                   return std::find(claim.begin(),
                                                                    claim.end(),
                                                                    c) != claim.end();
                                               }),
                                seed_set.end());
                        }
                    }
                    if (!seed_set.empty()) in.seed_next_to[prog.rooms[qi].id] = seed_set;
                }
            }
            // The auto-corridor must door into the shaft it serves (the link
            // above): seed it at the shaft's edge, same never-relaxed
            // mechanism as the hearth seeding. Without it a tight field (a
            // chimney stack shaving the storey) can leave the corridor
            // winged off the shaft with no legal anneal move left. Only the
            // shaft's fully door-ok sides qualify (the long flight side and
            // the far end, §9.1): a contact anywhere else cannot host the
            // door no matter how long the shared run is.
            if (corridor_shaft_linked) {
                std::vector<glm::ivec2> seed_set =
                    corridor_seed_cells(in.outside, in.w, in.h, res.stairs, k);
                if (!seed_set.empty()) in.seed_next_to["corridor"] = seed_set;
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
                    if (std::getenv("BTG_DEBUG_DUMP") && err.empty()) {
                        std::string t;
                        for (size_t ri = 0; ri < grown.rooms.size(); ++ri) {
                            const PlanRoom& rm = grown.rooms[ri];
                            if (rm.hearth_code == 0) continue;
                            bool touch = false;
                            static const int di[4] = {-1, 1, 0, 0};
                            static const int dj[4] = {0, 0, -1, 1};
                            for (int j = rm.z0; j < rm.z1 && !touch; ++j)
                                for (int i = rm.x0; i < rm.x1 && !touch; ++i)
                                    for (int q = 0; q < 4; ++q) {
                                        const int ni = i + di[q], nj = j + dj[q];
                                        if (ni < 0 || nj < 0 || ni >= grown.w ||
                                            nj >= grown.h)
                                            continue;
                                        if (grown.outside[static_cast<size_t>(nj) * grown.w +
                                                          ni] == kCellCore) {
                                            touch = true;
                                            break;
                                        }
                                    }
                            t += " " + rm.id + (touch ? "(touch)" : "(AWAY)") + "[" +
                                 std::to_string(rm.x0) + "," + std::to_string(rm.z0) + "-" +
                                 std::to_string(rm.x1) + "," + std::to_string(rm.z1) + "]";
                        }
                        if (!t.empty())
                            std::fprintf(stderr, "storey %d attempt %d grown:%s\n", k, a,
                                         t.c_str());
                    }
                    if (!err.empty()) {
                        if (best_s.error.empty()) best_s.error = err;
                        continue;
                    }
                    const FloorPlan* below =
                        k > 0 ? &res.storeys[static_cast<size_t>(k - 1)].plan : nullptr;
                    PlanResult r =
                        anneal_plan(std::move(grown), sub_prog, params, rules, below, grng);
                    if (std::getenv("BTG_DEBUG_DUMP") && r.ok && r.cost.hard > 0) {
                        std::string t;
                        for (const auto& [name, cnt] : r.cost.hard_terms)
                            t += " " + name + "=" + std::to_string(cnt);
                        std::string miss;
                        for (const auto& [ma, mb] : r.openings.missing_door_links)
                            miss += " " + r.plan.rooms[static_cast<size_t>(ma)].id + "~" +
                                    r.plan.rooms[static_cast<size_t>(mb)].id;
                        if (!r.openings.entrance_ok) miss += " !entrance";
                        if (!r.openings.back_door_ok) miss += " !back_door";
                        // Which hearth room is off the core, and where did it
                        // end vs the grown state (the anneal's initial cost).
                        std::string ht;
                        if (r.cost.hard_terms.count("hearth") &&
                            r.cost.hard_terms.at("hearth") > 0) {
                            static const int di[4] = {-1, 1, 0, 0};
                            static const int dj[4] = {0, 0, -1, 1};
                            for (size_t ri = 0; ri < r.plan.rooms.size(); ++ri) {
                                const PlanRoom& rm = r.plan.rooms[ri];
                                if (rm.hearth_code == 0) continue;
                                bool touch = false;
                                for (int j = 0; j < r.plan.h && !touch; ++j)
                                    for (int i = 0; i < r.plan.w && !touch; ++i) {
                                        if (r.plan.at(i, j) != kCellCore) continue;
                                        for (int q = 0; q < 4; ++q)
                                            if (r.plan.room_at(i + di[q], j + dj[q]) ==
                                                static_cast<int32_t>(ri)) {
                                                touch = true;
                                                break;
                                            }
                                    }
                                ht += " " + rm.id + (touch ? "(touch)" : "(AWAY)") + "[" +
                                      std::to_string(rm.x0) + "," + std::to_string(rm.z0) +
                                      "-" + std::to_string(rm.x1) + "," +
                                      std::to_string(rm.z1) + "]w" +
                                      std::to_string(rm.wings.size());
                            }
                        }
                        std::fprintf(stderr,
                                     "storey %d attempt %d: hard=%d soft=%.2f moves=%d/%d "
                                     "terms:%s missing:%s%s\n",
                                     k, a, r.cost.hard, r.cost.soft, r.anneal.moves_accepted,
                                     r.anneal.moves_tried, t.c_str(), miss.c_str(),
                                     ht.c_str());
                        std::string rm_dump;
                        for (const PlanRoom& rm : r.plan.rooms) {
                            rm_dump += " " + rm.id + "[" + std::to_string(rm.x0) + "," +
                                       std::to_string(rm.z0) + "-" + std::to_string(rm.x1) +
                                       "," + std::to_string(rm.z1) + "]";
                            if (!rm.wings.empty())
                                rm_dump += "w" + std::to_string(rm.wings.size());
                            if (rm.fixed()) rm_dump += "F";
                        }
                        std::fprintf(stderr, "  rooms:%s\n", rm_dump.c_str());
                        for (const std::string& l : r.anneal.log)
                            std::fprintf(stderr, "  | %s\n", l.c_str());
                        std::string it;
                        for (const auto& [name, cnt] : r.anneal.initial.hard_terms)
                            it += " " + name + "=" + std::to_string(cnt);
                        std::fprintf(stderr, "  grown hard=%d terms:%s\n",
                                     r.anneal.initial.hard, it.c_str());
                    }
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
            // room exists only where it fits). B4: the same fallback rescues
            // a storey whose GROWTH never produced a plan at all (the repair
            // can fail outright when a chimney stack strips the field, §9.2)
            // — a dropped optional may leave a feasible field.
            while (!best_k.ok || best_k.cost.hard > 0) {
                int opt = -1;
                for (size_t qi = 0; qi < sub.rooms.size(); ++qi)
                    if (sub.rooms[static_cast<size_t>(qi)].optional &&
                        sub.rooms[static_cast<size_t>(qi)].hearth_code == 0)
                        opt = static_cast<int>(qi);
                if (opt < 0) break;  // a hearth room is structural (§9.2): never dropped
                const std::string dropped = sub.rooms[static_cast<size_t>(opt)].id;
                sub.rooms.erase(sub.rooms.begin() + opt);
                PlanResult retry = plan_storey(sub);
                if (!retry.ok) {
                    if (best_k.ok) break;  // keep the hard-but-placed result
                    continue;              // nothing placed yet — drop deeper
                }
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
            ban_stacks(res.chimneys);
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
        if (hard_sum > 0) ban_stacks(res.chimneys);  // before the move below
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
