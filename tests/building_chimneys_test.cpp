// Fast unit tests for the B4 stage G4b (requirements §5.7, §9.2): the chimney
// stack placement (grouping, archetypes, the exit rule), the kCellCore
// stamping, the hearth refs/anchors in the emitted IR and the §9.2 numeric
// example. The fixtures come from massing_from_pin (as the stair tests).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "chimneys.h"
#include "emit.h"
#include "massing.h"
#include "pipeline.h"
#include "plan.h"
#include "program.h"
#include "roof.h"
#include "storeys.h"

namespace building_topology_generator {
namespace {

constexpr double kCell = 0.5;
constexpr double kTExt = 0.45;

// As the stair tests: the outline of a w x d cell rect volume.
std::vector<glm::dvec2> cell_rect_outline(int w, int d) {
    const double t = kTExt / 2;
    return {{-t, -t}, {-t, d * kCell + t}, {w * kCell + t, d * kCell + t}, {w * kCell + t, -t}};
}

struct Fixture {
    ExpandedProgram prog;
    Ir ir;
    StoreyFields fields;
};

RoomInstance hearth_room(const std::string& id) {
    RoomInstance r;
    r.id = r.base_id = id;
    r.fn_code = kFnLiving;
    r.zone_code = kZonePublic;
    r.area_min = 8.0;
    r.area_max = 16.0;
    r.target = 12.0;
    r.storeys_mask = 1u << kStoreyGround;
    r.hearth_code = kHearthFireplace;
    return r;
}

// The §9.2 numeric example: a 16x14-cell rect (outline 8.45 x 7.45 m), one
// ground storey (2.8 + slab 0.25 -> the attic floor at 3.05) plus an attic
// with eave_rise 0.8 (the eave at 3.85), a 50 deg gable roof with the ridge
// along X at z = 0 -> the ridge top at 3.85 + 3.725*tan(50) ~= 8.289.
Fixture make_s92(const char* chimney = "central", int n_rooms = 2) {
    Fixture f;
    f.prog.type = "test";
    f.prog.style.chimney = chimney;
    f.prog.rooms.push_back(hearth_room("hall"));
    if (n_rooms > 1) f.prog.rooms.push_back(hearth_room("parlour"));
    MassingVolume v;
    v.outline = cell_rect_outline(16, 14);
    v.storeys = 1;
    v.attic = true;
    v.roof_kind = kRoofGable;
    v.pitch_by_edge = {90.0, 50.0, 90.0, 50.0};  // the gables on the x edges
    v.eave_rise = 0.8;
    MassingParams mp;
    const MassingResult m =
        massing_from_pin(f.prog, Site{20.0, 18.0, 0.0, 3}, mp, 1, {v}, "test");
    EXPECT_TRUE(m.ok) << m.error;
    std::string err;
    EXPECT_TRUE(massing_to_ir(m.chosen, f.prog, mp, 1, f.ir, err)) << err;
    EXPECT_TRUE(storey_fields(f.ir, f.fields, err)) << err;
    return f;
}

double ridge_top(const Ir& ir) {
    double ry = 0.0;
    for (const IrRoofEdge& e : ir.roof.ridges) ry = std::max({ry, e.a.y, e.b.y});
    return ry;
}

// ---------------------------------------------------------------------------
// §5.7: the exit rule as a pure function (the §9.2 numbers)
// ---------------------------------------------------------------------------

TEST(BuildingChimneys, TopRequiredMatchesTheSpecExample) {
    // §9.2: ridge top 8.29, eave 3.85, pitch 50 deg.
    const double ridge_y = 3.85 + 3.725 * std::tan(50.0 * M_PI / 180.0);
    EXPECT_NEAR(ridge_y, 8.29, 0.01);
    // At the ridge (d <= 1.5): 0.5 m above the ridge.
    EXPECT_NEAR(chimney_top_required(0.0, ridge_y, ridge_y, 0.5), ridge_y + 0.5, 1e-9);
    // The z = -2.0 case (d = 2.0, the 1.5-3 m band): not below the ridge;
    // the slope there is at 3.85 + 1.725*tan(50) ~= 5.91 -> the ridge rule
    // dominates the h_above_roof floor.
    const double exit_y = 3.85 + 1.725 * std::tan(50.0 * M_PI / 180.0);
    EXPECT_NEAR(exit_y, 5.91, 0.01);
    EXPECT_NEAR(chimney_top_required(2.0, ridge_y, exit_y, 0.5), ridge_y, 1e-9);
    // Far from the ridge (d > 3): the 10 deg line from the ridge, but never
    // below h_above_roof over the exit slope.
    const double far_exit = 3.85 + 0.725 * std::tan(50.0 * M_PI / 180.0);
    const double expect =
        std::max(ridge_y - 6.0 * std::tan(10.0 * M_PI / 180.0), far_exit + 0.5);
    EXPECT_NEAR(chimney_top_required(6.0, ridge_y, far_exit, 0.5), expect, 1e-9);
}

// ---------------------------------------------------------------------------
// G4b placement on the §9.2 massing
// ---------------------------------------------------------------------------

TEST(BuildingChimneys, CentralPairSharesOneStackAtTheRidge) {
    Fixture f = make_s92("central", 2);
    const std::vector<int> storey_of(f.prog.rooms.size(), 0);
    Rng rng = Rng::from_seed(7);
    PlanParams params;
    std::vector<StairCore> stairs;
    std::vector<ChimneyCore> cores;
    std::string err;
    ASSERT_TRUE(place_chimneys(f.ir, f.fields, stairs, f.prog, storey_of, params, rng, {}, cores,
                               err))
        << err;
    ASSERT_EQ(cores.size(), 1u);  // a ground pair shares one 1x2 stack
    const ChimneyCore& c = cores[0];
    EXPECT_EQ(c.id, "chimney1");
    EXPECT_EQ(c.archetype, 1);  // central as requested
    ASSERT_EQ(c.cells.size(), 2u);
    EXPECT_EQ(c.hearths.size(), 2u);
    EXPECT_EQ(c.storey0, 0);
    EXPECT_EQ(c.storey1, 1);  // ground + attic of the volume
    // §5.7: in the 1.5 m ridge band -> the top is the ridge + 0.5 m.
    const double ry = ridge_top(f.ir);
    ASSERT_GT(ry, 0.0);
    RoofModel model;
    ASSERT_TRUE(build_roof_model(f.ir.volumes[0], model, err)) << err;
    double ridge_d = 0.0, ridge_y = 0.0;
    ASSERT_TRUE(nearest_ridge(model, c.top.x, c.top.z, ridge_d, ridge_y));
    EXPECT_LE(ridge_d, 1.5 + 1e-9);
    EXPECT_NEAR(c.top.y, ry + 0.5, 1e-6);
    EXPECT_NEAR(c.top.y, 8.79, 0.01);  // the §9.2 number
    // The exit plane index addresses Ir::roof.planes.
    EXPECT_GE(c.roof_plane, 0);
    EXPECT_LT(c.roof_plane, static_cast<int>(f.ir.roof.planes.size()));
    // The cells are stamped kCellCore on every crossed storey.
    for (int k = c.storey0; k <= c.storey1; ++k)
        for (const glm::ivec2& cell : c.cells)
            EXPECT_EQ(f.fields.mask[static_cast<size_t>(k)]
                                   [static_cast<size_t>(cell.y) * f.fields.w + cell.x],
                      kCellCore)
                << "storey " << k;
    // The hearth rooms can seed next to the stack.
    EXPECT_GE(hearth_seed_cells(c, f.fields, 0, stairs).size(), 2u);
}

TEST(BuildingChimneys, SoloHearthGetsA1x1Stack) {
    Fixture f = make_s92("central", 1);
    const std::vector<int> storey_of(f.prog.rooms.size(), 0);
    Rng rng = Rng::from_seed(3);
    PlanParams params;
    std::vector<StairCore> stairs;
    std::vector<ChimneyCore> cores;
    std::string err;
    ASSERT_TRUE(place_chimneys(f.ir, f.fields, stairs, f.prog, storey_of, params, rng, {}, cores,
                               err))
        << err;
    ASSERT_EQ(cores.size(), 1u);
    EXPECT_EQ(cores[0].cells.size(), 1u);
    EXPECT_EQ(cores[0].hearths.size(), 1u);
}

TEST(BuildingChimneys, GableArchetypeSitsOnTheGableWall) {
    Fixture f = make_s92("gable", 2);
    const std::vector<int> storey_of(f.prog.rooms.size(), 0);
    Rng rng = Rng::from_seed(5);
    PlanParams params;
    std::vector<StairCore> stairs;
    std::vector<ChimneyCore> cores;
    std::string err;
    ASSERT_TRUE(place_chimneys(f.ir, f.fields, stairs, f.prog, storey_of, params, rng, {}, cores,
                               err))
        << err;
    ASSERT_EQ(cores.size(), 1u);
    const ChimneyCore& c = cores[0];
    EXPECT_EQ(c.archetype, 2);  // gable as requested
    // A boundary cell against a ~90 deg outline edge (the x extremes here).
    bool at_gable = false;
    for (const glm::ivec2& cell : c.cells)
        if (cell.x == 0 || cell.x == f.fields.w - 1) at_gable = true;
    EXPECT_TRUE(at_gable);
    // §5.7 still holds: the top clears the required height at the exit.
    RoofModel model;
    ASSERT_TRUE(build_roof_model(f.ir.volumes[0], model, err)) << err;
    double ridge_d = 0.0, ridge_y = 0.0, exit_y = 0.0;
    ASSERT_TRUE(nearest_ridge(model, c.top.x, c.top.z, ridge_d, ridge_y));
    ASSERT_TRUE(roof_y_at(model, c.top.x, c.top.z, exit_y));
    EXPECT_GE(c.top.y, chimney_top_required(ridge_d, ridge_y, exit_y,
                                            params.ir.h_above_roof) -
                           1e-9);
}

TEST(BuildingChimneys, NoHearthsNoStacks) {
    Fixture f = make_s92("central", 2);
    for (RoomInstance& r : f.prog.rooms) r.hearth_code = 0;
    const std::vector<int> storey_of(f.prog.rooms.size(), 0);
    Rng rng = Rng::from_seed(1);
    PlanParams params;
    std::vector<StairCore> stairs;
    std::vector<ChimneyCore> cores;
    std::string err;
    EXPECT_TRUE(place_chimneys(f.ir, f.fields, stairs, f.prog, storey_of, params, rng, {}, cores,
                               err))
        << err;
    EXPECT_TRUE(cores.empty());
}

// ---------------------------------------------------------------------------
// The driver end to end: house.json, two full storeys + attic
// ---------------------------------------------------------------------------

Fixture make_house() {
    Fixture f;
    Program p;
    std::string err;
    EXPECT_TRUE(load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/house.json", p, err))
        << err;
    f.prog = expand_program(p);
    MassingVolume v;
    v.outline = cell_rect_outline(16, 12);
    v.storeys = 2;
    v.attic = true;
    v.roof_kind = kRoofGable;
    v.pitch_by_edge = {90.0, 50.0, 90.0, 50.0};
    v.eave_rise = 0.8;
    MassingParams mp;
    const MassingResult m =
        massing_from_pin(f.prog, Site{16.0, 14.0, 0.0, 3}, mp, 1, {v}, "test");
    EXPECT_TRUE(m.ok) << m.error;
    EXPECT_TRUE(massing_to_ir(m.chosen, f.prog, mp, 1, f.ir, err)) << err;
    EXPECT_TRUE(storey_fields(f.ir, f.fields, err)) << err;
    return f;
}

TEST(BuildingChimneys, PlanBuildingEmitsTheChimneyIR) {
    Fixture f = make_house();
    PlanParams params;
    params.budget = 4000;
    BuildingPlanResult bp = plan_building(f.prog, f.ir, params, 42);
    ASSERT_TRUE(bp.ok) << bp.error;
    // house.json: hall + parlour, both fireplace, both ground -> one stack.
    ASSERT_EQ(bp.chimneys.size(), 1u);
    const ChimneyCore& cc = bp.chimneys[0];
    EXPECT_EQ(cc.hearths.size(), 2u);
    // The hard terms stay zero (the hearth rooms hug the stack).
    for (const PlanResult& pr : bp.storeys) {
        ASSERT_TRUE(pr.ok);
        EXPECT_EQ(pr.cost.hard, 0);
    }
    // Every hearth room touches the stack on its storey (the grown raster).
    for (const int qi : cc.hearths) {
        const int k = bp.storey_of[static_cast<size_t>(qi)];
        ASSERT_GE(k, 0);
        const FloorPlan& fp = bp.storeys[static_cast<size_t>(k)].plan;
        const int ri = fp.find_room(f.prog.rooms[static_cast<size_t>(qi)].id);
        ASSERT_GE(ri, 0) << f.prog.rooms[static_cast<size_t>(qi)].id;
        const PlanRoom& room = fp.rooms[static_cast<size_t>(ri)];
        (void)room;
        bool touches = false;
        for (const glm::ivec2& cell : cc.cells) {
            const int32_t di[4] = {-1, 1, 0, 0}, dj[4] = {0, 0, -1, 1};
            for (int q = 0; q < 4; ++q) {
                const int ni = cell.x + di[q], nj = cell.y + dj[q];
                if (fp.inside(ni, nj) && fp.at(ni, nj) == static_cast<int32_t>(ri))
                    touches = true;
            }
        }
        EXPECT_TRUE(touches) << f.prog.rooms[static_cast<size_t>(qi)].id;
    }

    // -- The emitted IR -------------------------------------------------------
    Ir out = f.ir;
    std::string err;
    ASSERT_TRUE(emit_building_ir(bp, f.prog, params, out, err)) << err;
    ASSERT_EQ(out.chimneys.size(), 1u);
    const IrChimney& ic = out.chimneys[0];
    EXPECT_EQ(ic.id, "chimney1");
    // 1x2 section: one axis spans two cells.
    const double sx = ic.section.x, sz = ic.section.y;
    EXPECT_TRUE((std::abs(sx - 1.0) < 1e-9 && std::abs(sz - 0.5) < 1e-9) ||
                (std::abs(sx - 0.5) < 1e-9 && std::abs(sz - 1.0) < 1e-9))
        << sx << " x " << sz;
    // cells_by_storey covers the whole crossed range.
    ASSERT_EQ(ic.cells_by_storey.size(),
              static_cast<size_t>(cc.storey1 - cc.storey0 + 1));
    for (int k = cc.storey0; k <= cc.storey1; ++k)
        EXPECT_TRUE(ic.cells_by_storey.count(out.storeys[static_cast<size_t>(k)].id))
            << out.storeys[static_cast<size_t>(k)].id;
    EXPECT_GE(ic.roof_plane, 0);
    EXPECT_LT(ic.roof_plane, static_cast<int>(out.roof.planes.size()));
    // §5.7 rechecked from the IR roof model.
    {
        RoofModel model;
        ASSERT_TRUE(build_roof_model(out.volumes[0], model, err)) << err;
        double ridge_d = 0.0, ridge_y = 0.0, exit_y = 0.0;
        ASSERT_TRUE(nearest_ridge(model, ic.top.x, ic.top.z, ridge_d, ridge_y));
        ASSERT_TRUE(roof_y_at(model, ic.top.x, ic.top.z, exit_y));
        EXPECT_GE(ic.top.y, chimney_top_required(ridge_d, ridge_y, exit_y,
                                                 params.ir.h_above_roof) -
                               1e-9);
    }
    // The hearth refs resolve: the wall exists, the space is a side of it,
    // s stays on the wall; the anchor sits 0.9 m off the wall in the room.
    ASSERT_EQ(ic.hearths.size(), 2u);
    for (const IrHearthRef& hr : ic.hearths) {
        EXPECT_EQ(hr.kind_code, kHearthFireplace);
        const IrWall* wall = nullptr;
        for (const IrWall& w : out.walls)
            if (w.id == hr.wall) wall = &w;
        ASSERT_TRUE(wall) << hr.wall;
        EXPECT_TRUE(wall->side_l == hr.space || wall->side_r == hr.space);
        const double len = glm::length(wall->b - wall->a);
        EXPECT_GE(hr.s, -1e-9);
        EXPECT_LE(hr.s, len + 1e-9);
        bool anchor = false;
        for (const IrAnchor& a : out.anchors)
            if (a.kind_code == kAnchorHearth && a.space == hr.space) anchor = true;
        EXPECT_TRUE(anchor) << hr.space;
    }
    // No wall between a room and the stack (§6: the core asset builds it);
    // the only emitted core boundary is core|outside (the gable wall face).
    for (const IrWall& w : out.walls) {
        const bool cl = w.side_l == "core", cr = w.side_r == "core";
        if (cl) EXPECT_EQ(w.side_r, "outside");
        if (cr) EXPECT_EQ(w.side_l, "outside");
    }
    // The JSON round trip keeps the chimneys.
    const std::string text = write_ir_json(out);
    Ir back;
    ASSERT_TRUE(read_ir_json(text, back, err)) << err;
    EXPECT_EQ(write_ir_json(back), text);
}

TEST(BuildingChimneys, PlanBuildingChimneysAreDeterministic) {
    Fixture f = make_house();
    PlanParams params;
    params.budget = 2000;
    BuildingPlanResult a = plan_building(f.prog, f.ir, params, 9);
    BuildingPlanResult b = plan_building(f.prog, f.ir, params, 9);
    ASSERT_TRUE(a.ok && b.ok);
    ASSERT_EQ(a.chimneys.size(), b.chimneys.size());
    for (size_t c = 0; c < a.chimneys.size(); ++c) {
        EXPECT_EQ(a.chimneys[c].id, b.chimneys[c].id);
        EXPECT_EQ(a.chimneys[c].cells, b.chimneys[c].cells);
        EXPECT_EQ(a.chimneys[c].hearths, b.chimneys[c].hearths);
        EXPECT_EQ(a.chimneys[c].storey0, b.chimneys[c].storey0);
        EXPECT_EQ(a.chimneys[c].storey1, b.chimneys[c].storey1);
        EXPECT_DOUBLE_EQ(a.chimneys[c].top.y, b.chimneys[c].top.y);
    }
}

TEST(BuildingChimneys, DebugAcceptanceSeed) {
    const char* env = std::getenv("BTG_SEED_LIST");  // "1,2,3"
    if (!env) GTEST_SKIP();
    Program p;
    std::string err;
    ASSERT_TRUE(load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/house.json", p, err))
        << err;
    p.storeys_min = 2;
    p.attic = AtticMode::Required;
    const ExpandedProgram prog = expand_program(p);
    std::stringstream ss(env);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        const uint64_t seed = std::stoull(tok);
        Site site{16.0, 14.0, 0.0, 3};
        MassingParams mp;
        MassingResult massing = generate_massing(prog, site, mp, seed);
        if (!massing.ok) {
            std::printf("seed %llu: massing: %s\n", (unsigned long long)seed,
                        massing.error.c_str());
            continue;
        }
        Ir ir;
        if (!massing_to_ir(massing.chosen, prog, mp, seed, ir, err)) {
            std::printf("seed %llu: massing_to_ir: %s\n", (unsigned long long)seed, err.c_str());
            continue;
        }
        PlanParams params;
        // BTG_MAX_RETRIES overrides the driver default for depth experiments.
        const char* mr = std::getenv("BTG_MAX_RETRIES");
        BuildingPlanResult bp =
            mr ? plan_building(prog, ir, params, seed, std::atoi(mr))
               : plan_building(prog, ir, params, seed);
        if (!bp.ok) {
            std::printf("seed %llu: plan: %s\n", (unsigned long long)seed, bp.error.c_str());
            continue;
        }
        int hard = 0;
        for (size_t k = 0; k < bp.storeys.size(); ++k) {
            hard += bp.storeys[k].cost.hard;
            if (bp.storeys[k].cost.hard > 0) {
                std::printf("seed %llu storey %zu:", (unsigned long long)seed, k);
                for (const auto& [name, cnt] : bp.storeys[k].cost.hard_terms)
                    if (cnt > 0) std::printf(" %s=%d", name.c_str(), cnt);
                std::printf("\n");
                // Where are the hearth rooms vs the stack?
                const FloorPlan& fp = bp.storeys[k].plan;
                for (const ChimneyCore& cc : bp.chimneys)
                    for (const glm::ivec2& cell : cc.cells)
                        std::printf("  stack %s cell (%d,%d)\n", cc.id.c_str(), cell.x, cell.y);
                for (const PlanRoom& r : fp.rooms)
                    if (r.hearth_code != 0)
                        std::printf("  hearth room %s [%d,%d - %d,%d] wings %zu\n",
                                    r.id.c_str(), r.x0, r.z0, r.x1, r.z1, r.wings.size());
            }
        }
        if (std::getenv("BTG_DEBUG_DUMP")) {
            // The accepted ground storey: rooms + the dark habitable ones.
            const PlanResult& pr0 = bp.storeys[0];
            std::string t = "  final storey 0:";
            for (const PlanRoom& r : pr0.plan.rooms) {
                t += " " + r.id + "[" + std::to_string(r.x0) + "," + std::to_string(r.z0) +
                     "-" + std::to_string(r.x1) + "," + std::to_string(r.z1) + "]" +
                     (r.wings.empty() ? "" : "w" + std::to_string(r.wings.size())) +
                     (r.pinned ? "F" : "");
            }
            std::printf("%s\n", t.c_str());
            for (size_t r = 0; r < pr0.plan.rooms.size(); ++r) {
                if (!pr0.plan.rooms[r].habitable) continue;
                bool lit = false;
                for (const DerivedOpening& o : pr0.openings.openings)
                    if (o.kind_code == kOpeningWindow && o.room_a == static_cast<int>(r))
                        lit = true;
                if (!lit)
                    std::printf("  dark: %s\n", pr0.plan.rooms[r].id.c_str());
            }
        }
        std::printf("seed %llu: hard=%d chimneys=%zu\n", (unsigned long long)seed, hard,
                    bp.chimneys.size());
    }
}

}  // namespace
}  // namespace building_topology_generator
