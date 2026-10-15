// B2 acceptance gate (requirements §16): the three stock programs, 100 seeds
// each on a 16x14 plot, full anneal budget. Hard invariants I1 (rectangle
// tiling), I3 (access from the street), I6 (daylight), I7 (openings inside
// walls) must hold in 100% of the runs; the soft metrics are reported with
// generous regression thresholds; the move throughput is printed for the N2
// extrapolation (a house must stay under 5 s of Debug search).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "anneal.h"
#include "cost.h"
#include "emit.h"
#include "massing.h"
#include "openings.h"
#include "plan.h"
#include "program.h"

namespace building_topology_generator {
namespace {

std::string program_path(const std::string& name) {
    return std::string(BUILDING_PROGRAMS_DIR) + "/" + name + ".json";
}

struct RunResult {
    bool ok = false;
    std::string error;
    int hard = 0;
    double soft = 0.0;
    double seconds = 0.0;
    int moves = 0;
    // Invariant flags.
    bool tiling = false;      // I1
    bool access = false;      // I3
    bool daylight = false;    // I6
    bool openings_ok = false; // I7
};

RunResult run_one(const Program& program, uint64_t seed) {
    RunResult rr;
    Program p = program;
    clamp_single_storey(p);
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
    PlanParams params;
    PlanResult plan = plan_floor(prog, massing.chosen, params, seed);
    if (!plan.ok) {
        rr.error = "plan: " + plan.error;
        return rr;
    }
    rr.ok = true;
    rr.hard = plan.cost.hard;
    rr.soft = plan.cost.soft;
    rr.seconds = plan.anneal.seconds;
    rr.moves = plan.anneal.moves_tried;

    std::string terr;
    rr.tiling = plan.plan.check_rectangle_tiling(terr);
    rr.access = true;
    for (char r : plan.openings.reachable) rr.access = rr.access && r != 0;
    rr.daylight = true;
    for (size_t r = 0; r < plan.plan.rooms.size(); ++r) {
        if (!plan.plan.rooms[r].habitable) continue;
        bool lit = false;
        for (const DerivedOpening& o : plan.openings.openings)
            if (o.kind_code == kOpeningWindow && o.room_a == static_cast<int>(r)) lit = true;
        if (!lit) rr.daylight = false;
    }
    // I7 through the emitted IR: emit fails when an opening finds no host
    // wall; then every opening must fit its wall span.
    Ir ir;
    std::string err;
    rr.openings_ok = massing_to_ir(massing.chosen, prog, mp, seed, ir, err) &&
                     emit_floor_ir(plan.plan, plan.openings, prog, params, plan.cost,
                                   plan.anneal, ir, err);
    if (rr.openings_ok)
        for (const IrWall& w : ir.walls) {
            const double len = glm::length(w.b - w.a);
            for (const IrOpening& o : w.openings)
                if (o.s0 < -1e-4 || o.s1 > len + 1e-4) rr.openings_ok = false;
        }
    return rr;
}

TEST(BuildingPlanAcceptance, ThreeProgramsHundredSeeds) {
    const char* names[3] = {"house", "bakery", "tavern"};
    const int kSeeds = 100;
    int total_failed = 0;
    for (const char* name : names) {
        Program program;
        std::string err;
        ASSERT_TRUE(load_program_file(program_path(name), program, err)) << err;

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

        int fails = 0, tiling = 0, access = 0, daylight = 0, openings_ok = 0;
        double soft_sum = 0.0, sec_sum = 0.0;
        long moves_sum = 0;
        std::vector<double> softs;
        std::string first_error;
        for (size_t i = 0; i < results.size(); ++i) {
            const RunResult& rr = results[i];
            if (!rr.ok) {
                ++fails;
                if (first_error.empty())
                    first_error = "seed " + std::to_string(i + 1) + ": " + rr.error;
                continue;
            }
            if (rr.hard != 0) {
                ++fails;
                if (first_error.empty())
                    first_error = "seed " + std::to_string(i + 1) + ": hard = " +
                                  std::to_string(rr.hard);
            }
            tiling += rr.tiling;
            access += rr.access;
            daylight += rr.daylight;
            openings_ok += rr.openings_ok;
            soft_sum += rr.soft;
            sec_sum += rr.seconds;
            moves_sum += rr.moves;
            softs.push_back(rr.soft);
        }
        std::sort(softs.begin(), softs.end());
        const double soft_mean = softs.empty() ? 0.0 : soft_sum / softs.size();
        const double soft_p95 = softs.empty() ? 0.0 : softs[static_cast<size_t>(
                                  std::min<size_t>(softs.size() - 1, static_cast<size_t>(softs.size() * 0.95)))];
        const double moves_per_s = sec_sum > 0.0 ? moves_sum / sec_sum : 0.0;
        std::printf(
            "[acceptance] %s: %d/%d ok, I1 %d%%, I3 %d%%, I6 %d%%, I7 %d%%; "
            "soft mean %.3f p95 %.3f; %.0f moves/s (mean %.2f s)\n",
            name, kSeeds - fails, kSeeds, tiling, access, daylight, openings_ok, soft_mean,
            soft_p95, moves_per_s, sec_sum / kSeeds);

        // The §16 gate: hard invariants at 100%.
        EXPECT_EQ(fails, 0) << name << ": " << first_error;
        EXPECT_EQ(tiling, kSeeds);
        EXPECT_EQ(access, kSeeds);
        EXPECT_EQ(daylight, kSeeds);
        EXPECT_EQ(openings_ok, kSeeds);
        // Soft metrics: generous regression thresholds (tuning belongs to the
        // metrics review, algorithms §14).
        EXPECT_LT(soft_mean, 0.6) << name;
        EXPECT_LT(soft_p95, 1.0) << name;
        // N2 extrapolation guard: well under the 5 s/house Debug budget.
        EXPECT_GT(moves_per_s, 3000.0);
        total_failed += fails;
    }
    ASSERT_EQ(total_failed, 0);
}

}  // namespace
}  // namespace building_topology_generator
