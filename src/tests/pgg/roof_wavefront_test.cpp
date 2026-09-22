// A4 (docs/pgg/architecture_library_plan.md, spec §8 L1): the weighted
// straight skeleton behind roof_wavefront. Convex outlines (rect = hip roof,
// square = pyramid peak cluster), reflex outlines (L with a valley, U/T with
// split cascades), gable walls (speed 0 edges never move), and the closing
// ridge of a two-vertex ring. Faces must tile the outline exactly (areas sum
// to the outline's, every face closed and planar).
#include <cmath>

#include <gtest/gtest.h>

#include "goldens_utils.h"
#include "src/eval/straight_skeleton.h"
#include "test_utils.h"

namespace {

using pgg::SkeletonInput;
using pgg::StraightSkeleton;

float signedArea(const std::vector<glm::vec2>& ring) {
    float a = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2& p = ring[i];
        const glm::vec2& q = ring[(i + 1) % ring.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

StraightSkeleton skeletonOf(std::vector<glm::vec2> outline, float speed = 1.0f) {
    SkeletonInput in;
    in.outline = std::move(outline);
    in.speed.assign(in.outline.size(), speed);
    return pgg::buildStraightSkeleton(in);
}

// Every face closed (its last node reaches back to its first outline vertex),
// faces tile the outline (sum of face areas == outline area), all node times
// finite and >= 0.
void expectSkeletonSane(const StraightSkeleton& sk, const std::vector<glm::vec2>& outline) {
    ASSERT_EQ(sk.faces.size(), outline.size());
    float total = 0.0f;
    for (const pgg::SkeletonFace& f : sk.faces) {
        ASSERT_GE(f.nodes.size(), 3u) << "face " << f.edge << " degenerate";
        std::vector<glm::vec2> poly;
        for (int32_t idx : f.nodes) poly.push_back(sk.nodes[static_cast<size_t>(idx)].p);
        total += signedArea(poly);
    }
    pggtest::expectF32Near(total, signedArea(outline), 1e-3f);
    for (const pgg::SkeletonNode& n : sk.nodes) {
        EXPECT_TRUE(std::isfinite(n.t));
        EXPECT_GE(n.t, 0.0f);
    }
    for (const pgg::SkeletonArc& a : sk.arcs) {
        EXPECT_NE(a.leftEdge, -1);
        EXPECT_NE(a.rightEdge, -1);
    }
}

TEST(RoofWavefront, RectIsHip) {
    // rect 8 x 5 (CCW-from-above: signed area < 0), uniform speed: hip roof.
    // Ridge from (-1.5, 0) to (1.5, 0) at t = 2.5; two triangle hips, two
    // trapezoid slopes.
    std::vector<glm::vec2> outline = {{-4, -2.5f}, {-4, 2.5f}, {4, 2.5f}, {4, -2.5f}};
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
    ASSERT_EQ(sk.nodes.size(), 6u);
    ASSERT_EQ(sk.arcs.size(), 5u);
    // skeleton nodes 4 and 5: the ridge ends.
    pggtest::expectF32Near(sk.nodes[4].t, 2.5f, 1e-4f);
    pggtest::expectF32Near(sk.nodes[5].t, 2.5f, 1e-4f);
    EXPECT_TRUE((std::abs(sk.nodes[4].p.x + 1.5f) < 1e-4f && std::abs(sk.nodes[5].p.x - 1.5f) < 1e-4f) ||
                (std::abs(sk.nodes[4].p.x - 1.5f) < 1e-4f && std::abs(sk.nodes[5].p.x + 1.5f) < 1e-4f));
    // faces: short edges -> triangles, long edges -> quads.
    ASSERT_EQ(sk.faces.size(), 4u);
    EXPECT_EQ(sk.faces[0].nodes.size(), 3u);
    EXPECT_EQ(sk.faces[1].nodes.size(), 4u);
    EXPECT_EQ(sk.faces[2].nodes.size(), 3u);
    EXPECT_EQ(sk.faces[3].nodes.size(), 4u);
}

TEST(RoofWavefront, SquareIsPyramidPeak) {
    // Square: all four corners meet at the center in one event cluster.
    std::vector<glm::vec2> outline = {{-2, -2}, {-2, 2}, {2, 2}, {2, -2}};
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
    ASSERT_EQ(sk.nodes.size(), 5u);
    pggtest::expectVec3Near(glm::vec3(sk.nodes[4].p.x, 0.0f, sk.nodes[4].p.y), glm::vec3(0.0f), 1e-4f);
    pggtest::expectF32Near(sk.nodes[4].t, 2.0f, 1e-4f);
    EXPECT_EQ(sk.arcs.size(), 4u);
    for (const pgg::SkeletonFace& f : sk.faces) EXPECT_EQ(f.nodes.size(), 3u);
}

TEST(RoofWavefront, GableWallsNeverMove) {
    // rect 8 x 5 with speed 0 on the short edges: gable roof. The short-edge
    // faces are vertical triangles; the ridge spans (-4, 0) .. (4, 0) at 2.5.
    std::vector<glm::vec2> outline = {{-4, -2.5f}, {-4, 2.5f}, {4, 2.5f}, {4, -2.5f}};
    SkeletonInput in;
    in.outline = outline;
    in.speed = {0.0f, 1.0f, 0.0f, 1.0f};
    StraightSkeleton sk = pgg::buildStraightSkeleton(in);
    expectSkeletonSane(sk, outline);
    ASSERT_EQ(sk.nodes.size(), 6u);
    pggtest::expectF32Near(sk.nodes[4].t, 2.5f, 1e-4f);
    pggtest::expectF32Near(sk.nodes[5].t, 2.5f, 1e-4f);
    // gable faces: 3 nodes, all x pinned to the wall line.
    ASSERT_EQ(sk.faces[0].nodes.size(), 3u);
    for (int32_t idx : sk.faces[0].nodes) pggtest::expectF32Near(sk.nodes[static_cast<size_t>(idx)].p.x, -4.0f, 1e-4f);
    // long slopes: 4 nodes each.
    EXPECT_EQ(sk.faces[1].nodes.size(), 4u);
    EXPECT_EQ(sk.faces[3].nodes.size(), 4u);
}

TEST(RoofWavefront, LPlanValleyAndSplits) {
    // L outline 9 x 9, cut 3 x 2.5 at (+x, +z): one reflex corner at (1.5, 2).
    // The skeleton has a valley arc from that corner heading (-1, -1) and the
    // faces still tile the outline exactly.
    std::vector<glm::vec2> outline = {{-4.5f, -4.5f}, {-4.5f, 4.5f}, {1.5f, 4.5f}, {1.5f, 2.0f}, {4.5f, 2.0f}, {4.5f, -4.5f}};
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
    // The reflex vertex's first arc heads (-1, -1) from (1.5, 2).
    bool foundValley = false;
    for (const pgg::SkeletonArc& a : sk.arcs) {
        const pgg::SkeletonNode& na = sk.nodes[static_cast<size_t>(a.a)];
        const pgg::SkeletonNode& nb = sk.nodes[static_cast<size_t>(a.b)];
        if (std::abs(na.p.x - 1.5f) < 1e-4f && std::abs(na.p.y - 2.0f) < 1e-4f) {
            foundValley = true;
            EXPECT_LT(nb.p.x, na.p.x);
            EXPECT_LT(nb.p.y, na.p.y);
        }
    }
    EXPECT_TRUE(foundValley) << "no valley arc from the reflex corner";
}

TEST(RoofWavefront, UPlanSplitsCascade) {
    // U outline 9 x 6.5, notch 3 x 2.5 from the front (+z): two reflex corners,
    // several split events; faces tile the outline.
    std::vector<glm::vec2> outline = {{-4.5f, -3.25f}, {-4.5f, 3.25f}, {-1.5f, 3.25f}, {-1.5f, 0.75f},
                                      {1.5f, 0.75f},  {1.5f, 3.25f}, {4.5f, 3.25f}, {4.5f, -3.25f}};
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
}

TEST(RoofWavefront, TPlanSplitsCascade) {
    // T outline 9 x 6.5, bar 6 deep, stem 3 wide: two reflex shoulders.
    std::vector<glm::vec2> outline = {{-4.5f, -3.25f}, {-4.5f, 0.75f}, {-1.5f, 0.75f}, {-1.5f, 3.25f},
                                      {1.5f, 3.25f},  {1.5f, 0.75f}, {4.5f, 0.75f}, {4.5f, -3.25f}};
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
}

TEST(RoofWavefront, CrossPlanEightReflex) {
    // Cross outline (arm 3 x 3 in a 9 x 7 box): 12 vertices, 8 reflex corners —
    // the heaviest split cascade of the plan builders. Faces must still tile.
    const float hw = 4.5f, hd = 3.5f, aw = 1.5f, ad = 1.5f;
    std::vector<glm::vec2> outline = {
        {-hw, -hd}, {-hw, -ad}, {-aw, -ad}, {-aw, ad}, {-hw, ad}, {-hw, hd},
        {hw, hd},   {hw, ad},   {aw, ad},   {aw, -ad}, {hw, -ad}, {hw, -hd},
    };
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
}

TEST(RoofWavefront, CrossPlanCrossBuilderOrder) {
    // The exact plan_cross vertex order (arm 3 x 3 in a 9 x 7 box): same shape
    // as CrossPlanEightReflex but the walk starts at the stem tip — the skeleton
    // must be start-invariant. Corpus regression: 8 faces came out degenerate.
    const float hw = 4.5f, hd = 3.5f, aw = 1.5f, ad = 1.5f;
    std::vector<glm::vec2> outline = {
        {-aw, -hd}, {-aw, -ad}, {-hw, -ad}, {-hw, ad}, {-aw, ad}, {-aw, hd},
        {aw, hd},   {aw, ad},   {hw, ad},   {hw, -ad}, {aw, -ad}, {aw, -hd},
    };
    StraightSkeleton sk = skeletonOf(outline);
    expectSkeletonSane(sk, outline);
    for (const pgg::SkeletonFace& f : sk.faces) ASSERT_GE(f.nodes.size(), 3u) << "degenerate face " << f.edge;
}

}  // namespace

namespace {

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

std::vector<glm::vec3> sortedPositions(const pgg::Geo& g) {
    std::vector<glm::vec3> v = *g.positions;
    std::sort(v.begin(), v.end(), [](const glm::vec3& a, const glm::vec3& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    return v;
}

// The plan acceptance: the straight skeleton over a rect reproduces the
// analytic hip roof vertex set exactly (same 14 panel points, ridge at 5.5).
TEST(RoofWavefront, BuiltinMatchesAnalyticHip) {
    pgg::RunResult r = pgg::runFile(corpusPath("arch_roof_wavefront.pgg"), archParams());
    pggtest::expectNoErrors(r);
    pggtest::expectGolden("arch_roof_wavefront", r);

    pgg::GeoPtr wf = pggtest::geoOutput(r, "wf_p");
    pgg::GeoPtr hip = pggtest::geoOutput(r, "hip_s");
    ASSERT_TRUE(wf != nullptr && hip != nullptr);
    ASSERT_EQ(wf->faceCount(), 4u);
    ASSERT_EQ(hip->faceCount(), 14u - 10u);  // 4 faces
    std::vector<glm::vec3> a = sortedPositions(*wf);
    std::vector<glm::vec3> b = sortedPositions(*hip);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) pggtest::expectVec3Near(a[i], b[i], 1e-4f);

    // edges: 4 eaves + 4 hips + 1 ridge of len 3.0 at y = 5.5.
    pgg::GeoPtr wf_e = pggtest::geoOutput(r, "wf_e");
    ASSERT_TRUE(wf_e != nullptr);
    ASSERT_EQ(wf_e->pointCount(), 9u);
    const std::vector<int64_t>* roles = intCol(*wf_e, "role");
    ASSERT_TRUE(roles != nullptr);
    size_t ridges = 0, hips = 0, eaves = 0;
    for (int64_t role : *roles) {
        ridges += role == 0;
        hips += role == 1;
        eaves += role == 3;
    }
    EXPECT_EQ(ridges, 1u);
    EXPECT_EQ(hips, 4u);
    EXPECT_EQ(eaves, 4u);
    const std::vector<float>* lens = f32Col(*wf_e, "len");
    const std::vector<float>* y1s = f32Col(*wf_e, "y1");
    ASSERT_TRUE(lens != nullptr && y1s != nullptr);
    for (size_t i = 0; i < roles->size(); ++i)
        if ((*roles)[i] == 0) {
            pggtest::expectF32Near((*lens)[i], 3.0f, 1e-3f);
            pggtest::expectF32Near((*y1s)[i], 5.5f, 1e-3f);
        }

    // T and cross plans: roofs unreachable by the A3 analytics.
    pgg::GeoPtr t_p = pggtest::geoOutput(r, "t_p");
    ASSERT_TRUE(t_p != nullptr);
    EXPECT_EQ(t_p->faceCount(), 8u);
    pgg::GeoPtr c_p = pggtest::geoOutput(r, "c_p");
    ASSERT_TRUE(c_p != nullptr);
    EXPECT_EQ(c_p->faceCount(), 12u);

    // Mansard cut: every panel clipped at y = 1.5, the deck rim is a 4-point ring.
    pgg::GeoPtr man_p = pggtest::geoOutput(r, "man_p");
    ASSERT_TRUE(man_p != nullptr);
    EXPECT_EQ(man_p->faceCount(), 4u);
    for (const glm::vec3& p : *man_p->positions) EXPECT_LE(p.y, 1.5f + 1e-4f);
    pgg::GeoPtr man_t = pggtest::geoOutput(r, "man_t");
    ASSERT_TRUE(man_t != nullptr);
    ASSERT_EQ(man_t->pointCount(), 4u);
    for (const glm::vec3& p : *man_t->positions) pggtest::expectF32Near(p.y, 1.5f, 1e-4f);

    // Overhang + planes: the eave line moves out by 0.4 and tile_anchors covers
    // the skeleton panels without outliers (every anchor inside the roof bbox).
    pgg::GeoPtr oh_p = pggtest::geoOutput(r, "oh_p");
    ASSERT_TRUE(oh_p != nullptr);
    pgg::GeoPtr oh_pl = pggtest::geoOutput(r, "oh_pl");
    ASSERT_TRUE(oh_pl != nullptr);
    EXPECT_EQ(oh_pl->pointCount(), 14u);  // 4 panels x their polygon edges
    pgg::GeoPtr anchors = pggtest::geoOutput(r, "anchors");
    ASSERT_TRUE(anchors != nullptr);
    ASSERT_GT(anchors->pointCount(), 400u);
    for (const glm::vec3& p : *anchors->positions) {
        EXPECT_GE(p.x, -4.5f);
        EXPECT_LE(p.x, 4.5f);
        EXPECT_GE(p.z, -3.0f);
        EXPECT_LE(p.z, 3.0f);
        EXPECT_GE(p.y, -0.01f);
        EXPECT_LE(p.y, 3.0f);
    }
}

TEST(RoofWavefront, RiseMaxCutSealsRings) {
    // rect 8 x 5 cut at t = 1.5 (mansard): one cut ring of 4 nodes at t = 1.5,
    // panels stop at the cut, faces + cut ring tile the outline.
    std::vector<glm::vec2> outline = {{-4, -2.5f}, {-4, 2.5f}, {4, 2.5f}, {4, -2.5f}};
    SkeletonInput in;
    in.outline = outline;
    in.speed.assign(outline.size(), 1.0f);
    StraightSkeleton sk = pgg::buildStraightSkeleton(in, 1.5f);
    ASSERT_EQ(sk.topRings.size(), 1u);
    ASSERT_EQ(sk.topRings[0].size(), 4u);
    for (int32_t idx : sk.topRings[0]) pggtest::expectF32Near(sk.nodes[static_cast<size_t>(idx)].t, 1.5f, 1e-4f);
    for (const pgg::SkeletonNode& nd : sk.nodes) EXPECT_LE(nd.t, 1.5f + 1e-4f);
    float total = 0.0f;
    for (const pgg::SkeletonFace& f : sk.faces) {
        std::vector<glm::vec2> poly;
        for (int32_t idx : f.nodes) poly.push_back(sk.nodes[static_cast<size_t>(idx)].p);
        total += signedArea(poly);
    }
    std::vector<glm::vec2> top;
    for (int32_t idx : sk.topRings[0]) top.push_back(sk.nodes[static_cast<size_t>(idx)].p);
    total += signedArea(top);
    pggtest::expectF32Near(total, signedArea(outline), 1e-3f);
}

TEST(RoofWavefront, ShedRoofThreeGables) {
    // Shed (single slope) 4 x 6: three gable walls (speed 0), one sloped edge
    // on the low side (x = 4): one rectangular slope panel from x = 4 up to the
    // high edge x = 0, three vertical walls, faces tile the outline.
    std::vector<glm::vec2> outline = {{0, -3}, {0, 3}, {4, 3}, {4, -3}};
    SkeletonInput in;
    in.outline = outline;
    in.speed = {0.0f, 0.0f, 1.0f, 0.0f};
    StraightSkeleton sk = pgg::buildStraightSkeleton(in);
    expectSkeletonSane(sk, outline);
    // slope face (edge 2, the low edge): quad from the low edge to the high one.
    ASSERT_EQ(sk.faces.size(), 4u);
    EXPECT_EQ(sk.faces[2].nodes.size(), 4u);
    // the high edge (edge 0) is a 6 x 4 vertical wall quad; the two short
    // sides are gable triangles pinned to their walls.
    EXPECT_EQ(sk.faces[0].nodes.size(), 4u);
    EXPECT_EQ(sk.faces[1].nodes.size(), 3u);
    EXPECT_EQ(sk.faces[3].nodes.size(), 3u);
}

}  // namespace
