// L0 anneal (algorithms §8, §11). See anneal.h.

#include "pch.h"

#include "anneal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "codes.h"

namespace building_topology_generator {

namespace {

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------

// A line-shift unit: the maximal cell range on an interior grid line where
// the low-side rooms end exactly at the line and the high-side rooms start
// exactly at it. Shifting the line by one cell extends one side and shrinks
// the other; every room stays a rectangle and the tiling stays gapless.
struct ShiftUnit {
    bool vertical = false;  // vertical line (x = line), rooms lo/hi along x
    int line = 0;
    int c0 = 0, c1 = 0;       // cell range along the line
    std::vector<int> lo, hi;  // room indices on the low / high side
};

struct Move {
    bool is_swap = false;
    int swap_a = -1, swap_b = -1;
    ShiftUnit unit;
    int d = 0;  // +-1
};

// Shift units on one vertical line (x = X) of a rasterized plan.
void units_on_vertical_line(const FloorPlan& fp, int X, std::vector<ShiftUnit>& out) {
    std::vector<char> covered(static_cast<size_t>(fp.h), 0);
    for (int z = 0; z < fp.h; ++z) {
        if (covered[static_cast<size_t>(z)]) continue;
        const int32_t L = fp.at(X - 1, z), R = fp.at(X, z);
        if (L < 0 || R < 0 || L == R) continue;
        // A fixed room (the stair shaft, a winged L-room) never moves; no
        // unit crosses it.
        if (fp.rooms[static_cast<size_t>(L)].fixed() ||
            fp.rooms[static_cast<size_t>(R)].fixed())
            continue;
        if (fp.rooms[static_cast<size_t>(L)].x1 != X ||
            fp.rooms[static_cast<size_t>(R)].x0 != X)
            continue;
        // Closure: the unit must cover the full z-range of every room it
        // touches (else the shift would break the rectangle).
        int a = std::min(fp.rooms[static_cast<size_t>(L)].z0,
                         fp.rooms[static_cast<size_t>(R)].z0);
        int b = std::max(fp.rooms[static_cast<size_t>(L)].z1,
                         fp.rooms[static_cast<size_t>(R)].z1);
        bool valid = true, grown = true;
        while (grown && valid) {
            grown = false;
            for (int row = a; row < b && valid; ++row) {
                const int32_t l = fp.at(X - 1, row), r = fp.at(X, row);
                if (l < 0 || r < 0 || l == r ||
                    fp.rooms[static_cast<size_t>(l)].fixed() ||
                    fp.rooms[static_cast<size_t>(r)].fixed() ||
                    fp.rooms[static_cast<size_t>(l)].x1 != X ||
                    fp.rooms[static_cast<size_t>(r)].x0 != X) {
                    valid = false;
                    break;
                }
                if (fp.rooms[static_cast<size_t>(l)].z0 < a) {
                    a = fp.rooms[static_cast<size_t>(l)].z0;
                    grown = true;
                }
                if (fp.rooms[static_cast<size_t>(l)].z1 > b) {
                    b = fp.rooms[static_cast<size_t>(l)].z1;
                    grown = true;
                }
                if (fp.rooms[static_cast<size_t>(r)].z0 < a) {
                    a = fp.rooms[static_cast<size_t>(r)].z0;
                    grown = true;
                }
                if (fp.rooms[static_cast<size_t>(r)].z1 > b) {
                    b = fp.rooms[static_cast<size_t>(r)].z1;
                    grown = true;
                }
            }
        }
        if (!valid) continue;
        ShiftUnit u;
        u.vertical = true;
        u.line = X;
        u.c0 = a;
        u.c1 = b;
        for (int row = a; row < b; ++row) {
            covered[static_cast<size_t>(row)] = 1;
            const int l = fp.at(X - 1, row), r = fp.at(X, row);
            if (std::find(u.lo.begin(), u.lo.end(), l) == u.lo.end()) u.lo.push_back(l);
            if (std::find(u.hi.begin(), u.hi.end(), r) == u.hi.end()) u.hi.push_back(r);
        }
        std::sort(u.lo.begin(), u.lo.end());
        std::sort(u.hi.begin(), u.hi.end());
        out.push_back(std::move(u));
    }
}

void units_on_horizontal_line(const FloorPlan& fp, int Z, std::vector<ShiftUnit>& out) {
    std::vector<char> covered(static_cast<size_t>(fp.w), 0);
    for (int i = 0; i < fp.w; ++i) {
        if (covered[static_cast<size_t>(i)]) continue;
        const int32_t D = fp.at(i, Z - 1), U = fp.at(i, Z);
        if (D < 0 || U < 0 || D == U) continue;
        if (fp.rooms[static_cast<size_t>(D)].fixed() ||
            fp.rooms[static_cast<size_t>(U)].fixed())
            continue;
        if (fp.rooms[static_cast<size_t>(D)].z1 != Z ||
            fp.rooms[static_cast<size_t>(U)].z0 != Z)
            continue;
        int a = std::min(fp.rooms[static_cast<size_t>(D)].x0,
                         fp.rooms[static_cast<size_t>(U)].x0);
        int b = std::max(fp.rooms[static_cast<size_t>(D)].x1,
                         fp.rooms[static_cast<size_t>(U)].x1);
        bool valid = true, grown = true;
        while (grown && valid) {
            grown = false;
            for (int col = a; col < b && valid; ++col) {
                const int32_t d = fp.at(col, Z - 1), u = fp.at(col, Z);
                if (d < 0 || u < 0 || d == u ||
                    fp.rooms[static_cast<size_t>(d)].fixed() ||
                    fp.rooms[static_cast<size_t>(u)].fixed() ||
                    fp.rooms[static_cast<size_t>(d)].z1 != Z ||
                    fp.rooms[static_cast<size_t>(u)].z0 != Z) {
                    valid = false;
                    break;
                }
                if (fp.rooms[static_cast<size_t>(d)].x0 < a) {
                    a = fp.rooms[static_cast<size_t>(d)].x0;
                    grown = true;
                }
                if (fp.rooms[static_cast<size_t>(d)].x1 > b) {
                    b = fp.rooms[static_cast<size_t>(d)].x1;
                    grown = true;
                }
                if (fp.rooms[static_cast<size_t>(u)].x0 < a) {
                    a = fp.rooms[static_cast<size_t>(u)].x0;
                    grown = true;
                }
                if (fp.rooms[static_cast<size_t>(u)].x1 > b) {
                    b = fp.rooms[static_cast<size_t>(u)].x1;
                    grown = true;
                }
            }
        }
        if (!valid) continue;
        ShiftUnit un;
        un.vertical = false;
        un.line = Z;
        un.c0 = a;
        un.c1 = b;
        for (int col = a; col < b; ++col) {
            covered[static_cast<size_t>(col)] = 1;
            const int d = fp.at(col, Z - 1), u = fp.at(col, Z);
            if (std::find(un.lo.begin(), un.lo.end(), d) == un.lo.end()) un.lo.push_back(d);
            if (std::find(un.hi.begin(), un.hi.end(), u) == un.hi.end()) un.hi.push_back(u);
        }
        std::sort(un.lo.begin(), un.lo.end());
        std::sort(un.hi.begin(), un.hi.end());
        out.push_back(std::move(un));
    }
}

std::vector<ShiftUnit> all_units(const FloorPlan& fp) {
    std::vector<ShiftUnit> out;
    for (int X = 1; X < fp.w; ++X) units_on_vertical_line(fp, X, out);
    for (int Z = 1; Z < fp.h; ++Z) units_on_horizontal_line(fp, Z, out);
    return out;
}

int min_cells(const PlanRoom& room, double s) {
    return std::max(1, static_cast<int>(std::ceil(room.min_dim / s - 1e-9)));
}

// The shift keeps every shrinking room at least min_dim wide (I10 stays
// reachable; a pre-existing violation never blocks growth back).
bool shift_legal(const FloorPlan& fp, const ShiftUnit& u, int d, double s) {
    const std::vector<int>& shrinking = d > 0 ? u.hi : u.lo;
    for (int r : shrinking) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        const int width = u.vertical ? room.x1 - room.x0 : room.z1 - room.z0;
        if (width - 1 < min_cells(room, s)) return false;
    }
    return true;
}

void apply_move(FloorPlan& fp, const Move& mv) {
    if (mv.is_swap) {
        PlanRoom& A = fp.rooms[static_cast<size_t>(mv.swap_a)];
        PlanRoom& B = fp.rooms[static_cast<size_t>(mv.swap_b)];
        std::swap(A.x0, B.x0);
        std::swap(A.x1, B.x1);
        std::swap(A.z0, B.z0);
        std::swap(A.z1, B.z1);
        return;
    }
    for (int r : mv.unit.lo) {
        PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (mv.unit.vertical)
            room.x1 += mv.d;
        else
            room.z1 += mv.d;
    }
    for (int r : mv.unit.hi) {
        PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (mv.unit.vertical)
            room.x0 += mv.d;
        else
            room.z0 += mv.d;
    }
}

// ---------------------------------------------------------------------------
// Move proposals
// ---------------------------------------------------------------------------

// True when the room is a hearth room currently touching its stack (§9.2).
// Swapping such a room trades its hearth hard term for whatever the swap
// fixes — a trap the lex-best then cannot leave (the way back is +1 hard
// uphill), so the random swap below excludes anchored rooms.
bool anchored_room(const FloorPlan& fp, int r) {
    const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
    if (room.hearth_code == 0) return false;
    static const int di[4] = {-1, 1, 0, 0};
    static const int dj[4] = {0, 0, -1, 1};
    for (int j = room.z0; j < room.z1; ++j)
        for (int i = room.x0; i < room.x1; ++i)
            for (int q = 0; q < 4; ++q) {
                const int ni = i + di[q], nj = j + dj[q];
                if (ni < 0 || nj < 0 || ni >= fp.w || nj >= fp.h) continue;
                if (fp.at(ni, nj) == kCellCore) return true;
            }
    return false;
}

bool random_move(const FloorPlan& fp, double s, Rng& rng, Move& out) {
    const int N = static_cast<int>(fp.rooms.size());
    if (rng.f64() < 0.8) {
        // Uniform over the legal (unit, direction) pairs.
        const std::vector<ShiftUnit> units = all_units(fp);
        std::vector<std::pair<int, int>> legal;  // (unit index, d)
        for (size_t k = 0; k < units.size(); ++k)
            for (int d : {-1, 1})
                if (shift_legal(fp, units[k], d, s))
                    legal.emplace_back(static_cast<int>(k), d);
        if (!legal.empty()) {
            const auto [k, d] = legal[static_cast<size_t>(rng.below(static_cast<int>(legal.size())))];
            out = Move{};
            out.unit = units[static_cast<size_t>(k)];
            out.d = d;
            return true;
        }
    }
    if (N >= 2) {
        // Swaps never touch fixed rooms (the stair shaft stays put, B3; a
        // winged L-room's shape is the field's, phase L) nor anchored ones
        // (a hearth room on its core, §9.2).
        std::vector<int> free_rooms;
        for (int r = 0; r < N; ++r)
            if (!fp.rooms[static_cast<size_t>(r)].fixed() && !anchored_room(fp, r))
                free_rooms.push_back(r);
        if (free_rooms.size() < 2) return false;
        const int a = free_rooms[static_cast<size_t>(rng.below(static_cast<int>(free_rooms.size())))];
        std::vector<int> rest;
        for (int r : free_rooms)
            if (r != a) rest.push_back(r);
        const int b = rest[static_cast<size_t>(rng.below(static_cast<int>(rest.size())))];
        out = Move{};
        out.is_swap = true;
        out.swap_a = a;
        out.swap_b = b;
        return true;
    }
    return false;
}

// A random legal shift touching the given room (critic helper).
bool shift_touching(const FloorPlan& fp, int room, double s, Rng& rng, Move& out) {
    const PlanRoom& R = fp.rooms[static_cast<size_t>(room)];
    std::vector<std::pair<ShiftUnit, int>> legal;
    std::vector<ShiftUnit> units;
    for (int X : {R.x0, R.x1})
        if (X >= 1 && X <= fp.w - 1) units_on_vertical_line(fp, X, units);
    for (int Z : {R.z0, R.z1})
        if (Z >= 1 && Z <= fp.h - 1) units_on_horizontal_line(fp, Z, units);
    for (const ShiftUnit& u : units) {
        const bool touches = std::find(u.lo.begin(), u.lo.end(), room) != u.lo.end() ||
                             std::find(u.hi.begin(), u.hi.end(), room) != u.hi.end();
        if (!touches) continue;
        for (int d : {-1, 1})
            if (shift_legal(fp, u, d, s)) legal.emplace_back(u, d);
    }
    if (legal.empty()) return false;
    const auto& [u, d] = legal[static_cast<size_t>(rng.below(static_cast<int>(legal.size())))];
    out = Move{};
    out.unit = u;
    out.d = d;
    return true;
}

bool room_has_exterior(const FloorPlan& fp, int r) {
    const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
    for (int j = room.z0; j < room.z1; ++j) {
        if (!fp.inside(room.x0 - 1, j) || fp.at(room.x0 - 1, j) == kCellOutside) return true;
        if (!fp.inside(room.x1, j) || fp.at(room.x1, j) == kCellOutside) return true;
    }
    for (int i = room.x0; i < room.x1; ++i) {
        if (!fp.inside(i, room.z0 - 1) || fp.at(i, room.z0 - 1) == kCellOutside) return true;
        if (!fp.inside(i, room.z1) || fp.at(i, room.z1) == kCellOutside) return true;
    }
    return false;
}

// The room has a door-capable exterior run on the front (+z) or back (-z)
// facade: a contiguous exposed span of at least door + margin cells (the
// same bar facade_runs() applies in derive_openings).
bool facade_capable(const FloorPlan& fp, const PlanParams& params, int r, bool front) {
    const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
    int run = 0, best = 0;
    for (int i = room.x0; i < room.x1; ++i) {
        const int j = front ? room.z1 : room.z0 - 1;
        const bool exposed = !fp.inside(i, j) || fp.at(i, j) == kCellOutside;
        run = exposed ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best >= params.door_cells + 2 * params.door_margin;
}

// ---------------------------------------------------------------------------
// Evaluation context
// ---------------------------------------------------------------------------

struct Ctx {
    const ExpandedProgram* prog = nullptr;
    const PlanParams* params = nullptr;
    const OpeningRules* rules = nullptr;  // access root (street / stair arrival)
    const FloorPlan* below = nullptr;     // lower storey plan for C11 (support)
    OpeningMemo memo;
    FloorPlan cur;
    DerivedOpenings ops;
    CostBreakdown cost;
};

struct Candidate {
    FloorPlan plan;
    OpeningMemo memo;
    DerivedOpenings ops;
    CostBreakdown cost;
};

Candidate evaluate(const Ctx& c, const FloorPlan& plan) {
    Candidate cand;
    cand.plan = plan;
    cand.plan.rasterize();
    cand.memo = c.memo;
    cand.ops = derive_openings(cand.plan, *c.prog, *c.params, cand.memo, *c.rules);
    cand.cost = evaluate_cost(cand.plan, *c.prog, cand.ops, *c.params, c.below,
                              c.rules->full_height);
    return cand;
}

}  // namespace

bool lex_better(const CostBreakdown& a, const CostBreakdown& b) {
    if (a.hard != b.hard) return a.hard < b.hard;
    return a.soft < b.soft - 1e-12;
}

PlanResult anneal_plan(FloorPlan grown, const ExpandedProgram& prog,
                       const PlanParams& params, const OpeningRules& rules,
                       const FloorPlan* below, Rng& rng) {
    PlanResult res;

    Ctx c;
    c.prog = &prog;
    c.params = &params;
    c.rules = &rules;
    c.below = below;
    c.cur = std::move(grown);
    c.cur.rasterize();
    c.ops = derive_openings(c.cur, prog, params, c.memo, rules);
    c.cost = evaluate_cost(c.cur, prog, c.ops, params, below, rules.full_height);

    AnnealReport& rep = res.anneal;
    rep.initial = c.cost;

    const int B = std::max(1, params.budget);
    const double s = params.ir.cell;

    // -- T0 calibration (§11): mean positive delta is accepted with p = 0.5.
    {
        Ctx probe = c;  // rules/below come along; cur/ops/cost shadow below
        std::vector<double> pos;
        for (int i = 0; i < params.calibrate_moves; ++i) {
            Move mv;
            if (!random_move(probe.cur, s, rng, mv)) break;
            FloorPlan next = probe.cur;
            apply_move(next, mv);
            Candidate cc = evaluate(probe, next);
            const double delta =
                cc.cost.total(params.w_hard0) - probe.cost.total(params.w_hard0);
            if (delta > 1e-12) pos.push_back(delta);
            probe.cur = std::move(cc.plan);
            probe.memo = std::move(cc.memo);
            probe.ops = std::move(cc.ops);
            probe.cost = cc.cost;
        }
        double mean = 0.0;
        for (double d : pos) mean += d;
        if (!pos.empty()) mean /= static_cast<double>(pos.size());
        rep.t0 = pos.empty() ? 1.0 : -mean / std::log(0.5);
        if (rep.t0 <= 1e-9) rep.t0 = 1.0;
    }

    // -- Main loop -------------------------------------------------------------
    FloorPlan best_plan = c.cur;
    DerivedOpenings best_ops = c.ops;
    CostBreakdown best_cost = c.cost;
    int stall = 0;
    bool reheated = false;
    int reheat_t = -1;
    double reheat_T = 0.0;
    const auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < B; ++t) {
        const int phase = t * 3 / B;
        const double w_hard = params.w_hard0 * (phase == 0 ? 1.0 : phase == 1 ? 10.0 : 100.0);
        double T;
        if (reheat_t < 0) {
            T = rep.t0 * std::pow(0.01, static_cast<double>(t) / B);
        } else {
            // After the single reheat: geometric from 0.3*T0 down to 0.01*T0.
            const double f = static_cast<double>(t - reheat_t) / std::max(1, B - reheat_t);
            T = reheat_T * std::pow(0.01 * rep.t0 / reheat_T, f);
        }

        // Move proposal: critics first (q_critic share), else the L0 mix.
        Move mv;
        bool have = false;
        if (rng.f64() < params.q_critic) {
            const int N = static_cast<int>(c.cur.rooms.size());
            // Core contact (§9.2), computed at most once per iteration: the
            // door-link critic spares core-touching hearth rooms, the hearth
            // critic reads its bad/donor sets from it.
            std::vector<char> touch_memo;
            auto core_touch = [&]() -> const std::vector<char>& {
                if (touch_memo.empty()) {
                    touch_memo.assign(static_cast<size_t>(N), 0);
                    static const int di[4] = {-1, 1, 0, 0};
                    static const int dj[4] = {0, 0, -1, 1};
                    for (int j = 0; j < c.cur.h; ++j)
                        for (int i = 0; i < c.cur.w; ++i) {
                            if (c.cur.at(i, j) != kCellCore) continue;
                            for (int q = 0; q < 4; ++q) {
                                const int32_t nn = c.cur.room_at(i + di[q], j + dj[q]);
                                if (nn >= 0) touch_memo[static_cast<size_t>(nn)] = 1;
                            }
                        }
                }
                return touch_memo;
            };
            auto anchored = [&](int r) {
                const PlanRoom& rm = c.cur.rooms[static_cast<size_t>(r)];
                return rm.hearth_code != 0 && core_touch()[static_cast<size_t>(r)];
            };
            // Program critic (hard.program): a street door without a facade
            // host, or a program door link without a door-capable run.
            if (!c.ops.entrance_ok || !c.ops.back_door_ok) {
                for (int which = 0; which < 2 && !have; ++which) {
                    const bool front = which == 0;
                    if (front ? c.ops.entrance_ok : c.ops.back_door_ok) continue;
                    std::vector<int> hosts, donors, donors_soft;
                    for (int r = 0; r < N; ++r) {
                        const PlanRoom& room = c.cur.rooms[static_cast<size_t>(r)];
                        if (room.fixed()) continue;
                        // An anchor (§9.2: a hearth room touching its core)
                        // never moves for a street door — the swap trades the
                        // facade term for a hearth term and the two critics
                        // oscillate (observed: hall swapped off its stack for
                        // the back door, then swapped back). Every host
                        // anchored -> the critic stays silent; the repair or
                        // a re-grow owns the failure.
                        if (anchored(r)) continue;
                        if (front ? room.entrance_front : room.entrance_back) {
                            hosts.push_back(r);
                        } else if (facade_capable(c.cur, params, r, front)) {
                            // A host of the opposite street door would just
                            // move the failure to the other facade.
                            (front ? room.entrance_back : room.entrance_front)
                                ? donors_soft.push_back(r)
                                : donors.push_back(r);
                        }
                    }
                    if (donors.empty()) donors = donors_soft;
                    if (!hosts.empty() && !donors.empty()) {
                        mv = Move{};
                        mv.is_swap = true;
                        mv.swap_a =
                            hosts[static_cast<size_t>(rng.below(static_cast<int>(hosts.size())))];
                        mv.swap_b =
                            donors[static_cast<size_t>(rng.below(static_cast<int>(donors.size())))];
                        have = true;
                    }
                }
            }
            if (!have && !c.ops.missing_door_links.empty()) {
                const auto& link = c.ops.missing_door_links[static_cast<size_t>(
                    rng.below(static_cast<int>(c.ops.missing_door_links.size())))];
                const int a = link.first, b = link.second;
                const bool fix_a = c.cur.rooms[static_cast<size_t>(a)].fixed();
                const bool fix_b = c.cur.rooms[static_cast<size_t>(b)].fixed();
                if (fix_a || fix_b) {
                    // A fixed endpoint (the stair shaft, a winged L-room)
                    // cannot swap; pull the other room toward it.
                    if (!fix_a) have = shift_touching(c.cur, a, s, rng, mv);
                    if (!have && !fix_b) have = shift_touching(c.cur, b, s, rng, mv);
                } else {
                // Swap one endpoint with a room adjacent to the other; the
                // shared wall makes the link door-capable. An anchor — a
                // hearth room touching its core (§9.2) — is never the one to
                // move: that would trade the link for a hearth hard term.
                // When b is anchored, a travels toward b instead. Both
                // anchored -> no swap at all, only a boundary nudge.
                const bool anc_a = anchored(a), anc_b = anchored(b);
                if (anc_a && anc_b) {
                    have = shift_touching(c.cur, a, s, rng, mv);
                    if (!have) have = shift_touching(c.cur, b, s, rng, mv);
                } else {
                const int mover = anc_b ? a : b;
                const int stay = mover == a ? b : a;
                std::vector<int> partners;
                for (const auto& [x, y] : c.ops.adjacency) {
                    const int o = x == stay ? y : y == stay ? x : -1;
                    if (o >= 0 && o != mover &&
                        !c.cur.rooms[static_cast<size_t>(o)].fixed() && !anchored(o) &&
                        std::find(partners.begin(), partners.end(), o) == partners.end())
                        partners.push_back(o);
                }
                if (!partners.empty()) {
                    mv = Move{};
                    mv.is_swap = true;
                    mv.swap_a = mover;
                    mv.swap_b = partners[static_cast<size_t>(
                        rng.below(static_cast<int>(partners.size())))];
                    have = true;
                } else {
                    have = shift_touching(c.cur, a, s, rng, mv);
                }
                }
                }
            }
            if (!have) {
                // Access critic (hard.access): pull an unreachable room into
                // the connected set. Anchors are excluded on both sides — the
                // swap would otherwise fix access by breaking the hearth
                // contact (observed: the unreachable pantry swapped with the
                // anchored hall, hall landed in the back stripe off its
                // stack).
                std::vector<int> unreachable;
                for (size_t r = 0; r < c.ops.reachable.size(); ++r)
                    if (!c.ops.reachable[r] && !c.cur.rooms[r].fixed() &&
                        !anchored(static_cast<int>(r)))
                        unreachable.push_back(static_cast<int>(r));
                if (!unreachable.empty()) {
                    const int u = unreachable[static_cast<size_t>(
                        rng.below(static_cast<int>(unreachable.size())))];
                    if (rng.f64() < 0.5) {
                        std::vector<int> partners;
                        for (const auto& [a, b] : c.ops.adjacency) {
                            const int other = a == u ? b : b == u ? a : -1;
                            if (other >= 0 && c.ops.reachable[static_cast<size_t>(other)] &&
                                !c.cur.rooms[static_cast<size_t>(other)].fixed() &&
                                !anchored(other))
                                partners.push_back(other);
                        }
                        for (size_t r = 0; partners.empty() && r < c.ops.reachable.size(); ++r)
                            if (c.ops.reachable[r] && static_cast<int>(r) != u &&
                                !c.cur.rooms[r].fixed() && !anchored(static_cast<int>(r)))
                                partners.push_back(static_cast<int>(r));
                        if (!partners.empty()) {
                            mv = Move{};
                            mv.is_swap = true;
                            mv.swap_a = u;
                            mv.swap_b = partners[static_cast<size_t>(
                                rng.below(static_cast<int>(partners.size())))];
                            have = true;
                        }
                    } else {
                        have = shift_touching(c.cur, u, s, rng, mv);
                    }
                }
            }
            if (!have && c.cost.hard_terms.count("hearth") != 0 &&
                c.cost.hard_terms.at("hearth") > 0) {
                // Hearth critic (hard.hearth, §9.2): a hearth room ended away
                // from its chimney stack — swap it with a (non-fixed)
                // non-hearth room touching a core; failing that, pull one of
                // its boundary lines.
                const int Nr = static_cast<int>(c.cur.rooms.size());
                const std::vector<char>& touch = core_touch();
                std::vector<int> bad, donors;
                for (int r = 0; r < Nr; ++r) {
                    const PlanRoom& room = c.cur.rooms[static_cast<size_t>(r)];
                    if (room.fixed()) continue;
                    if (room.hearth_code != 0 && !touch[static_cast<size_t>(r)])
                        bad.push_back(r);
                    else if (room.hearth_code == 0 && touch[static_cast<size_t>(r)])
                        donors.push_back(r);
                }
                if (!bad.empty() && !donors.empty()) {
                    mv = Move{};
                    mv.is_swap = true;
                    mv.swap_a =
                        bad[static_cast<size_t>(rng.below(static_cast<int>(bad.size())))];
                    mv.swap_b =
                        donors[static_cast<size_t>(rng.below(static_cast<int>(donors.size())))];
                    have = true;
                } else if (!bad.empty()) {
                    have = shift_touching(
                        c.cur, bad[static_cast<size_t>(rng.below(static_cast<int>(bad.size())))],
                        s, rng, mv);
                }
            }
            if (!have) {
                // Dark-room critic: swap an unlit habitable room with a room
                // that has exterior exposure (prefer non-habitable donors).
                // Anchors are excluded on both sides (same reason as the
                // access critic: light is not worth a hearth hard term).
                std::vector<int> unlit;
                for (size_t r = 0; r < c.cur.rooms.size(); ++r) {
                    if (!c.cur.rooms[r].habitable || c.cur.rooms[r].fixed() ||
                        anchored(static_cast<int>(r)))
                        continue;
                    bool lit = false;
                    for (const DerivedOpening& o : c.ops.openings)
                        if (o.kind_code == kOpeningWindow && o.room_a == static_cast<int>(r)) {
                            lit = true;
                            break;
                        }
                    if (!lit) unlit.push_back(static_cast<int>(r));
                }
                if (!unlit.empty()) {
                    const int u =
                        unlit[static_cast<size_t>(rng.below(static_cast<int>(unlit.size())))];
                    std::vector<int> donors, donors_soft;
                    for (size_t r = 0; r < c.cur.rooms.size(); ++r) {
                        if (static_cast<int>(r) == u || c.cur.rooms[r].fixed() ||
                            anchored(static_cast<int>(r)) ||
                            !room_has_exterior(c.cur, static_cast<int>(r)))
                            continue;
                        (c.cur.rooms[r].habitable ? donors_soft : donors).push_back(static_cast<int>(r));
                    }
                    if (donors.empty()) donors = donors_soft;
                    if (!donors.empty()) {
                        mv = Move{};
                        mv.is_swap = true;
                        mv.swap_a = u;
                        mv.swap_b =
                            donors[static_cast<size_t>(rng.below(static_cast<int>(donors.size())))];
                        have = true;
                    }
                }
            }
            if (!have) {
                // Off-axis critic: nudge a partition of a room whose window
                // missed the bay axes.
                std::vector<int> offaxis;
                for (const DerivedOpening& o : c.ops.openings)
                    if (o.kind_code == kOpeningWindow && !o.on_axis && o.room_a >= 0 &&
                        std::find(offaxis.begin(), offaxis.end(), o.room_a) == offaxis.end())
                        offaxis.push_back(o.room_a);
                if (!offaxis.empty())
                    have = shift_touching(
                        c.cur,
                        offaxis[static_cast<size_t>(rng.below(static_cast<int>(offaxis.size())))],
                        s, rng, mv);
            }
        }
        if (!have) have = random_move(c.cur, s, rng, mv);
        if (!have) continue;

        ++rep.moves_tried;
        FloorPlan next = c.cur;
        apply_move(next, mv);
        Candidate cc = evaluate(c, next);
        const double delta = cc.cost.total(w_hard) - c.cost.total(w_hard);
        const bool accept = delta <= 0.0 || rng.f64() < std::exp(-delta / T);
        if (!accept) {
            ++stall;
        } else {
            ++rep.moves_accepted;
            c.cur = std::move(cc.plan);
            c.memo = std::move(cc.memo);
            c.ops = std::move(cc.ops);
            c.cost = cc.cost;
            if (lex_better(c.cost, best_cost)) {
                best_plan = c.cur;
                best_ops = c.ops;
                best_cost = c.cost;
                stall = 0;
            } else {
                ++stall;
            }
        }
        if (!reheated && stall >= B / 10 && t < B * 3 / 4) {
            reheated = true;
            reheat_t = t;
            reheat_T = 0.3 * rep.t0;
            stall = 0;
            rep.log.push_back("reheat at move " + std::to_string(t));
        }
    }

