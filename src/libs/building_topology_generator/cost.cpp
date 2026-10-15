// Cost function (algorithms §8.2, normalization §8.4). See cost.h.

#include "pch.h"

#include "cost.h"

#include <algorithm>
#include <cmath>

#include "codes.h"

namespace building_topology_generator {

CostBreakdown evaluate_cost(const FloorPlan& fp, const ExpandedProgram& prog,
                            const DerivedOpenings& ops, const PlanParams& params) {
    CostBreakdown cb;
    const int N = static_cast<int>(fp.rooms.size());
    const IrParams& ir = params.ir;

    // -- Hard terms -----------------------------------------------------------
    int n_unreachable = 0;
    for (int r = 0; r < N; ++r)
        if (!ops.reachable[static_cast<size_t>(r)]) ++n_unreachable;
    cb.hard_terms["access"] = n_unreachable;

    int n_program = static_cast<int>(ops.missing_door_links.size());
    if (!ops.entrance_ok) ++n_program;
    if (!ops.back_door_ok) ++n_program;
    cb.hard_terms["program"] = n_program;

    int n_min_dim = 0;
    for (int r = 0; r < N; ++r) {
        double w_m, d_m;
        fp.room_dims_net(r, ir, w_m, d_m);
        if (std::min(w_m, d_m) < fp.rooms[static_cast<size_t>(r)].min_dim - 1e-6)
            ++n_min_dim;
    }
    cb.hard_terms["min_dim"] = n_min_dim;
    cb.hard = n_unreachable + n_program + n_min_dim;

    // -- Soft terms (§8.4 normalization; weights from PlanParams) -------------
    auto add = [&](const char* name, double raw, double w) {
        cb.raw[name] = raw;
        cb.terms[name] = raw * w;
        cb.soft += raw * w;
    };

    // Area: per room ((deficit/target)^2 + 0.3 * surplus beyond max), mean.
    double area_sum = 0.0;
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        const double net = fp.room_area_net(r, ir);
        const double target = std::max(1.0, room.target);
        const double deficit = std::max(0.0, room.target - net) / target;
        const double surplus = std::max(0.0, net - room.area_max) / std::max(1.0, room.area_max);
        area_sum += deficit * deficit + 0.3 * surplus;
    }
    add("area", N > 0 ? area_sum / N : 0.0, params.w_area);

    // Adjacency: the share of unmet near links (a link is met when the rooms
    // share a border).
    int near_total = 0, near_met = 0;
    for (const ProgramLink& l : prog.links) {
        if (l.kind != kLinkNear) continue;
        const int a = fp.find_room(l.a), b = fp.find_room(l.b);
        if (a < 0 || b < 0) continue;
        ++near_total;
        const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
        if (std::find(ops.adjacency.begin(), ops.adjacency.end(), key) != ops.adjacency.end())
            ++near_met;
    }
    add("adjacency", near_total > 0 ? double(near_total - near_met) / near_total : 0.0,
        params.w_adjacency);

    // Privacy: private rooms want depth >= d_min from the street (§8.2).
    double priv_sum = 0.0;
    int n_private = 0;
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (room.zone_code != kZonePrivate) continue;
        ++n_private;
        const double depth = ops.depth[static_cast<size_t>(r)];
        if (depth > 1e20) continue;  // unreachable: the access hard term owns it
        priv_sum += std::max(0.0, params.d_min - depth) / params.d_min;
    }
    add("privacy", n_private > 0 ? priv_sum / n_private : 0.0, params.w_privacy);

    // Light (I6): habitable rooms without a window + the glass deficit, both
    // in shares.
    std::vector<double> glass(static_cast<size_t>(N), 0.0);
    std::vector<int> windows(static_cast<size_t>(N), 0);
    for (const DerivedOpening& o : ops.openings) {
        if (o.kind_code != kOpeningWindow || o.room_a < 0) continue;
        glass[static_cast<size_t>(o.room_a)] += (o.m1 - o.m0) * (o.y1 - o.y0);
        ++windows[static_cast<size_t>(o.room_a)];
    }
    int n_hab = 0, n_unlit = 0;
    double glass_need = 0.0, glass_deficit = 0.0;
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (!room.habitable) continue;
        ++n_hab;
        if (windows[static_cast<size_t>(r)] == 0) ++n_unlit;
        const double need = fp.room_area_net(r, ir) * ir.glass_ratio;
        glass_need += need;
        glass_deficit += std::max(0.0, need - glass[static_cast<size_t>(r)]);
    }
    const double light = (n_hab > 0 ? double(n_unlit) / n_hab : 0.0) +
                         (glass_need > 0.0 ? glass_deficit / glass_need : 0.0);
    add("light", light, params.w_light);

    // Regularity (C6): the share of windows off the bay axes; strict style
    // adds facade asymmetry (unpaired windows around the facade center).
    int n_win = 0, n_off = 0;
    for (const DerivedOpening& o : ops.openings) {
        if (o.kind_code != kOpeningWindow) continue;
        ++n_win;
        if (!o.on_axis) ++n_off;
    }
    double regularity = n_win > 0 ? double(n_off) / n_win : 0.0;
    if (prog.style.regularity == "strict") {
        const double cx = fp.origin.x + fp.w * ir.cell / 2.0;
        std::vector<double> front;  // centers of the front-facing windows
        for (const DerivedOpening& o : ops.openings) {
            if (o.kind_code != kOpeningWindow || o.vertical || o.room_a < 0) continue;
            const PlanRoom& room = fp.rooms[static_cast<size_t>(o.room_a)];
            if (o.line == room.z1) front.push_back((o.m0 + o.m1) / 2.0);
        }
        int unpaired = 0;
        for (size_t i = 0; i < front.size(); ++i) {
            const double mirror = 2.0 * cx - front[i];
            bool paired = false;
            for (size_t j = 0; j < front.size(); ++j)
                if (i != j && std::fabs(front[j] - mirror) < 0.3) {
                    paired = true;
                    break;
                }
            if (!paired && std::fabs(front[i] - cx) > 0.3) ++unpaired;
        }
        if (!front.empty()) regularity += double(unpaired) / front.size();
    }
    add("regularity", regularity, params.w_regularity);

    // Span (§7.5): rooms whose smaller net side exceeds the unsupported span.
    double span_sum = 0.0;
    for (int r = 0; r < N; ++r) {
        double w_m, d_m;
        fp.room_dims_net(r, ir, w_m, d_m);
        span_sum += std::max(0.0, std::min(w_m, d_m) - params.span_max) / params.span_max;
    }
    add("span", N > 0 ? span_sum / N : 0.0, params.w_span);

    return cb;
}

}  // namespace building_topology_generator
