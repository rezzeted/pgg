// G6 (B2): derive doors and windows from a FloorPlan. See openings.h.

#include "pch.h"

#include "openings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <queue>
#include <set>

#include "codes.h"

namespace building_topology_generator {

namespace {

constexpr double kInf = 1e30;
constexpr int kOutside = -2;  // room_b marker for the street

// A contiguous cell run on a grid line with the same left/right pair.
struct Run {
    bool vertical = false;  // wall runs along z at x = line
    int line = 0;
    int c0 = 0, c1 = 0;  // cell range along the wall
    int len() const { return c1 - c0; }
};

// Pass price (§7.1): halls/corridors are free, then by zone.
double pass_price(const PlanRoom& r) {
    if (r.fn_code == kFnCorridor || r.fn_code == kFnHall || r.fn_code == kFnLobby)
        return 0.0;
    switch (r.zone_code) {
        case kZonePublic: return 0.5;
        case kZoneService: return 2.0;
        case kZonePrivate: return 10.0;
        default: return 1.0;
    }
}

// All cell edges of the plan grouped into runs: shared runs per room pair,
// exterior runs per room per side (0=-x 1=+x 2=-z 3=+z).
struct PlanScan {
    std::map<std::pair<int, int>, std::vector<Run>> shared;
    std::vector<std::array<std::vector<Run>, 4>> exterior;  // per room
    std::vector<std::pair<int, int>> adjacency;
};

PlanScan scan_plan(const FloorPlan& fp) {
    PlanScan scan;
    scan.exterior.resize(fp.rooms.size());
    auto left_of = [&](int X, int z) { return X > 0 ? fp.at(X - 1, z) : int32_t(kCellOutside); };
    auto right_of = [&](int X, int z) { return X < fp.w ? fp.at(X, z) : int32_t(kCellOutside); };
    auto below_of = [&](int i, int Z) { return Z > 0 ? fp.at(i, Z - 1) : int32_t(kCellOutside); };
    auto above_of = [&](int i, int Z) { return Z < fp.h ? fp.at(i, Z) : int32_t(kCellOutside); };
    for (int X = 0; X <= fp.w; ++X)
        for (int z = 0; z < fp.h;) {
            const int32_t L = left_of(X, z), R = right_of(X, z);
            if (L == R) {
                ++z;
                continue;
            }
            int z1 = z + 1;
            while (z1 < fp.h && left_of(X, z1) == L && right_of(X, z1) == R) ++z1;
            const Run run{true, X, z, z1};
            if (L >= 0 && R >= 0) {
                scan.shared[{std::min(L, R), std::max(L, R)}].push_back(run);
            } else if (L >= 0) {
                scan.exterior[static_cast<size_t>(L)][1].push_back(run);  // +x side
            } else if (R >= 0) {
                scan.exterior[static_cast<size_t>(R)][0].push_back(run);  // -x side
            }
            z = z1;
        }
    for (int Z = 0; Z <= fp.h; ++Z)
        for (int i = 0; i < fp.w;) {
            const int32_t D = below_of(i, Z), U = above_of(i, Z);
            if (D == U) {
                ++i;
                continue;
            }
            int i1 = i + 1;
            while (i1 < fp.w && below_of(i1, Z) == D && above_of(i1, Z) == U) ++i1;
            const Run run{false, Z, i, i1};
            if (D >= 0 && U >= 0) {
                scan.shared[{std::min(D, U), std::max(D, U)}].push_back(run);
            } else if (D >= 0) {
                scan.exterior[static_cast<size_t>(D)][3].push_back(run);  // +z side
            } else if (U >= 0) {
                scan.exterior[static_cast<size_t>(U)][2].push_back(run);  // -z side
            }
            i = i1;
        }
    for (const auto& [pair, runs] : scan.shared) scan.adjacency.push_back(pair);
    return scan;
}

std::string door_key(const FloorPlan& fp, int a, int b) {
    const std::string sa = a >= 0 ? fp.rooms[static_cast<size_t>(a)].id : "outside";
    const std::string sb = b >= 0 ? fp.rooms[static_cast<size_t>(b)].id : "outside";
    return sa < sb ? sa + "|" + sb : sb + "|" + sa;
}

// Door center in cell coords for a candidate position.
glm::dvec2 door_center(const Run& run, int c0, int door_cells) {
    return run.vertical ? glm::dvec2(run.line, c0 + door_cells / 2.0)
                        : glm::dvec2(c0 + door_cells / 2.0, run.line);
}

double corner_dist(const PlanRoom& room, const glm::dvec2& c) {
    double best = kInf;
    const double xs[2] = {static_cast<double>(room.x0), static_cast<double>(room.x1)};
    const double zs[2] = {static_cast<double>(room.z0), static_cast<double>(room.z1)};
    for (double x : xs)
        for (double z : zs) best = std::min(best, glm::length(c - glm::dvec2(x, z)));
    return best;
}

// Pick the door position on one of the runs: memo-sticky when still valid,
// else the run/c0 minimizing the summed distance to the nearest corners of
// both rooms (§7.1) — for entrance doors the distance to the facade center.
// Returns false when no run can host a door.
bool place_door(const FloorPlan& fp, int a, int b, const std::vector<Run>& runs,
                const PlanParams& params, bool entrance, OpeningMemo& memo, DerivedOpening& out) {
    const int need = params.door_cells;
    const int m = params.door_margin;
    const std::string key = door_key(fp, a, b);
    // Memo: the same line and a still-valid span?
    if (auto it = memo.doors.find(key); it != memo.doors.end()) {
        const auto& [vertical, line, m0] = it->second;
        const double s = params.ir.cell;
        // Span coordinate: z along a vertical wall, x along a horizontal one.
        const double line_m = vertical ? fp.origin.y : fp.origin.x;
        const int c0 = static_cast<int>(std::lround((m0 - line_m) / s));
        for (const Run& run : runs)
            if (run.vertical == vertical && run.line == line && c0 >= run.c0 + m &&
                c0 + need <= run.c1 - m) {
                out.vertical = vertical;
                out.line = line;
                out.m0 = m0;
                out.m1 = m0 + need * s;
                return true;
            }
    }
    bool found = false;
    double best_score = kInf;
    Run best_run{};
    int best_c0 = 0;
    const double facade_cx = fp.w / 2.0;
    for (const Run& run : runs) {
        if (run.len() < need + 2 * m) continue;
        for (int c0 = run.c0 + m; c0 + need <= run.c1 - m; ++c0) {
            const glm::dvec2 c = door_center(run, c0, need);
            double score;
            if (entrance) {
                score = std::abs(c.x - facade_cx);
            } else {
                score = corner_dist(fp.rooms[static_cast<size_t>(a)], c);
                if (b >= 0) score += corner_dist(fp.rooms[static_cast<size_t>(b)], c);
            }
            if (score < best_score - 1e-9) {
                best_score = score;
                best_run = run;
                best_c0 = c0;
                found = true;
            }
        }
    }
    if (!found) return false;
    const double s = params.ir.cell;
    const double line_m = best_run.vertical ? fp.origin.y : fp.origin.x;
    out.vertical = best_run.vertical;
    out.line = best_run.line;
    out.m0 = line_m + best_c0 * s;
    out.m1 = line_m + (best_c0 + need) * s;
    memo.doors[key] = {out.vertical, out.line, out.m0};
    return true;
}

}  // namespace

int DerivedOpenings::door_count() const {
    int n = 0;
    for (const DerivedOpening& o : openings)
        if (o.kind_code == kOpeningDoor) ++n;
    return n;
}

DerivedOpenings derive_openings(const FloorPlan& fp, const ExpandedProgram& prog,
                                const PlanParams& params, OpeningMemo& memo) {
    DerivedOpenings out;
    const int N = static_cast<int>(fp.rooms.size());
    const PlanScan scan = scan_plan(fp);
    out.adjacency = scan.adjacency;
    const double s = params.ir.cell;
    const int min_run = params.door_cells + 2 * params.door_margin;

    // Room indices by id (links are instance-resolved, ids match exactly).
    auto room_index = [&](const std::string& id) { return fp.find_room(id); };

    // no_door pairs (§4.1): never a door, never a Dijkstra edge.
    std::set<std::pair<int, int>> no_door;
    for (const ProgramLink& l : prog.links) {
        if (l.kind != kLinkNoDoor) continue;
        const int a = room_index(l.a), b = room_index(l.b);
        if (a >= 0 && b >= 0) no_door.insert({std::min(a, b), std::max(a, b)});
    }
    auto door_capable = [&](int a, int b) {
        if (no_door.count({std::min(a, b), std::max(a, b)})) return false;
        const auto it = scan.shared.find({std::min(a, b), std::max(a, b)});
        if (it == scan.shared.end()) return false;
        for (const Run& run : it->second)
            if (run.len() >= min_run) return true;
        return false;
    };
    auto runs_of = [&](int a, int b) -> const std::vector<Run>& {
        static const std::vector<Run> empty;
        const auto it = scan.shared.find({std::min(a, b), std::max(a, b)});
        return it == scan.shared.end() ? empty : it->second;
    };
    // Facade runs of a room for the street door: front = +z side (3), back =
    // -z side (2), door-capable length.
    auto facade_runs = [&](int r, bool front) -> std::vector<Run> {
        std::vector<Run> out_runs;
        for (const Run& run : scan.exterior[static_cast<size_t>(r)][front ? 3 : 2])
            if (run.len() >= min_run) out_runs.push_back(run);
        return out_runs;
    };

    // -- Dijkstra from the street (§7.1) ------------------------------------
    // Edge a -> b costs 1 + pass(a); the street has pass 0. The program's
    // entrance/back_door "into" lists are ALLOWED sets (§4.1): exactly one
    // street door each, hosted by the allowed room whose facade run is
    // closest to the facade center.
    std::vector<std::vector<std::pair<int, double>>> adj(static_cast<size_t>(N) + 1);
    const int OUT = N;
    for (const auto& [pair, runs] : scan.shared) {
        const int a = pair.first, b = pair.second;
        if (!door_capable(a, b)) continue;
        adj[static_cast<size_t>(a)].push_back({b, 1.0 + pass_price(fp.rooms[static_cast<size_t>(a)])});
        adj[static_cast<size_t>(b)].push_back({a, 1.0 + pass_price(fp.rooms[static_cast<size_t>(b)])});
    }
    // The entrance host: an allowed room with a front run, the run center
    // closest to the facade center; ties by room order (deterministic).
    auto pick_street_host = [&](bool front) -> int {
        int best = -1;
        double best_d = kInf;
        for (int r = 0; r < N; ++r) {
            const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
            if (!(front ? room.entrance_front : room.entrance_back)) continue;
            for (const Run& run : facade_runs(r, front)) {
                const double cx = (run.c0 + run.c1) / 2.0;
                const double d = std::fabs(cx - fp.w / 2.0);
                if (d < best_d - 1e-9) {
                    best_d = d;
                    best = r;
                }
            }
        }
        return best;
    };
    const int entrance_room = pick_street_host(true);
    const int back_room = prog.has_back_door ? pick_street_host(false) : -1;
    auto add_street_edge = [&](int r) {
        adj[static_cast<size_t>(OUT)].push_back({r, 1.0});
        adj[static_cast<size_t>(r)].push_back({OUT, 1.0 + pass_price(fp.rooms[static_cast<size_t>(r)])});
    };
    if (entrance_room >= 0) add_street_edge(entrance_room);
    if (back_room >= 0 && back_room != entrance_room) add_street_edge(back_room);
    std::vector<double> dist(static_cast<size_t>(N) + 1, kInf);
    std::vector<int> parent(static_cast<size_t>(N) + 1, -1);
    using QItem = std::pair<double, int>;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<>> pq;
    dist[static_cast<size_t>(OUT)] = 0.0;
    pq.push({0.0, OUT});
    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[static_cast<size_t>(u)] + 1e-12) continue;
        for (const auto& [v, w] : adj[static_cast<size_t>(u)]) {
            const double nd = d + w;
            if (nd < dist[static_cast<size_t>(v)] - 1e-12) {
                dist[static_cast<size_t>(v)] = nd;
                parent[static_cast<size_t>(v)] = u;
                pq.push({nd, v});
            }
        }
    }
    out.depth.assign(dist.begin(), dist.end() - 1);
    out.reachable.assign(static_cast<size_t>(N), 0);
    for (int r = 0; r < N; ++r)
        out.reachable[static_cast<size_t>(r)] = dist[static_cast<size_t>(r)] < kInf / 2 ? 1 : 0;

    // -- Doors ---------------------------------------------------------------
    // Tree doors (one per room, toward the parent), then program door links,
    // then public loops (§7.1: a loop between public rooms whose depths
    // differ by >= 2).
    std::set<std::pair<int, int>> has_door;  // (a, b), b == kOutside for the street
    std::vector<char> street_door_side(static_cast<size_t>(N), 0);  // 1 = front, 2 = back
    auto add_door = [&](int a, int b, const std::vector<Run>& runs, bool entrance) {
        DerivedOpening o;
        o.kind_code = kOpeningDoor;
        o.room_a = a;
        o.room_b = b;
        o.type = "door_plain";
        o.y0 = 0.0;
        o.y1 = params.door_h;
        if (!place_door(fp, a, b, runs, params, entrance, memo, o)) return false;
        out.openings.push_back(o);
        has_door.insert({a, b});
        return true;
    };
    for (int r = 0; r < N; ++r) {
        const int p = parent[static_cast<size_t>(r)];
        if (p < 0) continue;
        if (p == OUT) {
            // Only the chosen street hosts have a street edge; the entrance
            // goes to the front facade, the back door to the back.
            const bool front = r == entrance_room;
            if (add_door(r, kOutside, facade_runs(r, front), /*entrance=*/true))
                street_door_side[static_cast<size_t>(r)] |= front ? 1 : 2;
        } else {
            add_door(r, p, runs_of(r, p), /*entrance=*/false);
        }
    }
    // A room hosting both the entrance and the back door gets the back door
    // as an extra (the tree placed the front one).
    if (back_room >= 0 && !(street_door_side[static_cast<size_t>(back_room)] & 2))
        if (add_door(back_room, kOutside, facade_runs(back_room, false), /*entrance=*/true))
            street_door_side[static_cast<size_t>(back_room)] |= 2;
    out.entrance_ok =
        entrance_room >= 0 && (street_door_side[static_cast<size_t>(entrance_room)] & 1) != 0;
    out.back_door_ok = !prog.has_back_door ||
                       (back_room >= 0 &&
                        (street_door_side[static_cast<size_t>(back_room)] & 2) != 0);

    for (const ProgramLink& l : prog.links) {
        if (l.kind != kLinkDoor) continue;
        const int a = room_index(l.a), b = room_index(l.b);
        if (a < 0 || b < 0) continue;
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        if (has_door.count({a, b}) || has_door.count({b, a})) continue;
        if (no_door.count(key)) continue;
        if (!door_capable(a, b) || !add_door(a, b, runs_of(a, b), /*entrance=*/false))
            out.missing_door_links.emplace_back(a, b);
    }
    for (const auto& [pair, runs] : scan.shared) {
        const int a = pair.first, b = pair.second;
        if (has_door.count({a, b}) || has_door.count({b, a})) continue;
        if (fp.rooms[static_cast<size_t>(a)].zone_code != kZonePublic ||
            fp.rooms[static_cast<size_t>(b)].zone_code != kZonePublic)
            continue;
        if (!out.reachable[static_cast<size_t>(a)] || !out.reachable[static_cast<size_t>(b)])
            continue;
        if (std::fabs(dist[static_cast<size_t>(a)] - dist[static_cast<size_t>(b)]) < 2.0)
            continue;
        if (door_capable(a, b)) add_door(a, b, runs, /*entrance=*/false);
    }

    // -- Windows (§7.2) -------------------------------------------------------
    // Bay axes per building side; a window centers on an axis inside its
    // exterior run with clearance for half the window + a pier.
    const double W_m = fp.w * s, H_m = fp.h * s;
    const double win_w = params.window_area / params.window_h;
    const double pier = 0.25;
    const double clr = win_w / 2 + pier;
    const int n_front = std::max(1, static_cast<int>(std::lround(W_m / params.bay_target)));
    const int n_side = std::max(1, static_cast<int>(std::lround(H_m / params.bay_target)));
    std::vector<double> axes_x(static_cast<size_t>(n_front)), axes_z(static_cast<size_t>(n_side));
    for (int k = 0; k < n_front; ++k)
        axes_x[static_cast<size_t>(k)] = fp.origin.x + (k + 0.5) * W_m / n_front;
    for (int k = 0; k < n_side; ++k)
        axes_z[static_cast<size_t>(k)] = fp.origin.y + (k + 0.5) * H_m / n_side;

    // Street doors block window slots on the same exterior run.
    // (Interior doors sit on partitions windows never touch.)
    std::vector<std::vector<std::pair<double, double>>> blocked(static_cast<size_t>(N));
    for (const DerivedOpening& o : out.openings) {
        if (o.kind_code != kOpeningDoor || o.room_b != kOutside) continue;
        blocked[static_cast<size_t>(o.room_a)].push_back({o.m0 - pier, o.m1 + pier});
    }

    const bool strict = prog.style.regularity == "strict";
    // Side priority: front (+z), back (-z), then the x sides.
    const int side_order[4] = {3, 2, 0, 1};
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (!room.habitable) continue;
        const double area = fp.room_area_net(r, params.ir);
        const int need =
            std::max(1, static_cast<int>(
                            std::ceil(area * params.ir.glass_ratio / params.window_area)));
        int placed = 0;
        for (int side : side_order) {
            if (placed >= need) break;
            const auto& runs = scan.exterior[static_cast<size_t>(r)][side];
            if (runs.empty()) continue;
            const bool vertical = side < 2;
            // Span coordinate: z along a vertical wall, x along a horizontal.
            const double line_m = vertical ? fp.origin.y : fp.origin.x;
            // Valid axis slots on this side.
            std::vector<double> slots;
            for (double axis : (vertical ? axes_z : axes_x)) {
                for (const Run& run : runs) {
                    const double r0 = line_m + run.c0 * s, r1 = line_m + run.c1 * s;
                    if (axis < r0 + clr || axis > r1 - clr) continue;
                    bool blocked_hit = false;
                    for (const auto& [b0, b1] : blocked[static_cast<size_t>(r)])
                        if (axis + win_w / 2 > b0 && axis - win_w / 2 < b1) {
                            blocked_hit = true;
                            break;
                        }
                    if (!blocked_hit) {
                        slots.push_back(axis);
                        break;
                    }
                }
            }
            if (slots.empty()) continue;
            // Segment center: the middle of the room's side span in meters.
            const double center =
                vertical ? fp.origin.y + (room.z0 + room.z1) * s / 2.0
                         : fp.origin.x + (room.x0 + room.x1) * s / 2.0;
            std::sort(slots.begin(), slots.end(), [&](double p, double q) {
                const double dp = std::fabs(p - center), dq = std::fabs(q - center);
                if (dp != dq) return dp < dq;
                return p < q;
            });
            // Memo: pin the previous axis when it is still a valid slot.
            const std::string mkey = room.id + "|" + std::to_string(side);
            std::vector<double> chosen;
            if (auto it = memo.window_axes.find(mkey); it != memo.window_axes.end()) {
                const double pinned = it->second;
                if (std::find(slots.begin(), slots.end(), pinned) != slots.end())
                    chosen.push_back(pinned);
            }
            if (strict) {
                // Symmetric pairs around the center first, the center slot last.
                std::vector<char> used(slots.size(), 0);
                for (size_t i = 0; i < slots.size(); ++i) {
                    if (used[i]) continue;
                    const double mirror = 2 * center - slots[i];
                    int j = -1;
                    for (size_t k = i + 1; k < slots.size(); ++k)
                        if (!used[k] && std::fabs(slots[k] - mirror) < 0.3) {
                            j = static_cast<int>(k);
                            break;
                        }
                    if (j >= 0) {
                        used[i] = 1;
                        used[static_cast<size_t>(j)] = 1;
                        chosen.push_back(slots[i]);
                        chosen.push_back(slots[static_cast<size_t>(j)]);
                    }
                }
                for (size_t i = 0; i < slots.size(); ++i)
                    if (!used[i] && std::fabs(slots[i] - center) < 0.3) {
                        used[i] = 1;
                        chosen.push_back(slots[i]);
                        break;
                    }
                for (size_t i = 0; i < slots.size() && chosen.size() < static_cast<size_t>(need);
                     ++i)
                    if (!used[i]) {
                        used[i] = 1;
                        chosen.push_back(slots[i]);
                    }
            } else {
                for (double p : slots)
                    if (std::find(chosen.begin(), chosen.end(), p) == chosen.end())
                        chosen.push_back(p);
            }
            // Emit from the chosen list, keeping a min gap on the wall.
            std::vector<double> emitted;
            for (double p : chosen) {
                if (placed >= need) break;
                bool clash = false;
                for (double q : emitted)
                    if (std::fabs(p - q) < win_w + 2 * pier) {
                        clash = true;
                        break;
                    }
                if (clash) continue;
                DerivedOpening o;
                o.kind_code = kOpeningWindow;
                o.room_a = r;
                o.room_b = kOutside;
                o.vertical = vertical;
                // The wall line index of this side of the room.
                o.line = side == 0 ? room.x0 : side == 1 ? room.x1
                         : side == 2 ? room.z0
                                     : room.z1;
                o.m0 = p - win_w / 2;
                o.m1 = p + win_w / 2;
                o.y0 = params.sill_y;
                o.y1 = params.sill_y + params.window_h;
                o.on_axis = true;
                o.type = "win_flat";
                out.openings.push_back(o);
                emitted.push_back(p);
                ++placed;
            }
            if (!emitted.empty()) memo.window_axes[mkey] = emitted.front();
        }
        // No axis slot found (short runs, narrow annex walls, ...): center
        // the missing windows on the longest exterior run, off-axis (C6
        // penalizes, I6 satisfied).
        int fallback = 0;
        while (placed < need) {
            const Run* longest = nullptr;
            for (int side : side_order)
                for (const Run& run : scan.exterior[static_cast<size_t>(r)][side])
                    if (!longest || run.len() > longest->len()) longest = &run;
            if (!longest) break;
            const bool vertical = longest->vertical;
            const double line_m = vertical ? fp.origin.y : fp.origin.x;
            const double r0 = line_m + longest->c0 * s, r1 = line_m + longest->c1 * s;
            double mid = (r0 + r1) / 2.0 + fallback * (win_w + 2 * pier);
            if (r1 - r0 > win_w) mid = std::clamp(mid, r0 + win_w / 2, r1 - win_w / 2);
            DerivedOpening o;
            o.kind_code = kOpeningWindow;
            o.room_a = r;
            o.room_b = kOutside;
            o.vertical = vertical;
            o.line = longest->line;
            o.m0 = mid - win_w / 2;
            o.m1 = mid + win_w / 2;
            o.y0 = params.sill_y;
            o.y1 = params.sill_y + params.window_h;
            o.on_axis = false;
            o.type = "win_flat";
            out.openings.push_back(o);
            ++placed;
            ++fallback;
        }
    }

    return out;
}

}  // namespace building_topology_generator
