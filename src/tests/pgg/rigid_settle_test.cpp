// §8.14 rigid_settle: identity at steps = 0, a cube coming to rest on another
// cube and on a static floor, thread-count invariance, E613 and E204.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "goldens_utils.h"
#include "test_utils.h"

namespace {

using pggtest::expectNoErrors;
using pggtest::geoContentHash;
using pggtest::geoOutput;

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

pgg::RunResult runSrc(const std::string& src, unsigned threads = 0) {
    pgg::RunParams p;
    p.threads = threads;
    return pgg::run(src, p);
}

void boundsY(const pgg::Geo& g, float& lo, float& hi) {
    lo = 1.0e9f;
    hi = -1.0e9f;
    for (const glm::vec3& p : *g.positions) {
        lo = std::min(lo, p.y);
        hi = std::max(hi, p.y);
    }
}

TEST(RigidSettle, StepsZeroIsIdentity) {
    const char* src =
        "b = box(size = (1, 1, 1))\n"
        "s = rigid_settle(b, steps = 0)\n"
        "output b\n"
        "output s\n";
    pgg::RunResult r = runSrc(src);
    expectNoErrors(r);
    pgg::GeoPtr a = geoOutput(r, "b");
    pgg::GeoPtr b = geoOutput(r, "s");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(geoContentHash(a), geoContentHash(b));
}

TEST(RigidSettle, UpperCubeRestsOnLower) {
    const char* src =
        "floor = transform(box(size = (4, 0.2, 4)), translate = (0, -0.1, 0))\n"
        "lo = transform(box(size = (1, 1, 1)), translate = (0, 0.5, 0))\n"
        "hi = transform(box(size = (1, 1, 1)), translate = (0, 2.5, 0))\n"
        "s = rigid_settle(merge(lo, hi), static = floor, steps = 180)\n"
        "output s\n";
    pgg::RunResult r = runSrc(src);
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 12u);
    float lo = 0, hi = 0;
    boundsY(*g, lo, hi);
    EXPECT_GT(lo, -0.05f);
    EXPECT_LT(lo, 0.08f);
    EXPECT_GT(hi, 1.8f);
    EXPECT_LT(hi, 2.15f);
}

TEST(RigidSettle, CubeRestsOnStaticFloor) {
    const char* src =
        "floor = transform(box(size = (3, 0.2, 3)), translate = (0, -0.1, 0))\n"
        "cube = transform(box(size = (1, 1, 1)), translate = (0, 2, 0))\n"
        "s = rigid_settle(cube, static = floor, steps = 180)\n"
        "output cube\n"
        "output s\n";
    pgg::RunResult r = runSrc(src);
    expectNoErrors(r);
    pgg::GeoPtr before = geoOutput(r, "cube");
    pgg::GeoPtr after = geoOutput(r, "s");
    ASSERT_TRUE(before && after);
    EXPECT_EQ(after->pointCount(), before->pointCount());
    EXPECT_EQ(after->faceCount(), before->faceCount());
    float lo = 0, hi = 0;
    boundsY(*after, lo, hi);
    EXPECT_NEAR(lo, 0.0f, 0.05f);
    EXPECT_NEAR(hi - lo, 1.0f, 0.05f);
}

TEST(RigidSettle, ThreadCountDoesNotChangePose) {
    const char* src =
        "floor = transform(box(size = (3, 0.2, 3)), translate = (0, -0.1, 0))\n"
        "cube = transform(box(size = (1, 1, 1)), translate = (0, 1.5, 0))\n"
        "s = rigid_settle(cube, static = floor, steps = 90)\n"
        "output s\n";
    pgg::RunResult a = runSrc(src, 1);
    pgg::RunResult b = runSrc(src, 4);
    expectNoErrors(a);
    expectNoErrors(b);
    EXPECT_EQ(geoContentHash(geoOutput(a, "s")), geoContentHash(geoOutput(b, "s")));
}

TEST(RigidSettle, FlatIslandIsE613) {
    pgg::RunResult r = runSrc(
        "g = grid(size = (1, 1), res = (1, 1))\n"
        "s = rigid_settle(g, steps = 5)\n"
        "output s\n");
    EXPECT_EQ(countCode(r, "E613"), 1);
}

TEST(RigidSettle, PointsAreE204) {
    pgg::RunResult r = runSrc(
        "p = mesh_line(count = 2, length = 1, dir = (1, 0, 0))\n"
        "s = rigid_settle(p, steps = 5)\n"
        "output s\n");
    EXPECT_GE(countCode(r, "E204"), 1);
}

}  // namespace
