// Fast unit tests for the B2 plan stage (G5 growth, G6 openings, anneal L0,
// G8 emit): rectangle-tiling invariant, full reachability, daylight, door
// links, determinism (N1). The 3x100-seed acceptance gate lives in
// building_plan_acceptance_test (slow).

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "anneal.h"
#include "cost.h"
#include "emit.h"
#include "massing.h"
#include "openings.h"
#include "plan.h"
#include "program.h"

namespace building_topology_generator {
namespace {

struct Fixture {
    ExpandedProgram prog;
    MassingCandidate cand;
    PlanParams params;
};

// Program -> clamp to one storey -> massing on a 16x14 plot.
Fixture make_fixture(const std::string& program_path, uint64_t seed) {
    Fixture f;
    Program p;
    std::string err;
    EXPECT_TRUE(load_program_file(program_path, p, err)) << err;
    clamp_single_storey(p);
    f.prog = expand_program(p);
    Site site;
    site.w = 16.0;
    site.d = 14.0;
    MassingParams mp;
    MassingResult massing = generate_massing(f.prog, site, mp, seed);
    EXPECT_TRUE(massing.ok) << massing.error;
    f.cand = massing.chosen;
    return f;
}

std::string program_path(const std::string& name) {
    return std::string(BUILDING_PROGRAMS_DIR) + "/" + name + ".json";
}

TEST(BuildingPlan, GrowProducesValidTiling) {
    for (const char* name : {"house", "bakery", "tavern"}) {
        for (uint64_t seed = 1; seed <= 5; ++seed) {
            Fixture f = make_fixture(program_path(name), seed);
            Rng rng = Rng::from_seed(seed);
            std::string err;
            FloorPlan fp = grow_floor(f.prog, f.cand, f.params, rng, err);
            ASSERT_TRUE(err.empty()) << name << " seed " << seed << ": " << err;
            std::string terr;
            EXPECT_TRUE(fp.check_rectangle_tiling(terr)) << name << " seed " << seed << ": " << terr;
            // All required program rooms are present.
            for (const RoomInstance& r : f.prog.rooms)
                if (!r.optional)
                    EXPECT_GE(fp.find_room(r.id), 0)
                        << name << " seed " << seed << ": missing room " << r.id;
        }
    }
}

TEST(BuildingPlan, GrowIsDeterministic) {
    Fixture f = make_fixture(program_path("house"), 7);
    Rng r1 = Rng::from_seed(7), r2 = Rng::from_seed(7);
    std::string err;
    FloorPlan a = grow_floor(f.prog, f.cand, f.params, r1, err);
    FloorPlan b = grow_floor(f.prog, f.cand, f.params, r2, err);
    ASSERT_EQ(a.rooms.size(), b.rooms.size());
    for (size_t i = 0; i < a.rooms.size(); ++i) {
        EXPECT_EQ(a.rooms[i].id, b.rooms[i].id);
        EXPECT_EQ(a.rooms[i].x0, b.rooms[i].x0);
        EXPECT_EQ(a.rooms[i].x1, b.rooms[i].x1);
        EXPECT_EQ(a.rooms[i].z0, b.rooms[i].z0);
        EXPECT_EQ(a.rooms[i].z1, b.rooms[i].z1);
    }
}

TEST(BuildingPlan, PlanFloorSolvesHardTerms) {
    // A reduced budget keeps the unit test fast; the hard terms must still
    // reach zero on the three stock programs (house, bakery, tavern).
    for (const char* name : {"house", "bakery", "tavern"}) {
        Fixture f = make_fixture(program_path(name), 42);
        f.params.budget = 4000;
        PlanResult res = plan_floor(f.prog, f.cand, f.params, 42);
        ASSERT_TRUE(res.ok) << name << ": " << res.error;
        EXPECT_EQ(res.cost.hard, 0) << name;
        std::string terr;
        EXPECT_TRUE(res.plan.check_rectangle_tiling(terr)) << name << ": " << terr;
    }
}

TEST(BuildingPlan, DaylightAndAccess) {
    Fixture f = make_fixture(program_path("house"), 1);
    f.params.budget = 4000;
    PlanResult res = plan_floor(f.prog, f.cand, f.params, 1);
    ASSERT_TRUE(res.ok) << res.error;
    // I3: every room reachable from the street.
    for (size_t r = 0; r < res.plan.rooms.size(); ++r)
        EXPECT_TRUE(res.openings.reachable[r]) << res.plan.rooms[r].id << " unreachable";
    // I6: every habitable room has a window.
    for (size_t r = 0; r < res.plan.rooms.size(); ++r) {
        if (!res.plan.rooms[r].habitable) continue;
        int wins = 0;
        for (const DerivedOpening& o : res.openings.openings)
            if (o.kind_code == kOpeningWindow && o.room_a == static_cast<int>(r)) ++wins;
        EXPECT_GT(wins, 0) << res.plan.rooms[r].id << " has no window";
    }
    // The entrance sits on the front facade (+Z side of the building).
    bool have_entrance = false;
    for (const DerivedOpening& o : res.openings.openings)
        if (o.kind_code == kOpeningDoor && o.room_b == -2 && !o.vertical) {
            const PlanRoom& room = res.plan.rooms[static_cast<size_t>(o.room_a)];
            if (room.entrance_front && o.line == room.z1) have_entrance = true;
        }
    EXPECT_TRUE(have_entrance);
}

TEST(BuildingPlan, AnnealImprovesOrHolds) {
    Fixture f = make_fixture(program_path("bakery"), 5);
    f.params.budget = 3000;
    PlanResult res = plan_floor(f.prog, f.cand, f.params, 5);
    ASSERT_TRUE(res.ok) << res.error;
    const CostBreakdown& i = res.anneal.initial;
    const CostBreakdown& fin = res.anneal.final;
    EXPECT_LE(fin.hard, i.hard);
    if (fin.hard == i.hard) EXPECT_LE(fin.soft, i.soft + 1e-9);
}

TEST(BuildingPlan, PlanFloorIsDeterministic) {
    Fixture f = make_fixture(program_path("house"), 11);
    f.params.budget = 2000;
    PlanResult a = plan_floor(f.prog, f.cand, f.params, 11);
    PlanResult b = plan_floor(f.prog, f.cand, f.params, 11);
    ASSERT_TRUE(a.ok && b.ok);
    ASSERT_EQ(a.plan.rooms.size(), b.plan.rooms.size());
    for (size_t i = 0; i < a.plan.rooms.size(); ++i) {
        EXPECT_EQ(a.plan.rooms[i].x0, b.plan.rooms[i].x0);
        EXPECT_EQ(a.plan.rooms[i].x1, b.plan.rooms[i].x1);
        EXPECT_EQ(a.plan.rooms[i].z0, b.plan.rooms[i].z0);
        EXPECT_EQ(a.plan.rooms[i].z1, b.plan.rooms[i].z1);
    }
    EXPECT_EQ(a.openings.openings.size(), b.openings.openings.size());
    EXPECT_DOUBLE_EQ(a.cost.soft, b.cost.soft);
    EXPECT_EQ(a.cost.hard, b.cost.hard);
}

TEST(BuildingPlan, EmitProducesConsistentIr) {
    Fixture f = make_fixture(program_path("tavern"), 3);
    f.params.budget = 3000;
    PlanResult res = plan_floor(f.prog, f.cand, f.params, 3);
    ASSERT_TRUE(res.ok) << res.error;
    Ir ir;
    std::string err;
    MassingParams mp;
    ASSERT_TRUE(massing_to_ir(f.cand, f.prog, mp, 3, ir, err)) << err;
    ASSERT_TRUE(emit_floor_ir(res.plan, res.openings, f.prog, f.params, res.cost, res.anneal,
                              ir, err))
        << err;

    // Every opening sits inside its host wall (I7) with a sane span.
    for (const IrWall& w : ir.walls) {
        const double len = glm::length(w.b - w.a);
        for (const IrOpening& o : w.openings) {
            EXPECT_GE(o.s0, -1e-4) << o.id;
            EXPECT_LE(o.s1, len + 1e-4) << o.id;
            EXPECT_GT(o.s1 - o.s0, 0.5) << o.id;
        }
    }
    // Access edges reference existing doors and spaces.
    for (const IrAccessEdge& e : ir.access.edges) {
        EXPECT_TRUE(std::find(ir.access.nodes.begin(), ir.access.nodes.end(), e.a) !=
                    ir.access.nodes.end())
            << e.a;
        EXPECT_TRUE(std::find(ir.access.nodes.begin(), ir.access.nodes.end(), e.b) !=
                    ir.access.nodes.end())
            << e.b;
        bool door_found = false;
        for (const IrWall& w : ir.walls)
            for (const IrOpening& o : w.openings)
                if (o.id == e.ref) door_found = true;
        EXPECT_TRUE(door_found) << e.ref;
    }
    // Round-trip through the JSON writer/reader (N6).
    const std::string text = write_ir_json(ir);
    Ir back;
    ASSERT_TRUE(read_ir_json(text, back, err)) << err;
    EXPECT_EQ(back.spaces.size(), ir.spaces.size());
    EXPECT_EQ(back.walls.size(), ir.walls.size());
    EXPECT_EQ(back.access.edges.size(), ir.access.edges.size());
}

}  // namespace
}  // namespace building_topology_generator
