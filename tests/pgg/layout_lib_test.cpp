// A1 (docs/pgg/architecture_library_plan.md): the lib/layout protocol —
// scope/split/edges/record. Corpus runs with golden fingerprints plus exact
// per-element attribute checks (positions, sizes, kinds): the split semantics
// (fixed/flex/cycle/axis-Y/rotated parent), the fit ensure (E304), stage-merge
// schema compatibility (no E609) and run-to-run reproducibility (N1).
// (Named layout_lib_test.cpp to avoid clashing with layout_test.cpp — the E8
// node-graph layout suite.)
#include <gtest/gtest.h>

#include <algorithm>

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

// Corpus files import lib.layout.*: the fallback import root that PggTool adds
// implicitly has to be explicit here.
pgg::RunParams layoutParams() {
    pgg::RunParams p;
    p.importRoots.push_back(PGG_RESOURCES_DIR);
    return p;
}

// Column readers over the points domain (the layout models are points-only).
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

void expectXs(const pgg::Geo& g, std::initializer_list<float> xs) {
    ASSERT_EQ(g.pointCount(), xs.size());
    size_t i = 0;
    for (float x : xs) expectF32Near((*g.positions)[i++].x, x, 1e-4f);
}

void expectCol(const std::vector<float>* col, std::initializer_list<float> vs) {
    ASSERT_TRUE(col != nullptr);
    ASSERT_EQ(col->size(), vs.size());
    size_t i = 0;
    for (float v : vs) expectF32Near((*col)[i++], v, 1e-4f);
}

void expectCol(const std::vector<int64_t>* col, std::initializer_list<int64_t> vs) {
    ASSERT_TRUE(col != nullptr);
    ASSERT_EQ(col->size(), vs.size());
    size_t i = 0;
    for (int64_t v : vs) EXPECT_EQ((*col)[i++], v);
}

bool hasMessage(const pgg::RunResult& r, const std::string& code, const std::string& needle) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(needle) != std::string::npos) return true;
    return false;
}

TEST(LayoutLib, ScopesPickOverrideRecord) {
    pgg::RunResult r = pgg::runFile(corpusPath("layout_scopes.pgg"), layoutParams());
    expectNoErrors(r);
    pggtest::expectGolden("layout_scopes", r);

    // pick by kind: exactly the second scope survives.
    pgg::GeoPtr windows = geoOutput(r, "windows");
    ASSERT_TRUE(windows != nullptr);
    ASSERT_EQ(windows->pointCount(), 1u);
    expectVec3Near((*windows->positions)[0], glm::vec3(2.0f, 0.0f, 5.0f));

    // override_box by frame center: the scope at (0,0,0) is rewritten to 7/2,
    // the one at (2,0,5) keeps its 2/1.
    pgg::GeoPtr over = geoOutput(r, "over");
    ASSERT_TRUE(over != nullptr);
    expectCol(intCol(*over, "kind"), {7, 2});
    expectCol(intCol(*over, "variant"), {2, 1});

    // record: the consumer-written field reads back through value().
    pgg::GeoPtr rec = geoOutput(r, "rec");
    ASSERT_TRUE(rec != nullptr);
    expectCol(f32Col(*rec, "cornice_h"), {0.35f});
    expectCol(intCol(*rec, "type"), {1});

    // frame_w drove the marker width (1.0); place puts it into s1's frame
    // (yaw 90 at (2,0,5)): the 1.0 x-extent maps to z.
    pgg::GeoPtr scene = geoOutput(r, "scene");
    ASSERT_TRUE(scene != nullptr);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const glm::vec3& p : *scene->positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    expectVec3Near(lo, glm::vec3(1.9f, -0.1f, 4.5f), 1e-4f);
    expectVec3Near(hi, glm::vec3(2.1f, 0.1f, 5.5f), 1e-4f);
}

