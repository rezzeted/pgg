// B3 acceptance gate (requirements §16 п. 3): the house program forced to the
// full B3 stack (two full storeys + attic), 100 seeds on the 16x14 plot, full
// anneal budget. Per seed: the massing is multi-storey (G2 rejects attics
// whose walk zone cannot host the stair, §9.1), G4 places one stair core per
// storey gap, every storey plan solves its hard terms with a valid rectangle
// tiling (I1), every room is reachable from its storey root (I3), and the
// emitted IR round-trips with every opening inside its host wall (I7) and the
// stair access edges in place. I6 (daylight) gates the full-height storeys;
// the attic is reported separately — its daylight comes from gable windows,
// and a hip attic has none until dormers arrive (post-B3, requirements C4).
// A three-seed subsample runs the E2E fill + §14 check through the
// building_geometry_generator (slow, PGG).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "building_geometry.h"
#include "emit.h"
#include "ir.h"
#include "massing.h"
#include "openings.h"
#include "pipeline.h"
#include "plan.h"
#include "program.h"
#include "stairs.h"

namespace building_topology_generator {
namespace {

struct RunResult {
    bool ok = false;
    std::string error;
    int hard = 0;
    double soft = 0.0;
    double seconds = 0.0;
    long moves = 0;
    int storeys = 0;
    int stairs = 0;
    // Invariant flags.
    bool tiling = false;       // I1 on every storey
    bool access = false;       // I3 on every storey
    bool daylight = false;     // I6 on the full-height storeys
    bool attic_lit = false;    // every habitable attic room has a (gable) window
    bool openings_ok = false;  // I7 through the emitted IR
    std::map<std::string, double> raw;  // soft-term breakdown, summed over storeys
};

RunResult run_one(const Program& program, uint64_t seed) {
    RunResult rr;
    Program p = program;
    p.storeys_min = 2;  // force the full B3 stack: two full storeys + attic
    p.attic = AtticMode::Required;
    const ExpandedProgram prog = expand_program(p);
    Site site;
    site.w = 16.0;
    site.d = 14.0;
    MassingParams mp;
    MassingResult massing = generate_massing(prog, site, mp, seed);
    if (!massing.ok) {
        rr.error = "massing: " + massing.error;
        return rr;
    }
    if (!massing.chosen.attic || massing.chosen.storeys != 2) {
        rr.error = "massing: the forced program did not yield 2 storeys + attic";
        return rr;
    }
    Ir ir;
    std::string err;
    if (!massing_to_ir(massing.chosen, prog, mp, seed, ir, err)) {
        rr.error = "massing_to_ir: " + err;
        return rr;
    }
    PlanParams params;
    BuildingPlanResult bp = plan_building(prog, ir, params, seed);
    if (!bp.ok) {
        rr.error = "plan: " + bp.error;
        return rr;
    }
    rr.storeys = static_cast<int>(bp.storeys.size());
    rr.stairs = static_cast<int>(bp.stairs.size());

    rr.hard = 0;
    rr.soft = 0.0;
    rr.tiling = true;
    rr.access = true;
    rr.daylight = true;
    rr.attic_lit = true;
    for (size_t k = 0; k < bp.storeys.size(); ++k) {
        const PlanResult& pr = bp.storeys[k];
        if (!pr.ok) {
            rr.error = "storey plan: " + pr.error;
            rr.ok = false;
            return rr;
        }
        rr.hard += pr.cost.hard;
        rr.soft += pr.cost.soft;
        for (const auto& [name, v] : pr.cost.raw) rr.raw[name] += v;
        rr.seconds += pr.anneal.seconds;
        rr.moves += pr.anneal.moves_tried;
        std::string terr;
        if (!pr.plan.check_rectangle_tiling(terr)) rr.tiling = false;
        for (char r : pr.openings.reachable) rr.access = rr.access && r != 0;
        // I6 gates the full-height storeys; the attic is tracked apart (hip
        // attics have no gable windows until dormers, requirements C4).
        const bool is_attic = ir.storeys[k].kind_code == kStoreyAttic;
        for (size_t r = 0; r < pr.plan.rooms.size(); ++r) {
            if (!pr.plan.rooms[r].habitable) continue;
            bool lit = false;
            for (const DerivedOpening& o : pr.openings.openings)
                if (o.kind_code == kOpeningWindow && o.room_a == static_cast<int>(r)) lit = true;
            if (!lit && is_attic) rr.attic_lit = false;
            if (!lit && !is_attic) rr.daylight = false;
        }
    }
    // The stair cores: one per storey gap, the shaft pinned on the
    // from-storey, the well carved on the to-storey.
    if (bp.stairs.size() + 1 != bp.storeys.size()) {
        rr.error = "stairs: " + std::to_string(bp.stairs.size()) + " cores for " +
                   std::to_string(bp.storeys.size()) + " storeys";
        return rr;
    }
    for (const StairCore& c : bp.stairs) {
        const PlanResult& from = bp.storeys[static_cast<size_t>(c.from)];
        const int ri = from.plan.find_room("stairwell");
        if (ri < 0 || !from.plan.rooms[static_cast<size_t>(ri)].pinned) {
            rr.error = "stairs: no pinned stairwell on storey " + std::to_string(c.from);
            return rr;
        }
        const PlanResult& to = bp.storeys[static_cast<size_t>(c.to)];
        for (int v : c.void_cells)
            if (to.plan.cells[static_cast<size_t>(v)] >= 0) {
                rr.error = "stairs: a room covers a well cell on storey " + std::to_string(c.to);
                return rr;
            }
    }
    // I7 through the emitted IR: every opening fits its host wall span; the
    // access edges reference existing doors/stairs; the IR round-trips (N6).
    rr.openings_ok = emit_building_ir(bp, prog, params, ir, err);
    if (rr.openings_ok) {
        for (const IrWall& w : ir.walls) {
            const double len = glm::length(w.b - w.a);
            for (const IrOpening& o : w.openings)
                if (o.s0 < -1e-4 || o.s1 > len + 1e-4) rr.openings_ok = false;
        }
        for (const IrAccessEdge& e : ir.access.edges) {
            bool ref_found = false;
            for (const IrWall& w : ir.walls)
                for (const IrOpening& o : w.openings)
                    if (o.id == e.ref) ref_found = true;
            for (const IrStair& s : ir.stairs)
                if (s.id == e.ref) ref_found = true;
            if (!ref_found) rr.openings_ok = false;
        }
        Ir back;
        rr.openings_ok = rr.openings_ok && read_ir_json(write_ir_json(ir), back, err) &&
                         back.spaces.size() == ir.spaces.size() &&
                         back.stairs.size() == ir.stairs.size() &&
                         back.slabs.size() == ir.slabs.size();
    }
    if (!rr.openings_ok && rr.error.empty()) rr.error = "emit/ir: " + err;
    rr.ok = true;
    return rr;
}

TEST(BuildingPlanB3Acceptance, HouseTwoStoreysAtticHundredSeeds) {
    Program program;
    std::string err;
    ASSERT_TRUE(load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/house.json", program,
                                  err))
        << err;
    const int kSeeds = 100;
    std::vector<RunResult> results(static_cast<size_t>(kSeeds));
    std::atomic<int> next{0};
    const int n_threads =
        std::max(2, std::min(8, static_cast<int>(std::thread::hardware_concurrency())));
    std::vector<std::thread> pool;
    for (int t = 0; t < n_threads; ++t)
        pool.emplace_back([&]() {
            for (;;) {
                const int i = next.fetch_add(1);
                if (i >= kSeeds) break;
                results[static_cast<size_t>(i)] = run_one(program, static_cast<uint64_t>(i) + 1);
            }
        });
    for (std::thread& th : pool) th.join();

