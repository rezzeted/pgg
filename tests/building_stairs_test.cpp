// Fast unit tests for the B3 stages: G4 stair cores (requirements §5.5, §9.1
// — sizing by Blondel, pinned shaft, well voids, the attic walk mask), the
// storey fields (§5.6 — knee/walk masks), and G3 storey assignment (§7.3 —
// ground hosts, zone preferences, failure hints). The multi-storey fixtures
// come from massing_from_pin: a validated pinned massing keeps the test
// deterministic without scanning the candidate space.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "massing.h"
#include "pipeline.h"
#include "plan.h"
#include "program.h"
#include "stairs.h"
#include "storeys.h"

namespace building_topology_generator {
namespace {

constexpr double kCell = 0.5;
constexpr double kTExt = 0.45;

// Outline of a w x d cell rect volume (the inner field is exactly w x d
// cells): rect_outline(0, 0, w*s, d*s, t_ext) as in massing.cpp.
std::vector<glm::dvec2> cell_rect_outline(int w, int d) {
    const double t = kTExt / 2;
    return {{-t, -t}, {-t, d * kCell + t}, {w * kCell + t, d * kCell + t}, {w * kCell + t, -t}};
}

struct Fixture {
    ExpandedProgram prog;
    Ir ir;
    StoreyFields fields;
};

// The house program on a pinned 16x12-cell rect massing (8.45 x 6.45 m —
// grid-exact). full_storeys = 1 or 2, attic adds the attic storey with a
// 50-degree gable (slopes on the z edges, as the massing scans it).
Fixture make_fixture(int full_storeys, bool attic, double eave_rise = 0.8) {
    Fixture f;
    Program p;
    std::string err;
    EXPECT_TRUE(load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/house.json", p, err))
        << err;
    f.prog = expand_program(p);
    MassingVolume v;
    v.outline = cell_rect_outline(16, 12);
    v.storeys = full_storeys;
    v.attic = attic;
    v.roof_kind = kRoofGable;
    v.pitch_by_edge = {90.0, 50.0, 90.0, 50.0};
    v.eave_rise = attic ? eave_rise : 0.0;
    MassingParams mp;
    const MassingResult m =
        massing_from_pin(f.prog, Site{16.0, 14.0, 0.0, 3}, mp, 1, {v}, "test");
    EXPECT_TRUE(m.ok) << m.error;
    EXPECT_TRUE(massing_to_ir(m.chosen, f.prog, mp, 1, f.ir, err)) << err;
    EXPECT_TRUE(storey_fields(f.ir, f.fields, err)) << err;
    return f;
}

int attic_index(const Ir& ir) {
    for (size_t k = 0; k < ir.storeys.size(); ++k)
        if (ir.storeys[k].kind_code == kStoreyAttic) return static_cast<int>(k);
    return -1;
}

// ---------------------------------------------------------------------------
// §5.5 sizing (I2 by construction)
// ---------------------------------------------------------------------------

TEST(BuildingStairs, SizeStairBlondel) {
    for (double h : {2.5, 2.8, 2.85, 3.05, 3.5, 4.0, 5.0}) {
        int risers = 0;
        double rise = 0.0, going = 0.0;
        size_stair(h, risers, rise, going);
        EXPECT_DOUBLE_EQ(rise * risers, h) << "H=" << h;
        EXPECT_LE(rise, 0.19 + 1e-9) << "H=" << h;
        EXPECT_DOUBLE_EQ(going, 0.25);
        const double blondel = 2 * rise + going;
        EXPECT_GT(blondel, 0.59) << "H=" << h;
        EXPECT_LE(blondel, 0.63 + 1e-9) << "H=" << h;
        EXPECT_EQ(risers, std::max(4, static_cast<int>(std::ceil(h / 0.19 - 1e-9))));
    }
}

// ---------------------------------------------------------------------------
// G4 placement (§9.1): shaft, well, arrival, door_ok
// ---------------------------------------------------------------------------

TEST(BuildingStairs, PlaceStairsTwoStorey) {
    Fixture f = make_fixture(2, false);
    Rng rng = Rng::from_seed(7);
    PlanParams params;
    std::vector<StairCore> cores;
    std::string err;
    ASSERT_TRUE(place_stairs(f.ir, f.fields, params, rng, cores, err)) << err;
    ASSERT_EQ(cores.size(), 1u);
    const StairCore& c = cores[0];
    EXPECT_EQ(c.id, "stair1");
    EXPECT_EQ(c.from, 0);
    EXPECT_EQ(c.to, 1);
    // Sizing matches §5.5 on the true gap.
    const double h_gap = f.ir.storeys[1].y - f.ir.storeys[0].y;
    int risers = 0;
    double rise = 0.0, going = 0.0;
    size_stair(h_gap, risers, rise, going);
    EXPECT_EQ(c.risers, risers);
    EXPECT_DOUBLE_EQ(c.rise, rise);
    EXPECT_DOUBLE_EQ(c.going, going);
    EXPECT_GT(c.risers_low, 1);
    EXPECT_LT(c.risers_low, c.risers - 1);
    // The shaft is a pinned room inside the from-storey field.
    const PlanRoom& shaft = c.shaft_room;
    EXPECT_TRUE(shaft.pinned);
    EXPECT_TRUE(shaft.fixed());
    EXPECT_EQ(shaft.id, "stairwell");
    EXPECT_GE(shaft.x0, 0);
    EXPECT_GE(shaft.z0, 0);
    EXPECT_LE(shaft.x1, f.fields.w);
    EXPECT_LE(shaft.z1, f.fields.h);
    EXPECT_GT(shaft.area_cells(), 0);
    // door_ok covers the shaft bbox and allows at least the approach pad.
    ASSERT_EQ(shaft.door_ok.size(),
              static_cast<size_t>(shaft.x1 - shaft.x0) * (shaft.z1 - shaft.z0));
    EXPECT_TRUE(std::find(shaft.door_ok.begin(), shaft.door_ok.end(), 1) != shaft.door_ok.end());
    // The shaft cells stay free floor on the from-storey (the growth tiles
    // around the pinned room; only the to-storey is carved).
    for (int j = shaft.z0; j < shaft.z1; ++j)
        for (int i = shaft.x0; i < shaft.x1; ++i)
            EXPECT_EQ(f.fields.mask[0][static_cast<size_t>(j) * f.fields.w + i], kCellFree);
    // The well is carved into the to-storey mask; the arrival row is not.
    ASSERT_FALSE(c.void_cells.empty());
    int free_before = 0, free_after = 0;
    for (int32_t m : f.fields.mask[1])
        if (m == kCellFree || m == kCellVoid) ++free_after;
    for (int v : c.void_cells)
        EXPECT_EQ(f.fields.mask[1][static_cast<size_t>(v)], kCellVoid);
    for (int a : c.arrival_cells)
        EXPECT_EQ(f.fields.mask[1][static_cast<size_t>(a)], kCellFree);
    (void)free_before;
    // The well rects (meters, for the slab) cover exactly the void cells.
    std::vector<int32_t> wm(static_cast<size_t>(f.fields.w) * f.fields.h, kCellOutside);
    for (const IrSlabRect& r : c.well_rects) {
        EXPECT_LT(r.x0, r.x1);
        EXPECT_LT(r.z0, r.z1);
        const int i0 = static_cast<int>(std::lround((r.x0 - f.fields.origin.x) / kCell));
        const int j0 = static_cast<int>(std::lround((r.z0 - f.fields.origin.y) / kCell));
        const int i1 = static_cast<int>(std::lround((r.x1 - f.fields.origin.x) / kCell));
        const int j1 = static_cast<int>(std::lround((r.z1 - f.fields.origin.y) / kCell));
        for (int j = j0; j < j1; ++j)
            for (int i = i0; i < i1; ++i)
                wm[static_cast<size_t>(j) * f.fields.w + i] = kCellFree;
    }
    for (int v : c.void_cells) EXPECT_EQ(wm[static_cast<size_t>(v)], kCellFree);
    int covered = 0;
    for (int32_t m : wm)
        if (m == kCellFree) ++covered;
    EXPECT_EQ(covered, static_cast<int>(c.void_cells.size()));
}

TEST(BuildingStairs, PlaceStairsIntoAtticStaysInWalkMask) {
    Fixture f = make_fixture(1, true);
    const int at = attic_index(f.ir);
    ASSERT_EQ(at, 1);
    Rng rng = Rng::from_seed(11);
    PlanParams params;
    std::vector<StairCore> cores;
    std::string err;
    ASSERT_TRUE(place_stairs(f.ir, f.fields, params, rng, cores, err)) << err;
    ASSERT_EQ(cores.size(), 1u);
    // §14.2 + §5.6: the walked parts of the stair into the attic (the whole
    // well plus the arrival row) stay inside the h_walk mask.
    const std::vector<char>& walk = f.fields.walk[1];
    ASSERT_EQ(walk.size(), static_cast<size_t>(f.fields.w) * f.fields.h);
    for (int v : cores[0].void_cells) EXPECT_EQ(walk[static_cast<size_t>(v)], 1);
    for (int a : cores[0].arrival_cells) EXPECT_EQ(walk[static_cast<size_t>(a)], 1);
}

TEST(BuildingStairs, StackedCoresDoNotOverlap) {
    Fixture f = make_fixture(2, true);
    Rng rng = Rng::from_seed(3);
    PlanParams params;
    std::vector<StairCore> cores;
    std::string err;
    ASSERT_TRUE(place_stairs(f.ir, f.fields, params, rng, cores, err)) << err;
    ASSERT_EQ(cores.size(), 2u);
    EXPECT_EQ(cores[0].from, 0);
    EXPECT_EQ(cores[0].to, 1);
    EXPECT_EQ(cores[1].from, 1);
    EXPECT_EQ(cores[1].to, 2);
    // The upper core's shaft sits on cells the lower core's well did NOT
    // carve (stacking is preferred, overlapping is impossible).
    const PlanRoom& up = cores[1].shaft_room;
    for (int j = up.z0; j < up.z1; ++j)
        for (int i = up.x0; i < up.x1; ++i)
            EXPECT_EQ(f.fields.mask[1][static_cast<size_t>(j) * f.fields.w + i], kCellFree);
    for (int v : cores[0].void_cells)
        EXPECT_EQ(f.fields.mask[1][static_cast<size_t>(v)], kCellVoid);
    for (int v : cores[1].void_cells)
        EXPECT_EQ(f.fields.mask[2][static_cast<size_t>(v)], kCellVoid);
    // Into the attic the walked parts respect the h_walk mask.
    for (int v : cores[1].void_cells)
        EXPECT_EQ(f.fields.walk[2][static_cast<size_t>(v)], 1);
    for (int a : cores[1].arrival_cells)
        EXPECT_EQ(f.fields.walk[2][static_cast<size_t>(a)], 1);
}

TEST(BuildingStairs, AtticFromStoreyHasNoExit) {
    Fixture f = make_fixture(2, false);
    // A non-topmost attic can never place a stair out of it (§5.5).
    f.ir.storeys[0].kind_code = kStoreyAttic;
    Rng rng = Rng::from_seed(1);
    PlanParams params;
    std::vector<StairCore> cores;
    std::string err;
    EXPECT_FALSE(place_stairs(f.ir, f.fields, params, rng, cores, err));
    EXPECT_NE(err.find("no exit"), std::string::npos) << err;
}

// ---------------------------------------------------------------------------
// §5.6 storey fields: the attic knee/walk masks
// ---------------------------------------------------------------------------

TEST(BuildingStoreys, AtticKneeWalkMasks) {
    Fixture f = make_fixture(1, true);
    const int at = attic_index(f.ir);
    ASSERT_EQ(at, 1);
    const size_t n = static_cast<size_t>(f.fields.w) * f.fields.h;
    // Non-attic storeys carry no walk mask and no roof-void cells.
    ASSERT_TRUE(f.fields.walk[0].empty());
    for (int32_t m : f.fields.mask[0]) EXPECT_NE(m, kCellRoofVoid);
    // The attic has both a roof-void eave strip and a walkable ridge zone on
    // this massing (50 deg gable, eave_rise 0.8 over a 6.45 m deep volume).
    ASSERT_EQ(f.fields.walk[1].size(), n);
    int roof_void = 0, free = 0, walk = 0;
    for (size_t c = 0; c < n; ++c) {
        const int32_t m = f.fields.mask[1][c];
        const char wk = f.fields.walk[1][c];
        if (m == kCellRoofVoid) ++roof_void;
        if (m == kCellFree) ++free;
        if (wk) ++walk;
        // h_knee < h_walk: a roof-void cell can never be walkable, and a
        // walkable cell is always free floor.
        if (m == kCellRoofVoid) EXPECT_EQ(wk, 0);
        if (wk) EXPECT_EQ(m, kCellFree);
    }
    EXPECT_GT(roof_void, 0);
    EXPECT_GT(walk, 0);
    EXPECT_GE(free, walk);
}

TEST(BuildingStoreys, RoomAreaNetCountsWalkCellsOnly) {
    // §5.6: the net area of an attic room sums only the h_walk cells (minus
    // the wall allowances). Synthetic 6x4-cell room on an 8x6 field.
    FloorPlan fp;
    fp.w = 8;
    fp.h = 6;
    fp.outside.assign(48, kCellFree);
    fp.useful.assign(48, 0);
    PlanRoom room;
    room.id = "bed.0";
    room.x0 = 1;
    room.z0 = 1;
    room.x1 = 7;
    room.z1 = 5;
    fp.rooms.push_back(room);
    // Only the top two rows of the room clear h_walk.
    for (int j = 3; j < 5; ++j)
        for (int i = 1; i < 7; ++i) fp.useful[static_cast<size_t>(j) * 8 + i] = 1;
    fp.rasterize();
    IrParams p;
    const double s = p.cell;
    // Walk cells: 6x2 = 12 cells gross. Wall allowance: every useful cell
    // borders the field's free cells (partitions) on its non-room sides.
    // Rows j=3,4; cols i=1..6. Bottom row j=3 borders non-useful room cells
    // (a partition) along -z; the field outside the room is kCellFree here
    // (all-partition test field), so the boundary rows/cols each shed
    // t_partition/2 per non-room neighbor side.
    double expect = 0.0;
    for (int j = 3; j < 5; ++j)
        for (int i = 1; i < 7; ++i) {
            double a = s * s;
            const int di[4] = {-1, 1, 0, 0};
            const int dj[4] = {0, 0, -1, 1};
            for (int q = 0; q < 4; ++q) {
                const int ni = i + di[q], nj = j + dj[q];
                const bool own = ni >= 1 && ni < 7 && nj >= 1 && nj < 5;
                if (!own) a -= side_half_t(kCellFree, p) * s;
            }
            expect += a;
        }
    EXPECT_DOUBLE_EQ(fp.room_area_net(0, p), expect);
    // Without the useful mask the same room measures its bbox net.
    fp.useful.clear();
    double w_m, d_m;
    fp.room_dims_net(0, p, w_m, d_m);
    EXPECT_DOUBLE_EQ(fp.room_area_net(0, p), w_m * d_m);
}

// ---------------------------------------------------------------------------
// G3 assignment (§7.3)
// ---------------------------------------------------------------------------

TEST(BuildingStoreys, AssignStoreysGroundHostsAndZones) {
    Fixture f = make_fixture(2, true);
    Rng rng = Rng::from_seed(5);
    PlanParams params;
    std::vector<StairCore> cores;
    std::string err;
    ASSERT_TRUE(place_stairs(f.ir, f.fields, params, rng, cores, err)) << err;
    std::vector<int> storey_of;
    ASSERT_TRUE(
        assign_storeys(f.prog, f.ir, f.fields, cores, params.comms_share, storey_of, err))
        << err;
    ASSERT_EQ(storey_of.size(), f.prog.rooms.size());
    auto idx_of = [&](const std::string& id) {
        for (size_t i = 0; i < f.prog.rooms.size(); ++i)
            if (f.prog.rooms[i].id == id) return static_cast<int>(i);
        return -1;
    };
    // Every required room lands somewhere (§7.3).
    for (size_t i = 0; i < f.prog.rooms.size(); ++i)
        if (!f.prog.rooms[i].optional)
            EXPECT_GE(storey_of[i], 0) << f.prog.rooms[i].id;
    // The entrance and the back door keep a host on the ground storey (§7.1):
    // the house entrance goes into lobby/hall, the back door into hall.
    const int lobby = idx_of("lobby"), hall = idx_of("hall");
    ASSERT_GE(lobby, 0);
    ASSERT_GE(hall, 0);
    EXPECT_EQ(storey_of[static_cast<size_t>(lobby)], 0);
    EXPECT_EQ(storey_of[static_cast<size_t>(hall)], 0);
    // The private zone goes up (§7.3): bed.0 is required, upper/attic only.
    const int bed0 = idx_of("bed.0");
    ASSERT_GE(bed0, 0);
    EXPECT_GT(storey_of[static_cast<size_t>(bed0)], 0);
    // subprogram_for: the ground subprogram keeps the entrance data, the
    // upper one is rooted at the stair arrival (no entrance, no back door).
    for (int k = 0; k < 3; ++k) {
        const ExpandedProgram sub = subprogram_for(f.prog, storey_of, k);
        for (const RoomInstance& r : sub.rooms)
            EXPECT_EQ(storey_of[static_cast<size_t>(idx_of(r.id))], k);
        for (const ProgramLink& l : sub.links) {
            EXPECT_EQ(storey_of[static_cast<size_t>(idx_of(l.a))], k);
            EXPECT_EQ(storey_of[static_cast<size_t>(idx_of(l.b))], k);
        }
        if (k == 0) {
            EXPECT_FALSE(sub.entrance_into.empty());
            EXPECT_TRUE(sub.has_back_door);
        } else {
            EXPECT_TRUE(sub.entrance_into.empty());
            EXPECT_FALSE(sub.has_back_door);
        }
    }
}

TEST(BuildingStoreys, AssignStoreysFailsWithoutGroundEntranceHost) {
    Fixture f = make_fixture(2, true);
    Rng rng = Rng::from_seed(5);
    PlanParams params;
    std::vector<StairCore> cores;
    std::string err;
    ASSERT_TRUE(place_stairs(f.ir, f.fields, params, rng, cores, err)) << err;
    // The only entrance host is upper/attic-only: no required room can host
    // the street door on the ground storey (F1-style hint expected).
    f.prog.entrance_into = {"bed.0"};
    std::vector<int> storey_of;
    EXPECT_FALSE(assign_storeys(f.prog, f.ir, f.fields, cores, params.comms_share, storey_of,
                                err));
    EXPECT_NE(err.find("entrance"), std::string::npos) << err;
    EXPECT_NE(err.find("storeys mask"), std::string::npos) << err;
}

// ---------------------------------------------------------------------------
// The B3 driver end to end (G3 -> G4 -> G5/G6 per storey)
// ---------------------------------------------------------------------------

TEST(BuildingStairs, PlanBuildingMultiStoreyHardZero) {
    Fixture f = make_fixture(2, true);
    PlanParams params;
    params.budget = 4000;  // the acceptance gate runs the full budget
    BuildingPlanResult bp = plan_building(f.prog, f.ir, params, 42);
    ASSERT_TRUE(bp.ok) << bp.error;
    ASSERT_EQ(bp.storeys.size(), 3u);
    ASSERT_EQ(bp.storey_of.size(), f.prog.rooms.size());
    // Every storey solves its hard terms and tiles its field (I10).
    for (size_t k = 0; k < bp.storeys.size(); ++k) {
        const PlanResult& pr = bp.storeys[k];
        ASSERT_TRUE(pr.ok) << "storey " << k << ": " << pr.error;
        EXPECT_EQ(pr.cost.hard, 0) << "storey " << k;
        std::string terr;
        EXPECT_TRUE(pr.plan.check_rectangle_tiling(terr)) << "storey " << k << ": " << terr;
        // I3: every room reachable from the storey root.
        for (size_t r = 0; r < pr.plan.rooms.size(); ++r)
            EXPECT_TRUE(pr.openings.reachable[r])
                << "storey " << k << ": " << pr.plan.rooms[r].id << " unreachable";
    }
    // The stair shafts are the pinned rooms of their from-storeys.
    ASSERT_EQ(bp.stairs.size(), 2u);
    for (const StairCore& c : bp.stairs) {
        const PlanResult& pr = bp.storeys[static_cast<size_t>(c.from)];
        const int ri = pr.plan.find_room("stairwell");
        ASSERT_GE(ri, 0) << "storey " << c.from;
        const PlanRoom& shaft = pr.plan.rooms[static_cast<size_t>(ri)];
        EXPECT_TRUE(shaft.pinned);
        EXPECT_EQ(shaft.x0, c.shaft_room.x0);
        EXPECT_EQ(shaft.z0, c.shaft_room.z0);
        EXPECT_EQ(shaft.x1, c.shaft_room.x1);
        EXPECT_EQ(shaft.z1, c.shaft_room.z1);
        // No room covers a well cell of the storey above (the growth tiles
        // around the voids, I10 over the mask).
        const PlanResult& up = bp.storeys[static_cast<size_t>(c.to)];
        for (int v : c.void_cells) EXPECT_LT(up.plan.cells[static_cast<size_t>(v)], 0);
    }
    // Every assigned program room appears on its G3 storey, exactly once.
    for (size_t i = 0; i < f.prog.rooms.size(); ++i) {
        if (bp.storey_of[i] < 0) {
            EXPECT_TRUE(f.prog.rooms[i].optional) << f.prog.rooms[i].id;
            continue;
        }
        const PlanResult& pr = bp.storeys[static_cast<size_t>(bp.storey_of[i])];
        EXPECT_GE(pr.plan.find_room(f.prog.rooms[i].id), 0) << f.prog.rooms[i].id;
    }
}

TEST(BuildingStairs, PlanBuildingIsDeterministic) {
    Fixture f = make_fixture(2, true);
    PlanParams params;
    params.budget = 2000;
    BuildingPlanResult a = plan_building(f.prog, f.ir, params, 9);
    BuildingPlanResult b = plan_building(f.prog, f.ir, params, 9);
    ASSERT_TRUE(a.ok && b.ok);
    ASSERT_EQ(a.storeys.size(), b.storeys.size());
    EXPECT_EQ(a.storey_of, b.storey_of);
    for (size_t k = 0; k < a.storeys.size(); ++k) {
        ASSERT_EQ(a.storeys[k].plan.rooms.size(), b.storeys[k].plan.rooms.size());
        for (size_t r = 0; r < a.storeys[k].plan.rooms.size(); ++r) {
            const PlanRoom& x = a.storeys[k].plan.rooms[r];
            const PlanRoom& y = b.storeys[k].plan.rooms[r];
            EXPECT_EQ(x.id, y.id);
            EXPECT_EQ(x.x0, y.x0);
            EXPECT_EQ(x.x1, y.x1);
            EXPECT_EQ(x.z0, y.z0);
            EXPECT_EQ(x.z1, y.z1);
            EXPECT_EQ(x.wings, y.wings);
        }
        EXPECT_EQ(a.storeys[k].cost.hard, b.storeys[k].cost.hard);
        EXPECT_DOUBLE_EQ(a.storeys[k].cost.soft, b.storeys[k].cost.soft);
    }
    ASSERT_EQ(a.stairs.size(), b.stairs.size());
    for (size_t c = 0; c < a.stairs.size(); ++c) {
        EXPECT_EQ(a.stairs[c].void_cells, b.stairs[c].void_cells);
        EXPECT_EQ(a.stairs[c].risers_low, b.stairs[c].risers_low);
    }
}

}  // namespace
}  // namespace building_topology_generator