TEST(LayoutLib, SplitFixedFlexRepeatBandsRotated) {
    pgg::RunResult r = pgg::runFile(corpusPath("layout_split.pgg"), layoutParams());
    expectNoErrors(r);
    pggtest::expectGolden("layout_split", r);

    // fixed: sizes as given, centers packed from -L/2.
    pgg::GeoPtr fixed = geoOutput(r, "fixed");
    ASSERT_TRUE(fixed != nullptr);
    expectCol(f32Col(*fixed, "w"), {1.0f, 2.0f, 1.0f});
    expectCol(intCol(*fixed, "kind"), {2, 1, 2});
    expectCol(intCol(*fixed, "col"), {0, 1, 2});
    expectXs(*fixed, {-4.5f, -3.0f, -1.5f});

    // flex: the two walls share the leftover 8.8 equally.
    pgg::GeoPtr flexed = geoOutput(r, "flexed");
    ASSERT_TRUE(flexed != nullptr);
    expectCol(f32Col(*flexed, "w"), {4.4f, 1.2f, 4.4f});
    expectXs(*flexed, {-2.8f, 0.0f, 2.8f});

    // cycle: n = 3 repetitions, the flex element absorbs the leftover 1.0.
    pgg::GeoPtr rep = geoOutput(r, "rep");
    ASSERT_TRUE(rep != nullptr);
    expectCol(intCol(*rep, "kind"), {1, 2, 1, 1, 2, 1, 1, 2, 1});
    expectCol(f32Col(*rep, "w"), {2.0f, 1.0f, 1.0f / 3.0f, 2.0f, 1.0f, 1.0f / 3.0f, 2.0f, 1.0f, 1.0f / 3.0f});
    expectCol(intCol(*rep, "col"), {0, 1, 2, 3, 4, 5, 6, 7, 8});
    expectCol(f32Col(*rep, "t"), {0.1f, 0.25f, 0.3166667f, 0.4333333f, 0.5833333f, 0.65f, 0.7666667f, 0.9166667f, 0.9833333f});

    // axis Y: plinth 1.0, floor band takes the 4.8 leftover, crown 0.6.
    // Child @P is the band BOTTOM (the низ-центр scope contract, fixed in A2:
    // used to be the band center).
    pgg::GeoPtr bands = geoOutput(r, "bands");
    ASSERT_TRUE(bands != nullptr);
    expectCol(f32Col(*bands, "h"), {1.0f, 4.8f, 0.6f});
    ASSERT_EQ(bands->pointCount(), 3u);
    expectF32Near((*bands->positions)[0].y, 0.0f, 1e-4f);
    expectF32Near((*bands->positions)[1].y, 1.0f, 1e-4f);
    expectF32Near((*bands->positions)[2].y, 5.8f, 1e-4f);

    // rotated parent (yaw 90): the local +X maps to world -Z.
    pgg::GeoPtr rot = geoOutput(r, "rot");
    ASSERT_TRUE(rot != nullptr);
    ASSERT_EQ(rot->pointCount(), 3u);
    for (size_t i = 0; i < 3; ++i) expectF32Near((*rot->positions)[i].x, 10.0f, 1e-4f);
    expectF32Near((*rot->positions)[0].z, 2.5f, 1e-4f);
    expectF32Near((*rot->positions)[1].z, 1.0f, 1e-4f);
    expectF32Near((*rot->positions)[2].z, -0.5f, 1e-4f);

    // pattern_row: 3 slots of 1.0 at pitch 2.5 from x0=-2.5; margins become walls.
    pgg::GeoPtr rowed = geoOutput(r, "rowed");
    ASSERT_TRUE(rowed != nullptr);
    expectCol(intCol(*rowed, "kind"), {0, 2, 0, 2, 0, 2, 0});
    expectCol(f32Col(*rowed, "w"), {2.0f, 1.0f, 1.5f, 1.0f, 1.5f, 1.0f, 2.0f});
    expectXs(*rowed, {-4.0f, -2.5f, -1.25f, 0.0f, 1.25f, 2.5f, 4.0f});

    // stage merge: split children of two stages share the closed scope schema.
    pgg::GeoPtr merged = geoOutput(r, "merged");
    ASSERT_TRUE(merged != nullptr);
    EXPECT_EQ(merged->pointCount(), 6u);

    // N1: a second run reproduces the points bit-for-bit.
    pgg::RunResult r2 = pgg::runFile(corpusPath("layout_split.pgg"), layoutParams());
    expectNoErrors(r2);
    EXPECT_EQ(geoContentHash(geoOutput(r, "rep")), geoContentHash(geoOutput(r2, "rep")));
}

TEST(LayoutLib, SplitOverflowFailsWithFitMessage) {
    pgg::RunResult r = pgg::runFile(corpusPath("layout_split_overflow.pgg"), layoutParams());
    EXPECT_TRUE(r.hasErrors());
    EXPECT_TRUE(hasMessage(r, "E304", "не влезает"));
}

TEST(LayoutLib, EdgesOfPolygon) {
    pgg::RunResult r = pgg::runFile(corpusPath("layout_edges.pgg"), layoutParams());
    expectNoErrors(r);
    pggtest::expectGolden("layout_edges", r);

    pgg::GeoPtr edges = geoOutput(r, "edges");
    ASSERT_TRUE(edges != nullptr);
    expectCol(f32Col(*edges, "len"), {3.0f, 4.0f, 3.0f, 4.0f});
    expectCol(f32Col(*edges, "yaw"), {0.0f, 90.0f, 180.0f, -90.0f});
    expectCol(intCol(*edges, "role"), {5, 5, 5, 5});

    pgg::GeoPtr diags = geoOutput(r, "diags");
    ASSERT_TRUE(diags != nullptr);
    ASSERT_EQ(diags->pointCount(), 1u);
    expectCol(f32Col(*diags, "len"), {5.0f});
    expectCol(intCol(*diags, "role"), {6});

    // the edge terminal follows the from-origin-along-+Z contract.
    pgg::GeoPtr placed = geoOutput(r, "placed");
    ASSERT_TRUE(placed != nullptr);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const glm::vec3& p : *placed->positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    expectVec3Near(lo, glm::vec3(0.0f, -0.1f, 2.95f), 1e-4f);
    expectVec3Near(hi, glm::vec3(4.0f, 0.1f, 3.05f), 1e-4f);
}

}  // namespace
