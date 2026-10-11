// §8.9 geometry queries (v1.34): inside_polygon (plan even-odd mask) and
// raycast (one BVH ray per point, answers stamped as @hit* attributes).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/pgg.h"
#include "test_utils.h"

namespace {

pgg::GeoPtr outputGeo(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

const std::string kPlot =
    "def pt(x: f32, z: f32) -> (out: geo<points>) {\n"
    "    out = set_position(mesh_line(count = 1, length = 0.0), pos = (x, 0, z))\n"
    "}\n"
    // L-shaped plot: the 2x2 square minus its (+x, +z) quarter.
    "plot = merge(pt(x = 0, z = 0), pt(x = 2, z = 0), pt(x = 2, z = 1), pt(x = 1, z = 1), pt(x = 1, z = 2), "
    "pt(x = 0, z = 2))\n";

TEST(Query, InsidePolygonMasksPointsEvenOdd) {
    pgg::RunResult r = pgg::run(
        kPlot +
        "probe_pts = merge(pt(x = 0.5, z = 0.5), pt(x = 1.5, z = 0.5), pt(x = 1.5, z = 1.5), pt(x = 0.5, z = 1.5), "
        "pt(x = 3, z = 0.5))\n"
        "m = set(probe_pts, \"in\", inside_polygon(poly = plot))\n"
        "lifted = set(probe_pts, \"in\", inside_polygon(poly = plot, pos = @P + (0, 5, 0)))\n"
        "output m\noutput lifted\n");
    pggtest::expectNoErrors(r);
    for (const char* name : {"m", "lifted"}) {
        pgg::GeoPtr g = outputGeo(r, name);
        ASSERT_TRUE(g);
        const pgg::AttrColumn* col = g->pointAttrs ? g->pointAttrs->find("in") : nullptr;
        ASSERT_TRUE(col);
        const auto& v = *std::get<std::shared_ptr<const std::vector<uint8_t>>>(col->data);
        EXPECT_EQ(v, (std::vector<uint8_t>{1, 1, 0, 1, 0})) << name;
    }
}

TEST(Query, RaycastDropsPointsOntoMesh) {
    pgg::RunResult r = pgg::run(
        "ground = transform(grid(size = (4, 4), res = (4, 4)), translate = (0, 1, 0))\n"
        "roof = transform(box(size = (1, 1, 1)), translate = (0, 3, 0))\n"
        "target = merge(ground, roof)\n"
        "pts = set_position(mesh_line(count = 3, length = 3.0, dir = (1, 0, 0)), offset = (0, 10, 0))\n"
        "r = raycast(pts, target = target)\n"
        "far = raycast(pts, target = target, origin = @P + (10, 0, 0))\n"
        "up = raycast(pts, target = target, dir = (0, 1, 0))\n"
        "dropped = set_position(r, pos = @hit_pos, where = @hit)\n"
        "output dropped\noutput far\noutput up\n");
    pggtest::expectNoErrors(r);
    pgg::GeoPtr d = outputGeo(r, "dropped");
    ASSERT_TRUE(d);
    ASSERT_EQ(d->pointCount(), 3u);
    // x = 0 lands on the box top (y = 3.5), x = 1.5 on the ground, x = 3 is past the grid (+-2).
    EXPECT_NEAR((*d->positions)[0].y, 3.5f, 1e-5f);
    EXPECT_NEAR((*d->positions)[1].y, 1.0f, 1e-5f);
    EXPECT_NEAR((*d->positions)[2].y, 10.0f, 1e-5f);
    const auto& hit = *std::get<std::shared_ptr<const std::vector<uint8_t>>>(d->pointAttrs->find("hit")->data);
    EXPECT_EQ(hit, (std::vector<uint8_t>{1, 1, 0}));
    const auto& dist = *std::get<std::shared_ptr<const std::vector<float>>>(d->pointAttrs->find("hit_dist")->data);
    EXPECT_NEAR(dist[0], 6.5f, 1e-5f);
    EXPECT_NEAR(dist[1], 9.0f, 1e-5f);
    EXPECT_EQ(dist[2], -1.0f);
    const auto& n = *std::get<std::shared_ptr<const std::vector<glm::vec3>>>(d->pointAttrs->find("hit_n")->data);
    EXPECT_NEAR(std::abs(n[0].y), 1.0f, 1e-5f);

    pgg::GeoPtr far = outputGeo(r, "far");
    const auto& farHit = *std::get<std::shared_ptr<const std::vector<uint8_t>>>(far->pointAttrs->find("hit")->data);
    EXPECT_EQ(farHit, (std::vector<uint8_t>{0, 0, 0}));
    pgg::GeoPtr up = outputGeo(r, "up");
    const auto& upHit = *std::get<std::shared_ptr<const std::vector<uint8_t>>>(up->pointAttrs->find("hit")->data);
    EXPECT_EQ(upHit, (std::vector<uint8_t>{0, 0, 0}));
}

TEST(Query, RaycastSchemaAndEmptyTarget) {
    pgg::RunResult st = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "r = raycast(pts, target = box(size = (1, 1, 1)))\n"
        "bad = set_position(r, pos = @hit_poz)\n"
        "output bad\n");
    bool e302 = false;
    for (const pgg::Diagnostic& d : st.diagnostics) e302 |= d.code == "E302";
    EXPECT_TRUE(e302);
    pgg::RunResult empty = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "r = raycast(pts, target = empty_mesh())\n"
        "output r\n");
    bool e603 = false;
    for (const pgg::Diagnostic& d : empty.diagnostics) e603 |= d.code == "E603";
    EXPECT_TRUE(e603);
}

}  // namespace
