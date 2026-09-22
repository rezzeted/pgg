// A2 (docs/pgg/architecture_library_plan.md): the lib/arch layer — kinds, plan
// (six builders, @edge_len/@edge_yaw/@convex annotations, inside, corners),
// facade (facades → bands → tiles, facade_tree, style, walls with openings),
// terminals_basic. Corpus runs with golden fingerprints plus exact per-element
// attribute checks: vertex annotations, the shared window axis across floors
// (one pattern on every band), the door override and wall holes under slots.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/geometry.h"
#include "goldens_utils.h"
#include "test_utils.h"

namespace {

using pggtest::expectF32Near;
using pggtest::expectNoErrors;
using pggtest::expectVec3Near;
using pggtest::geoContentHash;
using pggtest::geoOutput;

std::string corpusPath(const char* name) { return std::string(PGG_CORPUS_DIR) + "/" + name; }

pgg::RunParams archParams() {
    pgg::RunParams p;
    p.importRoots.push_back(PGG_RESOURCES_DIR);
    return p;
}

const std::vector<float>* f32Col(const pgg::Geo& g, const char* name) {
    if (!g.pointAttrs) return nullptr;
    const pgg::AttrColumn* c = g.pointAttrs->find(name);
    if (!c) return nullptr;
    const auto* buf = std::get_if<std::shared_ptr<const std::vector<float>>>(&c->data);
    return buf ? buf->get() : nullptr;
}

const std::vector<int64_t>* intCol(const pgg::Geo& g, const char* name) {
    if (!g.pointAttrs) return nullptr;
    const pgg::AttrColumn* c = g.pointAttrs->find(name);
    if (!c) return nullptr;
    const auto* buf = std::get_if<std::shared_ptr<const std::vector<int64_t>>>(&c->data);
    return buf ? buf->get() : nullptr;
}

const std::vector<uint8_t>* boolCol(const pgg::Geo& g, const char* name) {
    if (!g.pointAttrs) return nullptr;
    const pgg::AttrColumn* c = g.pointAttrs->find(name);
    if (!c) return nullptr;
    const auto* buf = std::get_if<std::shared_ptr<const std::vector<uint8_t>>>(&c->data);
    return buf ? buf->get() : nullptr;
}

void expectCol(const std::vector<uint8_t>* col, std::initializer_list<int64_t> vs) {
    ASSERT_TRUE(col != nullptr);
    ASSERT_EQ(col->size(), vs.size());
    size_t i = 0;
    for (int64_t v : vs) EXPECT_EQ((*col)[i++], static_cast<uint8_t>(v));
}

void expectCol(const std::vector<float>* col, std::initializer_list<float> vs) {
    ASSERT_TRUE(col != nullptr);
    ASSERT_EQ(col->size(), vs.size());
    size_t i = 0;
    for (float v : vs) expectF32Near((*col)[i++], v, 1e-3f);
}

void expectCol(const std::vector<int64_t>* col, std::initializer_list<int64_t> vs) {
    ASSERT_TRUE(col != nullptr);
    ASSERT_EQ(col->size(), vs.size());
    size_t i = 0;
    for (int64_t v : vs) EXPECT_EQ((*col)[i++], v);
}

const glm::vec3* findPoint(const pgg::Geo& g, float x, float y, float z, float eps = 1e-3f) {
    for (const glm::vec3& p : *g.positions)
        if (std::abs(p.x - x) < eps && std::abs(p.y - y) < eps && std::abs(p.z - z) < eps) return &p;
    return nullptr;
}

TEST(ArchLib, PlanAnnotations) {
    pgg::RunResult r = pgg::runFile(corpusPath("arch_plan.pgg"), archParams());
    expectNoErrors(r);
    pggtest::expectGolden("arch_plan", r);

    // rect 8x5: four convex corners, outward azimuths -90/0/90/180.
    pgg::GeoPtr rect = geoOutput(r, "rect");
    ASSERT_TRUE(rect != nullptr);
    ASSERT_EQ(rect->pointCount(), 4u);
    expectCol(f32Col(*rect, "edge_len"), {5.0f, 8.0f, 5.0f, 8.0f});
    expectCol(f32Col(*rect, "edge_yaw"), {-90.0f, 0.0f, 90.0f, 180.0f});
    expectCol(boolCol(*rect, "convex"), {1, 1, 1, 1});

    // chamfered rect: five convex corners, the diagonal edge at 45 deg, len c*sqrt(2).
    pgg::GeoPtr chamfer = geoOutput(r, "chamfer");
    ASSERT_TRUE(chamfer != nullptr);
    ASSERT_EQ(chamfer->pointCount(), 5u);
    expectCol(f32Col(*chamfer, "edge_len"), {15.0f, 16.2f, 2.8f * 1.41421356f, 12.2f, 19.0f});
    expectCol(f32Col(*chamfer, "edge_yaw"), {-90.0f, 0.0f, 45.0f, 90.0f, 180.0f});
    expectCol(boolCol(*chamfer, "convex"), {1, 1, 1, 1, 1});

    // L-plan: exactly the reentrant corner is concave (index 3).
    pgg::GeoPtr ell = geoOutput(r, "ell");
    ASSERT_TRUE(ell != nullptr);
    expectCol(f32Col(*ell, "edge_len"), {6.5f, 6.0f, 2.5f, 3.0f, 4.0f, 9.0f});
    expectCol(f32Col(*ell, "edge_yaw"), {-90.0f, 0.0f, 90.0f, 0.0f, 90.0f, 180.0f});
    expectCol(boolCol(*ell, "convex"), {1, 1, 1, 0, 1, 1});

    // U-plan: the two inner notch corners are concave (indices 3, 4).
    pgg::GeoPtr yu = geoOutput(r, "yu");
    ASSERT_TRUE(yu != nullptr);
    ASSERT_EQ(yu->pointCount(), 8u);
    expectCol(boolCol(*yu, "convex"), {1, 1, 1, 0, 0, 1, 1, 1});

    // T-plan: the two stem shoulders are concave (indices 2, 5).
    pgg::GeoPtr tee = geoOutput(r, "tee");
    ASSERT_TRUE(tee != nullptr);
    ASSERT_EQ(tee->pointCount(), 8u);
    expectCol(boolCol(*tee, "convex"), {1, 1, 0, 1, 1, 0, 1, 1});

    // cross: eight convex arm tips and four concave inner corners (1, 4, 7, 10).
    pgg::GeoPtr cross = geoOutput(r, "cross");
    ASSERT_TRUE(cross != nullptr);
    ASSERT_EQ(cross->pointCount(), 12u);
    expectCol(f32Col(*cross, "edge_len"), {3.5f, 4.0f, 3.0f, 4.0f, 3.5f, 4.0f, 3.5f, 4.0f, 3.0f, 4.0f, 3.5f, 4.0f});
    expectCol(boolCol(*cross, "convex"), {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1});

    // corners(): K_CORNER slots with the vertex convexity and facade index copied.
    pgg::GeoPtr corners = geoOutput(r, "corners_l");
    ASSERT_TRUE(corners != nullptr);
    ASSERT_EQ(corners->pointCount(), 6u);
    expectCol(intCol(*corners, "kind"), {3, 3, 3, 3, 3, 3});
    expectCol(boolCol(*corners, "convex"), {1, 1, 1, 0, 1, 1});
    expectCol(intCol(*corners, "facade"), {0, 1, 2, 3, 4, 5});
    expectCol(f32Col(*corners, "h"), {7.2f, 7.2f, 7.2f, 7.2f, 7.2f, 7.2f});

    // inside(): center of the L in, the cut corner out, far point out;
    // cross arm in, cross notch out.
    pgg::GeoPtr flags = geoOutput(r, "flags");
    ASSERT_TRUE(flags != nullptr);
    expectCol(boolCol(*flags, "in_center"), {1});
    expectCol(boolCol(*flags, "in_cut"), {0});
    expectCol(boolCol(*flags, "in_far"), {0});
    expectCol(boolCol(*flags, "in_cross_arm"), {1});
    expectCol(boolCol(*flags, "in_cross_notch"), {0});
}

TEST(ArchLib, FacadeTreeSharedAxesAndOverride) {
    pgg::RunResult r = pgg::runFile(corpusPath("arch_facade.pgg"), archParams());
    expectNoErrors(r);
    pggtest::expectGolden("arch_facade", r);

    // 6 plan edges x 4 bands (plinth | floor | floor | crown).
    pgg::GeoPtr bands = geoOutput(r, "band_scopes");
    ASSERT_TRUE(bands != nullptr);
    ASSERT_EQ(bands->pointCount(), 24u);

    // One pattern on both floor bands: the window slots of the two floors share
    // the (x, z) axis per facade; floor1 windows sit at y = 1.75 (plinth 1.2 +
    // sill 0.55), floor2 at y = 5.35 (+ floor_h 3.6); slot height is style win_h.
    pgg::GeoPtr slots = geoOutput(r, "slots");
    ASSERT_TRUE(slots != nullptr);
    ASSERT_EQ(slots->pointCount(), 36u);
    expectCol(intCol(*slots, "facade"), {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5});
    expectCol(intCol(*slots, "floor"), {1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2});
    static const float kAxisXZ[6][2] = {
        {-4.5f, 0.0f}, {-1.5f, 3.25f}, {1.5f, 2.0f}, {3.0f, 0.75f}, {4.5f, -1.25f}, {0.0f, -3.25f},
    };
    for (int f = 0; f < 6; ++f) {
        const glm::vec3* w1 = findPoint(*slots, kAxisXZ[f][0], 1.75f, kAxisXZ[f][1]);
        const glm::vec3* w2 = findPoint(*slots, kAxisXZ[f][0], 5.35f, kAxisXZ[f][1]);
        ASSERT_TRUE(w1 != nullptr) << "floor1 window missing on facade " << f;
        ASSERT_TRUE(w2 != nullptr) << "floor2 window missing on facade " << f;
    }

    // The door override rewrote exactly the floor1 window of the front facade.
    pgg::GeoPtr doors = geoOutput(r, "doors");
    ASSERT_TRUE(doors != nullptr);
    ASSERT_EQ(doors->pointCount(), 1u);
    expectVec3Near((*doors->positions)[0], glm::vec3(-1.5f, 1.75f, 3.25f));
    pgg::GeoPtr wins = geoOutput(r, "wins");
    ASSERT_TRUE(wins != nullptr);
    EXPECT_EQ(wins->pointCount(), 11u);
    expectCol(f32Col(*wins, "h"), {2.2f, 2.2f, 2.2f, 2.2f, 2.2f, 2.2f, 2.2f, 2.2f, 2.2f, 2.2f, 2.2f});

    // walls(): a core+mortar wall per facade with a rectangular hole under the
    // window slot — no vertex inside the (shrunk) opening box.
    pgg::GeoPtr walls = geoOutput(r, "walls_s");
    ASSERT_TRUE(walls != nullptr);
    ASSERT_TRUE(walls->faceGroups != nullptr);
    EXPECT_TRUE(walls->faceGroups->find("brick") != nullptr);
    EXPECT_TRUE(walls->faceGroups->find("mortar") != nullptr);
    for (const glm::vec3& p : *walls->positions) {
        EXPECT_FALSE(std::abs(p.x) < 0.4f && p.y > 1.05f && p.y < 1.95f && p.z > 0.95f)
            << "vertex inside the window opening: " << p.x << " " << p.y << " " << p.z;
    }
}

TEST(ArchLib, TerminalsBasic) {
    pgg::RunResult r = pgg::runFile(corpusPath("arch_terminals.pgg"), archParams());
    expectNoErrors(r);
    pggtest::expectGolden("arch_terminals", r);

    pgg::GeoPtr row = geoOutput(r, "row");
    ASSERT_TRUE(row != nullptr);
    ASSERT_TRUE(row->faceGroups != nullptr);
    for (const char* grp : {"glass", "trim", "iron", "wood", "tile", "roof"})
        EXPECT_TRUE(row->faceGroups->find(grp) != nullptr) << "group missing: " << grp;
}

// A5 (§6.6): elements on edges and slopes. band_edges gives one cornice terminal
// serving every floor band of both L-plan masses through pick_role; the gutter
// follows every R_EAVE of the L-roof; the chimney top clears the highest ridge by
// the 0.5 m rule; dormer_scope anchors a slot on a slope panel; edge_ends dedups
// finial anchors at shared skeleton vertices.
TEST(ArchLib, ElementsEdgeRules) {
    pgg::RunResult r = pgg::runFile(corpusPath("arch_elements.pgg"), archParams());
    expectNoErrors(r);
    pggtest::expectGolden("arch_elements", r);

    // band_edges: 6 facades x 4 bands (plinth|floor|floor|crown) x 2 roles.
    pgg::GeoPtr bed = geoOutput(r, "bed");
    ASSERT_TRUE(bed != nullptr);
    ASSERT_EQ(bed->pointCount(), 48u);
    pgg::GeoPtr tops = geoOutput(r, "tops");
    ASSERT_TRUE(tops != nullptr);
    ASSERT_EQ(tops->pointCount(), 24u);
    expectCol(intCol(*tops, "role"), {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5});
    expectCol(intCol(*tops, "floor"), {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3});
    expectCol(f32Col(*tops, "y1"), {1.2f, 4.8f, 8.4f, 9.0f, 1.2f, 4.8f, 8.4f, 9.0f, 1.2f, 4.8f, 8.4f, 9.0f,
                                    1.2f, 4.8f, 8.4f, 9.0f, 1.2f, 4.8f, 8.4f, 9.0f, 1.2f, 4.8f, 8.4f, 9.0f});
    expectCol(f32Col(*tops, "len"), {9.0f, 9.0f, 9.0f, 9.0f, 6.0f, 6.0f, 6.0f, 6.0f, 2.5f, 2.5f, 2.5f, 2.5f,
                                     3.0f, 3.0f, 3.0f, 3.0f, 6.5f, 6.5f, 6.5f, 6.5f, 9.0f, 9.0f, 9.0f, 9.0f});
    pgg::GeoPtr bots = geoOutput(r, "bots");
    ASSERT_TRUE(bots != nullptr);
    ASSERT_EQ(bots->pointCount(), 24u);
    expectCol(intCol(*bots, "role"), {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6});

    // The gutter follows every eave of the L-roof (wing A front/back 9.8, wing B 3.3);
    // eaves are horizontal (tilt 0), gutter troughs hang below the eave line (y <= 9).
    pgg::GeoPtr eaves = geoOutput(r, "eaves");
    ASSERT_TRUE(eaves != nullptr);
    ASSERT_EQ(eaves->pointCount(), 4u);
    expectCol(f32Col(*eaves, "len"), {9.8f, 9.8f, 3.3f, 3.3f});
    expectCol(f32Col(*eaves, "tilt"), {0.0f, 0.0f, 0.0f, 0.0f});

    // Chimney rule: ridge A at 12.65, ridge B at 12.4 — the stack top clears the
    // highest ridge + 0.5 (13.15; +0.06 flue lip above the cap).
    pgg::GeoPtr ridges = geoOutput(r, "ridges");
    ASSERT_TRUE(ridges != nullptr);
    ASSERT_EQ(ridges->pointCount(), 2u);
    expectCol(f32Col(*ridges, "len"), {9.8f, 5.9f});
    expectCol(f32Col(*ridges, "y1"), {12.65f, 12.4f});
    pgg::GeoPtr chim = geoOutput(r, "chim");
    ASSERT_TRUE(chim != nullptr);
    float chim_top = 0.0f;
    for (const glm::vec3& p : *chim->positions) chim_top = std::max(chim_top, p.y);
    expectF32Near(chim_top, 13.21f, 1e-2f);
    EXPECT_GE(chim_top, 12.65f + 0.5f - 1e-3f);

    // dormer_scope: one K_DORMER slot on slope island 1, between eave and ridge.
    pgg::GeoPtr ds = geoOutput(r, "ds");
    ASSERT_TRUE(ds != nullptr);
    ASSERT_EQ(ds->pointCount(), 1u);
    expectCol(intCol(*ds, "kind"), {8});
    expectCol(intCol(*ds, "facade"), {1});
    const float ds_y = (*ds->positions)[0].y;
    EXPECT_GT(ds_y, 9.0f);
    EXPECT_LT(ds_y, 12.65f);
    pgg::GeoPtr dorm = geoOutput(r, "dorm");
    ASSERT_TRUE(dorm != nullptr);
    EXPECT_GT(dorm->faceCount(), 0u);

    // edge_ends: four unique anchors at the ridge ends (two ridges, no dup at the
    // wing-B ridge abutment — it sits below ridge A, so no position match anyway).
    pgg::GeoPtr ends = geoOutput(r, "ends");
    ASSERT_TRUE(ends != nullptr);
    ASSERT_EQ(ends->pointCount(), 4u);
    EXPECT_TRUE(findPoint(*ends, 4.9f, 12.65f, -1.25f) != nullptr);
    EXPECT_TRUE(findPoint(*ends, -1.5f, 12.4f, 4.9f) != nullptr);

    // The assembled house covers every element group.
    pgg::GeoPtr house = geoOutput(r, "house");
    ASSERT_TRUE(house != nullptr);
    ASSERT_TRUE(house->faceGroups != nullptr);
    for (const char* grp : {"brick", "glass", "iron", "roof", "tile", "trim"})
        EXPECT_TRUE(house->faceGroups->find(grp) != nullptr) << "group missing: " << grp;
}

TEST(ArchLib, RoofRectKinds) {
    pgg::RunResult r = pgg::runFile(corpusPath("arch_roof.pgg"), archParams());
    expectNoErrors(r);
    pggtest::expectGolden("arch_roof", r);

    // hip: 4 slope panels (2 trapezoids + 2 triangles), ridge edge of 3.0 at y=5.9.
    pgg::GeoPtr hip_s = geoOutput(r, "hip_s");
    ASSERT_TRUE(hip_s != nullptr);
    EXPECT_EQ(hip_s->faceCount(), 4u);
    pgg::GeoPtr hip_e = geoOutput(r, "hip_e");
    ASSERT_TRUE(hip_e != nullptr);
    EXPECT_EQ(hip_e->pointCount(), 9u);
    pgg::GeoPtr ridges = geoOutput(r, "ridges");
    ASSERT_TRUE(ridges != nullptr);
    ASSERT_EQ(ridges->pointCount(), 1u);
    expectCol(f32Col(*ridges, "len"), {3.0f});

    // gable: 2 panels, 5 edges (ridge + 2 eaves + 2 rakes).
    pgg::GeoPtr gable_s = geoOutput(r, "gable_s");
    ASSERT_TRUE(gable_s != nullptr);
    EXPECT_EQ(gable_s->faceCount(), 2u);
    pgg::GeoPtr gable_e = geoOutput(r, "gable_e");
    ASSERT_TRUE(gable_e != nullptr);
    EXPECT_EQ(gable_e->pointCount(), 5u);

    // mansard: 4 lower + 4 upper panels.
    pgg::GeoPtr man_s = geoOutput(r, "man_s");
    ASSERT_TRUE(man_s != nullptr);
    EXPECT_EQ(man_s->faceCount(), 8u);

    // roof_l cross: wing A slopes clipped at x=1.8 (wing B eave), one valley edge
    // of len 3.3*sqrt(3) from the overhang corner to wing B's ridge. The edge model
    // carries both wings' eaves/rakes (wing B ridge replaced by the visible stub):
    // wing A (ridge+2 eaves+2 rakes) + wing B without its full ridge (4) + stub + valley.
    pgg::GeoPtr lc_s = geoOutput(r, "lc_s");
    ASSERT_TRUE(lc_s != nullptr);
    EXPECT_EQ(lc_s->faceCount(), 12u);
    pgg::GeoPtr lc_e = geoOutput(r, "lc_e");
    ASSERT_TRUE(lc_e != nullptr);
    ASSERT_EQ(lc_e->pointCount(), 11u);
    pgg::GeoPtr lc_valley = geoOutput(r, "lc_valley");
    ASSERT_TRUE(lc_valley != nullptr);
    ASSERT_EQ(lc_valley->pointCount(), 1u);
    expectCol(f32Col(*lc_valley, "len"), {3.3f * 1.7320508f});

    // roof_cross: 4 valleys, slopes clipped to 8 visible panels; edges = both wings
    // without their full ridges (4+4) + 3 visible ridge runs + 4 valleys.
    pgg::GeoPtr cr_s = geoOutput(r, "cr_s");
    ASSERT_TRUE(cr_s != nullptr);
    EXPECT_EQ(cr_s->faceCount(), 8u);
    pgg::GeoPtr cr_e = geoOutput(r, "cr_e");
    ASSERT_TRUE(cr_e != nullptr);
    EXPECT_EQ(cr_e->pointCount(), 15u);
    pgg::GeoPtr valleys = geoOutput(r, "valleys");
    ASSERT_TRUE(valleys != nullptr);
    EXPECT_EQ(valleys->pointCount(), 4u);

    // anchors cover all 4 hip panels: exact count, all stamped with variant 0 and
    // oriented frames (orient quaternion present).
    pgg::GeoPtr anchors = geoOutput(r, "anchors");
    ASSERT_TRUE(anchors != nullptr);
    ASSERT_EQ(anchors->pointCount(), 739u);
    const std::vector<int64_t>* vars = intCol(*anchors, "variant");
    ASSERT_TRUE(vars != nullptr);
    for (int64_t v : *vars) EXPECT_EQ(v, 0);
}

}  // namespace