    rep.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    rep.final = best_cost;
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "anneal: %d/%d moves accepted, t0=%.4g, hard %d -> %d, soft %.4g -> %.4g",
                      rep.moves_accepted, rep.moves_tried, rep.t0, rep.initial.hard,
                      best_cost.hard, rep.initial.soft, best_cost.soft);
        rep.log.push_back(buf);
    }

    res.ok = true;
    res.plan = std::move(best_plan);
    res.plan.rasterize();
    res.openings = std::move(best_ops);
    res.cost = best_cost;
    return res;
}

PlanResult plan_floor(const ExpandedProgram& prog, const MassingCandidate& cand,
                      const PlanParams& params, uint64_t seed) {
    Rng rng = Rng::from_seed(seed);
    // Restarts (§11): the anneal moves never change the column/row topology
    // the growth produced, and some tilings cannot host the program (a door
    // link across a stacked column, ...). Re-grow from a split sub-stream and
    // re-anneal while a hard term survives; keep the lexicographic best.
    PlanResult best;
    const int attempts = std::max(1, params.max_attempts);
    for (int k = 0; k < attempts; ++k) {
        Rng arng = k == 0 ? rng : rng.split(0xB2B20000ull + static_cast<uint64_t>(k));
        std::string err;
        FloorPlan grown = grow_floor(prog, cand, params, arng, err);
        if (!err.empty()) {
            if (best.error.empty()) best.error = err;
            continue;
        }
        PlanResult r = anneal_plan(std::move(grown), prog, params, OpeningRules{}, nullptr, arng);
        if (!r.ok) {
            if (best.error.empty()) best.error = r.error;
            continue;
        }
        const bool better = !best.ok || lex_better(r.cost, best.cost);
        if (better) {
            if (k > 0)
                r.anneal.log.insert(r.anneal.log.begin(),
                                    "attempt " + std::to_string(k + 1) + "/" +
                                        std::to_string(attempts) + " (re-grew the tiling)");
            best = std::move(r);
        }
        if (best.ok && best.cost.hard == 0) break;
    }
    return best;
}

}  // namespace building_topology_generator