    int fails = 0, tiling = 0, access = 0, daylight = 0, attic_lit = 0, openings_ok = 0;
    double soft_sum = 0.0, sec_sum = 0.0;
    long moves_sum = 0;
    std::vector<double> softs;
    std::map<std::string, double> raw_sum;
    std::string first_error;
    for (size_t i = 0; i < results.size(); ++i) {
        const RunResult& rr = results[i];
        if (!rr.ok || rr.hard != 0) {
            ++fails;
            if (first_error.empty())
                first_error = "seed " + std::to_string(i + 1) + ": " +
                              (rr.ok ? "hard = " + std::to_string(rr.hard) : rr.error);
            continue;
        }
        EXPECT_EQ(rr.storeys, 3) << "seed " << i + 1;
        EXPECT_EQ(rr.stairs, 2) << "seed " << i + 1;
        tiling += rr.tiling;
        access += rr.access;
        daylight += rr.daylight;
        attic_lit += rr.attic_lit;
        openings_ok += rr.openings_ok;
        soft_sum += rr.soft;
        for (const auto& [name, v] : rr.raw) raw_sum[name] += v;
        sec_sum += rr.seconds;
        moves_sum += rr.moves;
        softs.push_back(rr.soft);
    }
    std::sort(softs.begin(), softs.end());
    const double soft_mean = softs.empty() ? 0.0 : soft_sum / softs.size();
    const double soft_p95 =
        softs.empty() ? 0.0
                      : softs[std::min<size_t>(softs.size() - 1,
                                               static_cast<size_t>(softs.size() * 0.95))];
    const double moves_per_s = sec_sum > 0.0 ? moves_sum / sec_sum : 0.0;
    std::printf(
        "[b3 acceptance] house 2+attic: %d/%d ok, I1 %d%%, I3 %d%%, I6 %d%% (attic lit %d%%), "
        "I7 %d%%; soft mean %.3f p95 %.3f; %.0f moves/s (mean %.2f s)\n",
        kSeeds - fails, kSeeds, tiling, access, daylight, attic_lit, openings_ok, soft_mean,
        soft_p95, moves_per_s, sec_sum / kSeeds);
    // Soft-term breakdown (means over the passing seeds, summed over the three
    // storeys) — the tuning input of the metrics review (algorithms §14).
    if (!softs.empty()) {
        std::string b;
        for (const auto& [name, v] : raw_sum) {
            if (!b.empty()) b += ", ";
            b += name + " " + std::to_string(v / softs.size()).substr(0, 5);
        }
        std::printf("[b3 acceptance] soft raw means: %s\n", b.c_str());
    }

