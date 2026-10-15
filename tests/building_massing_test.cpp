// BuildingGenerator B1 / G2: massing scan (F2 — the chosen massing fits the
// program within its ranges and stays inside the plot; the report explains
// the choice), pins (§4.5) and the massing IR (storeys, volumes, roof).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "ir.h"
#include "massing.h"
#include "pin.h"
#include "plan_svg.h"
#include "program.h"

namespace {

namespace bt = building_topology_generator;

bt::ExpandedProgram loadProgram(const std::string& name) {
    bt::Program p;
    std::string err;
    if (!bt::load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/" + name, p, err))
        ADD_FAILURE() << err;
    return bt::expand_program(p);
}

bt::MassingParams defaultParams() { return bt::MassingParams{}; }

// The candidate volumes are built in a local frame and centered only in
// massing_to_ir; the site check above uses half-extents, so run it on the
// union extents instead of absolute coordinates.
void expectFitsSiteExtents(const bt::MassingCandidate& c, const bt::Site& site,
                           const bt::MassingParams& params) {
    double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;
    for (const bt::MassingVolume& v : c.volumes)
        for (const glm::dvec2& p : v.outline) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            z0 = std::min(z0, p.y);
            z1 = std::max(z1, p.y);
        }
    EXPECT_LE(x1 - x0 + 2 * params.overhang, site.w - 2 * site.setback + 1e-6);
    EXPECT_LE(z1 - z0 + 2 * params.overhang, site.d - 2 * site.setback + 1e-6);
}

TEST(BuildingMassing, ThreeProgramsFivePlots) {
    const char* programs[] = {"house.json", "bakery.json", "tavern.json"};
    const bt::Site plots[] = {{10, 10, 0, 3}, {12, 10, 0, 3}, {16, 14, 0, 3},
                              {20, 14, 1, 3}, {24, 16, 2, 3}};
    const bt::MassingParams params = defaultParams();
    for (const char* name : programs)
        for (const bt::Site& site : plots) {
            SCOPED_TRACE(std::string(name) + " on " + std::to_string(site.w) + "x" +
                         std::to_string(site.d));
            const bt::ExpandedProgram prog = loadProgram(name);
            const bt::MassingResult r = bt::generate_massing(prog, site, params, 1);
            ASSERT_TRUE(r.ok) << r.error;
            // F2: the chosen massing fits the program — every required room is
            // placed (candidates that fail that are rejected outright), so the
            // used area covers the required area. The fit term only penalizes
            // a shortfall of the 12% circulation margin (algorithms §3): on
            // tight plots the best candidate may shave it, but the shortfall
            // stays sub-meter (worst here: tavern on 10x10, 0.56 m2 -> 0.34).
            EXPECT_LT(r.chosen.score.fit, 1.0) << r.report;
            double used = 0.0;
            for (double u : r.chosen.used_area) used += u;
            EXPECT_GE(used, prog.required_area() - 1e-6) << r.report;
            // ... and lies in the plot (I8, with the overhang).
            expectFitsSiteExtents(r.chosen, site, params);
            // The report explains the choice.
            EXPECT_NE(r.report.find("chosen:"), std::string::npos);
            EXPECT_NE(r.report.find("top "), std::string::npos);
            EXPECT_FALSE(r.top.empty());
        }
}

TEST(BuildingMassing, NoFitErrorHasNumbers) {
    const bt::ExpandedProgram prog = loadProgram("tavern.json");
    const bt::MassingResult r =
        bt::generate_massing(prog, bt::Site{6, 6, 0, 1}, defaultParams(), 1);
    ASSERT_FALSE(r.ok);
    EXPECT_NE(r.error.find("does not fit"), std::string::npos);
    EXPECT_NE(r.error.find("m2"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("loosen"), std::string::npos) << r.error;
}

TEST(BuildingMassing, DeterministicIr) {
    const bt::ExpandedProgram prog = loadProgram("house.json");
    const bt::Site site{16, 14, 0, 3};
    const bt::MassingParams params = defaultParams();
    std::string a, b;
    for (std::string* out : {&a, &b}) {
        const bt::MassingResult r = bt::generate_massing(prog, site, params, 42);
        ASSERT_TRUE(r.ok);
        bt::Ir ir;
        std::string err;
        ASSERT_TRUE(bt::massing_to_ir(r.chosen, prog, params, 42, ir, err)) << err;
        *out = bt::write_ir_json(ir);
    }
    EXPECT_EQ(a, b);
}

TEST(BuildingMassing, IrHasStoreysVolumesRoof) {
    const bt::ExpandedProgram prog = loadProgram("house.json");
    const bt::MassingResult r = bt::generate_massing(prog, bt::Site{16, 14, 0, 3},
                                                     defaultParams(), 7);
    ASSERT_TRUE(r.ok);
    bt::Ir ir;
    std::string err;
    ASSERT_TRUE(bt::massing_to_ir(r.chosen, prog, defaultParams(), 7, ir, err)) << err;
    ASSERT_FALSE(ir.storeys.empty());
    EXPECT_EQ(ir.storeys.front().id, "ground");
    EXPECT_DOUBLE_EQ(ir.storeys.front().y, 0.0);
    ASSERT_FALSE(ir.volumes.empty());
    EXPECT_FALSE(ir.roof.planes.empty()) << "derived roof planes";
    // The IR round-trips (massing-only: spaces/walls are empty arrays).
    bt::Ir back;
    ASSERT_TRUE(bt::read_ir_json(bt::write_ir_json(ir), back, err)) << err;
    EXPECT_EQ(back.volumes.size(), ir.volumes.size());
    EXPECT_EQ(back.storeys.size(), ir.storeys.size());
    EXPECT_EQ(back.roof.planes.size(), ir.roof.planes.size());
    // The origin is the footprint union center (§5.1).
    double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;
    for (const bt::IrVolume& v : ir.volumes)
        for (const glm::dvec2& p : v.outline) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            z0 = std::min(z0, p.y);
            z1 = std::max(z1, p.y);
        }
    EXPECT_NEAR((x0 + x1) / 2, 0.0, 1e-9);
    EXPECT_NEAR((z0 + z1) / 2, 0.0, 1e-9);
    // SVG plans exist for every storey.
    for (size_t k = 0; k < ir.storeys.size(); ++k) {
        const std::string svg =
            bt::storey_plan_svg(ir, static_cast<int>(k), bt::Site{16, 14, 0, 3});
        EXPECT_NE(svg.find("<polygon"), std::string::npos) << ir.storeys[k].id;
    }
}

TEST(BuildingMassing, PinRoundTrip) {
    const bt::ExpandedProgram prog = loadProgram("house.json");
    const bt::Site site{16, 14, 0, 3};
    const bt::MassingParams params = defaultParams();
    const bt::MassingResult r = bt::generate_massing(prog, site, params, 1);
    ASSERT_TRUE(r.ok);
    bt::Ir ir;
    std::string err;
    ASSERT_TRUE(bt::massing_to_ir(r.chosen, prog, params, 1, ir, err)) << err;

    bt::Pin pin;
    ASSERT_TRUE(bt::pin_from_ir(ir, "massing", pin, err)) << err;
    ASSERT_TRUE(pin.has_massing);
    const bt::MassingResult pinned =
        bt::massing_from_pin(prog, site, params, 1, pin.massing_volumes, "test");
    ASSERT_TRUE(pinned.ok) << pinned.error;
    // The pin is read from the IR (origin at the union bbox center), so compare
    // at the IR level: regenerating from the pin must reproduce the same IR.
    bt::Ir ir2;
    ASSERT_TRUE(bt::massing_to_ir(pinned.chosen, prog, params, 1, ir2, err)) << err;
    EXPECT_EQ(bt::write_ir_json(ir2), bt::write_ir_json(ir));
}

TEST(BuildingMassing, PinRejectsOffGridAndOffSite) {
    const bt::ExpandedProgram prog = loadProgram("house.json");
    const bt::Site site{16, 14, 0, 3};
    const bt::MassingParams params = defaultParams();

    // Off-grid edge (1.3 m is not a whole number of 0.5 m cells plus t_ext).
    {
        bt::MassingVolume v;
        v.outline = {{-4.0, -3.0}, {-4.0, 3.0}, {2.7, 3.0}, {2.7, -3.0}};  // 6.7 m side
        v.storeys = 2;
        v.roof_kind = bt::kRoofGable;
        v.pitch_by_edge = {90.0, 50.0, 90.0, 50.0};
        const bt::MassingResult r =
            bt::massing_from_pin(prog, site, params, 1, {v}, "test");
        ASSERT_FALSE(r.ok);
        EXPECT_NE(r.error.find("grid"), std::string::npos) << r.error;
    }
    // Off-site.
    {
        bt::MassingVolume v;
        v.outline = {{-8.0, -4.0}, {-8.0, 4.0}, {8.0, 4.0}, {8.0, -4.0}};  // 16 m > buildable
        v.storeys = 2;
        v.roof_kind = bt::kRoofGable;
        v.pitch_by_edge = {90.0, 50.0, 90.0, 50.0};
        const bt::MassingResult r =
            bt::massing_from_pin(prog, site, params, 1, {v}, "test");
        ASSERT_FALSE(r.ok);
        EXPECT_NE(r.error.find("does not fit the plot"), std::string::npos) << r.error;
    }
}

TEST(BuildingMassing, PinFileParses) {
    const std::string text = R"({
        "format": "building-pin/0",
        "massing": {"volumes": [
            {"outline": [[-4, -3], [-4, 3], [4, 3], [4, -3]],
             "storeys": 2, "attic": true,
             "roof": {"kind": "gable", "pitch": 50, "eave_rise": 0.8}}
        ]},
        "cores": [{"kind": "stair", "at": [1, 2]}]
    })";
    bt::Pin pin;
    std::string err;
    ASSERT_TRUE(bt::read_pin_json(text, pin, err)) << err;
    ASSERT_TRUE(pin.has_massing);
    EXPECT_TRUE(pin.has_cores);
    ASSERT_EQ(pin.massing_volumes.size(), 1u);
    EXPECT_EQ(pin.massing_volumes[0].pitch_by_edge,
              (std::vector<double>{90.0, 50.0, 90.0, 50.0}));
    // N7: a foreign format is rejected with a hint.
    EXPECT_FALSE(bt::read_pin_json("{\"format\": \"building-ir/0\"}", pin, err));
    EXPECT_NE(err.find(bt::kPinFormat), std::string::npos);
}

}  // namespace
