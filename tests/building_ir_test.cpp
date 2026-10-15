// Building IR (building-topology-generator-ir/0) round-trip and validation tests.
// The frozen B0 house (content/building/frozen/house_b0.ir.json) is the corpus.

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "codes.h"
#include "ir.h"
#include "roof.h"

namespace building_topology_generator {
namespace {

std::string frozen_path() {
    return std::string(BUILDING_FROZEN_DIR) + "/house_b0.ir.json";
}

bool load_frozen(Ir& ir, std::string& err) {
    return load_ir_file(frozen_path(), ir, err);
}

TEST(BuildingIr, FrozenLoads) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    EXPECT_EQ(ir.program, "frozen_house_b0");
    ASSERT_EQ(ir.storeys.size(), 3u);
    EXPECT_EQ(ir.storeys[0].id, "ground");
    EXPECT_DOUBLE_EQ(ir.storeys[1].y, 3.05);
    EXPECT_DOUBLE_EQ(ir.storeys[2].y, 5.90);
    EXPECT_EQ(ir.spaces.size(), 12u);
    EXPECT_EQ(ir.stairs.size(), 2u);
    ASSERT_EQ(ir.chimneys.size(), 1u);
    EXPECT_EQ(ir.chimneys[0].hearths.size(), 2u);
    EXPECT_EQ(ir.roof.planes.size(), 2u);
    EXPECT_EQ(ir.roof.ridges.size(), 1u);
}

TEST(BuildingIr, RoundTripDeterministic) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    sort_ir(ir);
    const std::string text1 = write_ir_json(ir);
    Ir ir2;
    ASSERT_TRUE(read_ir_json(text1, ir2, err)) << err;
    const std::string text2 = write_ir_json(ir2);
    EXPECT_EQ(text1, text2);
}

TEST(BuildingIr, RejectsForeignFormatWithHint) {
    std::string text = R"({"format": "dungeon-geometry-generator-ir/2", "storeys": [], "spaces": [], "walls": []})";
    Ir ir;
    std::string err;
    EXPECT_FALSE(read_ir_json(text, ir, err));
    EXPECT_NE(err.find("building-topology-generator-ir/0"), std::string::npos) << err;
}

TEST(BuildingIr, RejectsCodeNameMismatch) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    std::string text = write_ir_json(ir);
    // flip one coded field: class_code of the first wall disagrees with "class"
    const std::string needle = "\"class_code\": 1";
    const size_t at = text.find(needle);
    ASSERT_NE(at, std::string::npos);
    text.replace(at, needle.size(), "\"class_code\": 3");
    EXPECT_FALSE(read_ir_json(text, ir, err));
    EXPECT_NE(err.find("disagrees"), std::string::npos) << err;
}

TEST(BuildingIr, WriteQuantizesTo1e4) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    ir.walls[0].h = 2.81234567;
    const std::string text = write_ir_json(ir);
    EXPECT_NE(text.find("2.8123"), std::string::npos);
    EXPECT_EQ(text.find("2.81234567"), std::string::npos);
}

// --- frozen house invariants (the cheap half of §14.2, IR level) ---

double outline_area(const std::vector<glm::dvec2>& ring) {
    double a = 0.0;
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::dvec2& p = ring[i];
        const glm::dvec2& q = ring[(i + 1) % ring.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

TEST(BuildingIrFrozen, OutlinesCcwFromAbove) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    for (const IrVolume& v : ir.volumes)
        EXPECT_LT(outline_area(v.outline), 0.0) << v.id << ": volume outline must be CCW from +Y";
    for (const IrSpace& s : ir.spaces)
        EXPECT_LT(outline_area(s.outline), 0.0) << s.id << ": space outline must be CCW from +Y";
}

TEST(BuildingIrFrozen, OpeningsInsideWallSpans) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    for (const IrWall& w : ir.walls) {
        const double len = glm::length(w.b - w.a);
        for (const IrOpening& o : w.openings) {
            EXPECT_GT(o.s0, 0.05) << o.id << ": opening touches the wall start";
            EXPECT_LT(o.s1, len - 0.05) << o.id << ": opening touches the wall end";
            EXPECT_GT(o.s1 - o.s0, 0.5) << o.id << ": opening too narrow";
            EXPECT_LT(o.y1, 2.6) << o.id << ": opening head above the storey band";
            if (o.kind_code == kOpeningDoor) {
                EXPECT_GE(o.s1 - o.s0, 0.8) << o.id << ": door clear width < 0.8 (I3)";
                EXPECT_DOUBLE_EQ(o.y0, 0.0) << o.id << ": door not on the floor";
            }
        }
    }
}