    // The §16 gate: hard invariants at 100%.
    EXPECT_EQ(fails, 0) << first_error;
    EXPECT_EQ(tiling, kSeeds);
    EXPECT_EQ(access, kSeeds);
    EXPECT_EQ(daylight, kSeeds);
    EXPECT_EQ(openings_ok, kSeeds);
    // Soft metrics: regression thresholds calibrated on the first green
    // 100-seed run (mean 6.61, p95 7.68) with ~25% headroom. The dominant
    // terms are support (C11 — sequential per-storey anneal barely aligns
    // upper partitions with the walls below; the joint optimization is the
    // metrics-review business, algorithms §14) and area (the attic bedroom's
    // net counts the h_walk cells only, §5.6 — it structurally undershoots
    // the target). requirements §16 B3 gates the hard invariants only.
    EXPECT_LT(soft_mean, 8.0);
    EXPECT_LT(soft_p95, 10.0);
}

// E2E subsample: the full chain massing -> plan -> emit -> PGG fill -> the
// §14 checks (slow; the topology gate above covers the 100-seed statistics).
TEST(BuildingPlanB3Acceptance, E2EFillCheckSample) {
    Program program;
    std::string err;
    ASSERT_TRUE(load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/house.json", program,
                                  err))
        << err;
    program.storeys_min = 2;
    program.attic = AtticMode::Required;
    const ExpandedProgram prog = expand_program(program);
    Site site;
    site.w = 16.0;
    site.d = 14.0;
    MassingParams mp;
    for (uint64_t seed : {1, 2, 3}) {
        MassingResult massing = generate_massing(prog, site, mp, seed);
        ASSERT_TRUE(massing.ok) << "seed " << seed << ": " << massing.error;
        Ir ir;
        ASSERT_TRUE(massing_to_ir(massing.chosen, prog, mp, seed, ir, err))
            << "seed " << seed << ": " << err;
        PlanParams params;
        BuildingPlanResult bp = plan_building(prog, ir, params, seed);
        ASSERT_TRUE(bp.ok) << "seed " << seed << ": " << bp.error;
        ASSERT_TRUE(emit_building_ir(bp, prog, params, ir, err))
            << "seed " << seed << ": " << err;

        building_geometry_generator::FillParams fp;
        fp.assets_dir = BUILDING_ASSETS_DIR;
        fp.import_roots = {BUILDING_PGG_CONTENT_DIR};
        fp.seed = seed;
        const building_geometry_generator::FillResult fill =
            building_geometry_generator::fill_building(ir, fp);
        for (const std::string& d : fill.diagnostics) ADD_FAILURE() << "seed " << seed << ": " << d;
        EXPECT_EQ(fill.units_built, fill.units_total) << "seed " << seed;
        ASSERT_NE(fill.mesh, nullptr) << "seed " << seed;
        EXPECT_GT(fill.mesh->pointCount(), 0u) << "seed " << seed;
        const building_geometry_generator::CheckResult check =
            building_geometry_generator::check_building(ir, fill);
        for (const std::string& f : check.failures) ADD_FAILURE() << "seed " << seed << ": " << f;
        EXPECT_TRUE(check.ok()) << "seed " << seed;
    }
}

}  // namespace
}  // namespace building_topology_generator
