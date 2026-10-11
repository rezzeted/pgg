// §8.15 cloth_drape: identity at steps = 0, a sheet coming to rest on a
// static box, E614 and E204.
#include <gtest/gtest.h>

#include "pgg/eval.h"
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

void boundsY(const pgg::Geo& g, float& lo, float& hi) {
    lo = 1.0e9f;
    hi = -1.0e9f;
    for (const glm::vec3& p : *g.positions) {
        lo = std::min(lo, p.y);
        hi = std::max(hi, p.y);
    }
}

TEST(ClothDrape, StepsZeroIsIdentity) {
    const char* src =
        "sheet = grid(size = (1, 1), res = (2, 2))\n"
        "floor = box(size = (2, 0.2, 2))\n"
        "s = cloth_drape(sheet, collider = floor, steps = 0)\n"
        "output sheet\n"
        "output s\n";
    pgg::RunResult r = pgg::run(src);
    expectNoErrors(r);
    EXPECT_EQ(geoContentHash(geoOutput(r, "sheet")), geoContentHash(geoOutput(r, "s")));
}

TEST(ClothDrape, SheetRestsOnBox) {
    const char* src =
        "floor = transform(box(size = (2, 0.2, 2)), translate = (0, -0.1, 0))\n"
        "sheet = transform(grid(size = (0.6, 0.6), res = (3, 3)), translate = (0, 0.45, 0))\n"
        "s = cloth_drape(sheet, collider = floor, steps = 120)\n"
        "output sheet\n"
        "output s\n";
    pgg::RunResult r = pgg::run(src);
    expectNoErrors(r);
    pgg::GeoPtr before = geoOutput(r, "sheet");
    pgg::GeoPtr after = geoOutput(r, "s");
    ASSERT_TRUE(before && after);
    EXPECT_EQ(after->pointCount(), before->pointCount());
    EXPECT_EQ(after->faceCount(), before->faceCount());
    float lo = 0, hi = 0;
    boundsY(*after, lo, hi);
    EXPECT_GT(lo, -0.02f);
    EXPECT_LT(lo, 0.08f);
    EXPECT_LT(hi, 0.25f);
}

TEST(ClothDrape, EmptyColliderIsE614) {
    pgg::RunResult r = pgg::run(
        "sheet = grid(size = (0.4, 0.4), res = (2, 2))\n"
        "s = cloth_drape(sheet, collider = empty_mesh(), steps = 5)\n"
        "output s\n");
    EXPECT_EQ(countCode(r, "E614"), 1);
}

TEST(ClothDrape, PointsAreE204) {
    pgg::RunResult r = pgg::run(
        "p = mesh_line(count = 2, length = 1, dir = (1, 0, 0))\n"
        "floor = box(size = (1, 0.2, 1))\n"
        "s = cloth_drape(p, collider = floor, steps = 5)\n"
        "output s\n");
    EXPECT_GE(countCode(r, "E204"), 1);
}

}  // namespace