TEST(BuildingIrFrozen, SlabRectsClearOfWellsAndChimney) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    ASSERT_EQ(ir.slabs.size(), 2u);
    auto overlaps = [](const IrSlabRect& a, const IrSlabRect& b) {
        return a.x0 < b.x1 - 1e-9 && b.x0 < a.x1 - 1e-9 && a.z0 < b.z1 - 1e-9 && b.z0 < a.z1 - 1e-9;
    };
    for (size_t si = 0; si < ir.slabs.size(); ++si) {
        const IrSlab& slab = ir.slabs[si];
        const IrStair& stair = ir.stairs[si];
        for (const IrSlabRect& r : slab.rects) {
            for (const IrSlabRect& w : stair.well)
                EXPECT_FALSE(overlaps(r, w)) << stair.id << ": slab rect [" << r.x0 << "," << r.z0 << "]-["
                                             << r.x1 << "," << r.z1 << "] covers the stair well";
            const IrSlabRect ch{ -0.5, 0.0, 0.5, 0.5 };
            EXPECT_FALSE(overlaps(r, ch)) << "slab rect covers the chimney footprint";
        }
    }
}

TEST(BuildingIrFrozen, StairNorms) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    for (const IrStair& s : ir.stairs) {
        const double blondel = 2.0 * s.rise + s.going;
        EXPECT_GT(blondel, 0.57) << s.id;
        EXPECT_LT(blondel, 0.66) << s.id;
        EXPECT_GE(s.width, 0.8) << s.id << ": march width < 0.8 (I3)";
        EXPECT_EQ(s.risers_low + 7, s.risers);  // this house: 9 + 7
        // total rise matches the storey delta within quantization slack
        double dy = 0.0;
        for (size_t i = 0; i + 1 < ir.storeys.size(); ++i) {
            if (ir.storeys[i].id == s.from && ir.storeys[i + 1].id == s.to)
                dy = ir.storeys[i + 1].y - ir.storeys[i].y;
        }
        EXPECT_NEAR(s.risers * s.rise, dy, 1e-3) << s.id;
    }
}

TEST(BuildingIrFrozen, AccessGraphCoversHabitableSpaces) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    // BFS from "outside": every space must be reachable (I1).
    std::map<std::string, std::vector<std::string>> adj;
    for (const IrAccessEdge& e : ir.access.edges) {
        adj[e.a].push_back(e.b);
        adj[e.b].push_back(e.a);
    }
    std::set<std::string> seen{"outside"};
    std::vector<std::string> queue{"outside"};
    while (!queue.empty()) {
        const std::string cur = queue.back();
        queue.pop_back();
        for (const std::string& nxt : adj[cur])
            if (seen.insert(nxt).second) queue.push_back(nxt);
    }
    for (const IrSpace& s : ir.spaces) EXPECT_TRUE(seen.count(s.id)) << s.id << " unreachable (I1)";
}

TEST(BuildingIrFrozen, RoofDerivedMatchesModel) {
    Ir ir;
    std::string err;
    ASSERT_TRUE(load_frozen(ir, err)) << err;
    ASSERT_EQ(ir.volumes.size(), 1u);
    RoofModel m;
    ASSERT_TRUE(build_roof_model(ir.volumes[0], m, err)) << err;
    ASSERT_EQ(m.planes.size(), ir.roof.planes.size());
    for (size_t i = 0; i < m.planes.size(); ++i) {
        // match by source edge, not by index (skeleton face order is internal)
        const IrRoofPlane* ref = nullptr;
        for (const IrRoofPlane& p : ir.roof.planes)
            if (p.edge == m.planes[i].edge) ref = &p;
        ASSERT_TRUE(ref) << "no stored plane for edge " << m.planes[i].edge;
        EXPECT_NEAR(ref->o.y, m.planes[i].o.y, 1e-3);
        EXPECT_NEAR(glm::length(ref->n - m.planes[i].n), 0.0, 1e-3);
    }
    // ridge endpoints and chimney rule
    ASSERT_EQ(m.ridges.size(), 1u);
    EXPECT_NEAR(m.ridges[0].a.y, ir.roof.ridges[0].a.y, 1e-3);
    const IrChimney& ch = ir.chimneys[0];
    double dist = 0.0, ridge_y = 0.0;
    ASSERT_TRUE(nearest_ridge(m, ch.top.x, ch.top.z, dist, ridge_y));
    double exit_y = 0.0;
    ASSERT_TRUE(roof_y_at(m, ch.top.x, ch.top.z, exit_y));
    const double req = chimney_top_required(dist, ridge_y, exit_y, ir.params.h_above_roof);
    EXPECT_GE(ch.top.y, req - 1e-4) << "chimney top below the §5.7 rule";
    EXPECT_LE(ch.top.y, req + 0.2) << "chimney top overshoots the §5.7 minimum";
}

}  // namespace
}  // namespace building_topology_generator
