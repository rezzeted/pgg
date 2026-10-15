// Roof parity: the BTG RoofModel (geom2d straight skeleton, double) vs the PGG
// straight_skeleton builtin (float) on the frozen B0 house volume. Pins the
// requirements §7.2 contract: the planes the topology reasons about (attic
// mask, chimney exits, dormers) are exactly the planes the geometry builds.

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "pgg/eval.h"
#include "pgg/src/eval/geometry.h"

#include "ir.h"
#include "roof.h"

namespace building_topology_generator {
namespace {

constexpr double kTol = 1e-3;

// @arc_class of the edges output (builtins_skeleton.cpp).
constexpr int64_t kArcOutline = 0;
constexpr int64_t kArcConvex = 1;
constexpr int64_t kArcReflex = 2;
constexpr int64_t kArcRidge = 3;
constexpr int64_t kArcWall = 4;
constexpr int64_t kArcCut = 5;

struct Parity {
    Ir ir;
    RoofModel model;
    pgg::RunResult run;
};

pgg::GeoPtr outputGeo(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

template <typename T>
const std::vector<T>* column(const pgg::Geo& g, pgg::Domain domain, const char* name) {
    const pgg::AttrSet* attrs = g.attrs(domain);
    if (!attrs) return nullptr;
    const pgg::AttrColumn* col = attrs->find(name);
    if (!col) return nullptr;
    const auto* buf = std::get_if<std::shared_ptr<const std::vector<T>>>(&col->data);
    return buf ? buf->get() : nullptr;
}

glm::dvec3 panelNormal(const pgg::Geo& g, size_t f) {
    glm::dvec3 n(0.0);
    const std::vector<glm::vec3>& pos = *g.positions;
    const size_t c0 = static_cast<size_t>((*g.faceOffsets)[f]);
    const size_t c1 = static_cast<size_t>((*g.faceOffsets)[f + 1]);
    for (size_t c = c0; c < c1; ++c) {
        const glm::vec3& a = pos[static_cast<size_t>((*g.cornerVerts)[c])];
        const glm::vec3& b = pos[static_cast<size_t>((*g.cornerVerts)[c + 1 == c1 ? c0 : c + 1])];
        n.x += static_cast<double>((a.y - b.y)) * (a.z + b.z);
        n.y += static_cast<double>((a.z - b.z)) * (a.x + b.x);
        n.z += static_cast<double>((a.x - b.x)) * (a.y + b.y);
    }
    return glm::length(n) < 1e-12 ? glm::dvec3(0, 1, 0) : glm::normalize(n);
}

glm::dvec3 panelPoint(const pgg::Geo& g, size_t f) {
    const glm::vec3& p = (*g.positions)[static_cast<size_t>((*g.cornerVerts)[(*g.faceOffsets)[f]])];
    return glm::dvec3(p);
}

int faceByEdge(const std::vector<int64_t>& edgeId, int64_t e) {
    for (size_t f = 0; f < edgeId.size(); ++f)
        if (edgeId[f] == e) return static_cast<int>(f);
    return -1;
}

bool setup(Parity& p, std::string& err) {
    if (!load_ir_file(std::string(BUILDING_FROZEN_DIR) + "/house_b0.ir.json", p.ir, err)) return false;
    if (p.ir.volumes.empty()) {
        err = "frozen IR has no volumes";
        return false;
    }
    const IrVolume& vol = p.ir.volumes[0];
    if (!build_roof_model(vol, p.model, err)) return false;

    double slopePitch = 0.0;
    for (double pd : vol.roof.pitch_by_edge)
        if (pd < 89.999) slopePitch = pd;

    std::vector<glm::vec3> pos;
    std::vector<float> pitch;
    pos.reserve(vol.outline.size());
    pitch.reserve(vol.outline.size());
    for (size_t i = 0; i < vol.outline.size(); ++i) {
        pos.emplace_back(static_cast<float>(vol.outline[i].x), 0.0f, static_cast<float>(vol.outline[i].y));
        pitch.push_back(static_cast<float>(vol.roof.pitch_by_edge[i]));
    }
    pgg::GeoPtr outline = pgg::makePoints(std::move(pos));
    pgg::AttrSet attrs;
    attrs.columns["pitch"] = pgg::AttrColumn{std::make_shared<const std::vector<float>>(std::move(pitch)),
                                             pgg::AttrTypeInfo::None};
    outline = pgg::withAttrs(*outline, pgg::Domain::Points, std::make_shared<const pgg::AttrSet>(std::move(attrs)));

    const std::string src =
        "param outline: geo<points>\n"
        "param pitch: f32\n"
        "param y0: f32\n"
        "param overhang: f32\n"
        "panels, edges, top, planes = straight_skeleton(outline, pitch = pitch, cut = 0.0, y0 = y0, offset = overhang)\n"
        "output panels\n"
        "output edges\n"
        "output planes\n";
    pgg::RunParams rp;
    rp.values.push_back({"outline", pgg::Value(outline)});
    rp.values.push_back({"pitch", pgg::Value(static_cast<float>(slopePitch))});
    rp.values.push_back({"y0", pgg::Value(static_cast<float>(p.model.y0))});
    rp.values.push_back({"overhang", pgg::Value(static_cast<float>(vol.roof.overhang))});
    p.run = pgg::run(src, rp);
    if (p.run.hasErrors()) {
        for (const pgg::Diagnostic& d : p.run.diagnostics)
            if (!d.isWarning) err += d.code + " " + d.message + "\n";
        return false;
    }
    return true;
}

TEST(BuildingRoofParity, SkeletonRuns) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;

    pgg::GeoPtr panels = outputGeo(p.run, "panels");
    ASSERT_NE(panels, nullptr);
    // One panel per source edge of the rectangular volume.
    EXPECT_EQ(panels->faceCount(), 4u);
    const std::vector<int64_t>* edgeId = column<int64_t>(*panels, pgg::Domain::Faces, "edge_id");
    const std::vector<float>* fpitch = column<float>(*panels, pgg::Domain::Faces, "pitch");
    ASSERT_NE(edgeId, nullptr);
    ASSERT_NE(fpitch, nullptr);
    ASSERT_EQ(edgeId->size(), 4u);
    const IrVolume& vol = p.ir.volumes[0];
    for (size_t f = 0; f < 4; ++f)
        EXPECT_NEAR((*fpitch)[f], static_cast<float>(vol.roof.pitch_by_edge[static_cast<size_t>((*edgeId)[f])]), 1e-4)
            << "face " << f;
}

TEST(BuildingRoofParity, SlopePlanesMatch) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;
    ASSERT_EQ(p.model.planes.size(), 2u);

    pgg::GeoPtr panels = outputGeo(p.run, "panels");
    ASSERT_NE(panels, nullptr);
    const std::vector<int64_t>* edgeId = column<int64_t>(*panels, pgg::Domain::Faces, "edge_id");
    ASSERT_NE(edgeId, nullptr);

    for (const IrRoofPlane& pl : p.model.planes) {
        const int f = faceByEdge(*edgeId, pl.edge);
        ASSERT_GE(f, 0) << "no panel for edge " << pl.edge;
        const glm::dvec3 n = panelNormal(*panels, static_cast<size_t>(f));
        const glm::dvec3 pt = panelPoint(*panels, static_cast<size_t>(f));
        EXPECT_NEAR(glm::dot(n, pl.n), 1.0, kTol) << "edge " << pl.edge;
        // The BTG plane point (midpoint of the offset eave at y0) lies on the
        // PGG panel plane.
        EXPECT_NEAR(glm::dot(pl.o - pt, pl.n), 0.0, kTol) << "edge " << pl.edge;
    }
}

TEST(BuildingRoofParity, PlanesBoundaryRows) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;

    pgg::GeoPtr planes = outputGeo(p.run, "planes");
    ASSERT_NE(planes, nullptr);
    const std::vector<int64_t>* island = column<int64_t>(*planes, pgg::Domain::Points, "island_id");
    const std::vector<uint8_t>* base = column<uint8_t>(*planes, pgg::Domain::Points, "base");
    const std::vector<glm::vec3>* nrm = column<glm::vec3>(*planes, pgg::Domain::Points, "n");
    ASSERT_NE(island, nullptr);
    ASSERT_NE(base, nullptr);
    ASSERT_NE(nrm, nullptr);

    // The base=1 row of each sloped panel sits on the midpoint of the offset
    // eave at y0 with the horizontal outward normal (tile_anchors contract).
    const size_t n = p.model.sweep_outline.size();
    for (const IrRoofPlane& pl : p.model.planes) {
        int found = 0;
        for (size_t i = 0; i < island->size(); ++i) {
            if ((*island)[i] != pl.edge || (*base)[i] == 0) continue;
            ++found;
            const glm::dvec2& a = p.model.sweep_outline[static_cast<size_t>(pl.edge)];
            const glm::dvec2& b = p.model.sweep_outline[(static_cast<size_t>(pl.edge) + 1) % n];
            const glm::dvec3 oExp((a.x + b.x) * 0.5, p.model.y0, (a.y + b.y) * 0.5);
            const glm::dvec2 d = b - a;
            const glm::dvec3 nExp(-d.y / glm::length(d), 0.0, d.x / glm::length(d));
            const glm::dvec3 oGot((*planes->positions)[i]);
            EXPECT_NEAR(glm::length(oGot - oExp), 0.0, kTol) << "edge " << pl.edge;
            EXPECT_NEAR(glm::dot(glm::dvec3((*nrm)[i]), nExp), 1.0, kTol) << "edge " << pl.edge;
        }
        EXPECT_EQ(found, 1) << "edge " << pl.edge;
    }
}

TEST(BuildingRoofParity, RidgeMatches) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;
    ASSERT_EQ(p.model.ridges.size(), 1u);

    pgg::GeoPtr edges = outputGeo(p.run, "edges");
    ASSERT_NE(edges, nullptr);
    const std::vector<int64_t>* cls = column<int64_t>(*edges, pgg::Domain::Points, "arc_class");
    const std::vector<glm::vec3>* p0 = column<glm::vec3>(*edges, pgg::Domain::Points, "p0");
    const std::vector<glm::vec3>* p1 = column<glm::vec3>(*edges, pgg::Domain::Points, "p1");
    ASSERT_NE(cls, nullptr);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    int ridges = 0;
    for (size_t i = 0; i < cls->size(); ++i) {
        if ((*cls)[i] != kArcRidge) continue;
        ++ridges;
        const glm::dvec3 qa((*p0)[i]), qb((*p1)[i]);
        const glm::dvec3& ra = p.model.ridges[0].a;
        const glm::dvec3& rb = p.model.ridges[0].b;
        const double direct = glm::length(qa - ra) + glm::length(qb - rb);
        const double flipped = glm::length(qa - rb) + glm::length(qb - ra);
        EXPECT_NEAR(std::min(direct, flipped), 0.0, kTol);
    }
    EXPECT_EQ(ridges, 1);
}

TEST(BuildingRoofParity, ArcClassification) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;

    pgg::GeoPtr edges = outputGeo(p.run, "edges");
    ASSERT_NE(edges, nullptr);
    const std::vector<int64_t>* cls = column<int64_t>(*edges, pgg::Domain::Points, "arc_class");
    ASSERT_NE(cls, nullptr);

    int counts[6] = {0, 0, 0, 0, 0, 0};
    for (int64_t c : *cls) {
        ASSERT_GE(c, 0);
        ASSERT_LE(c, 5);
        ++counts[c];
    }
    // Rectangle with two gable ends: 4 outline edges, 4 rake (wall) arcs from
    // the corners to the ridge ends, 1 ridge; no hips, valleys or cut rim.
    EXPECT_EQ(counts[kArcOutline], 4);
    EXPECT_EQ(counts[kArcConvex], 0);
    EXPECT_EQ(counts[kArcReflex], 0);
    EXPECT_EQ(counts[kArcRidge], 1);
    EXPECT_EQ(counts[kArcWall], 4);
    EXPECT_EQ(counts[kArcCut], 0);
}

TEST(BuildingRoofParity, EaveAnchorAndOutside) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;
    const IrVolume& vol = p.ir.volumes[0];
    const size_t n = vol.outline.size();

    // §7.2 base anchor: the slope plane passes through the source (unoffset)
    // outline at eave_y.
    for (size_t e = 0; e < n; ++e) {
        if (vol.roof.pitch_by_edge[e] >= 89.999) continue;
        const glm::dvec2& a = vol.outline[e];
        const glm::dvec2& b = vol.outline[(e + 1) % n];
        double y = 0.0;
        ASSERT_TRUE(roof_y_at(p.model, (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, y)) << "edge " << e;
        EXPECT_NEAR(y, vol.roof.eave_y, kTol) << "edge " << e;
    }
    // Beyond the overhang the roof does not reach.
    double y = 0.0;
    EXPECT_FALSE(roof_y_at(p.model, 0.0, 5.0, y));
    EXPECT_FALSE(roof_y_at(p.model, 5.5, 0.0, y));
}

TEST(BuildingRoofParity, RoofYAtMatchesPanels) {
    Parity p;
    std::string err;
    ASSERT_TRUE(setup(p, err)) << err;

    pgg::GeoPtr panels = outputGeo(p.run, "panels");
    ASSERT_NE(panels, nullptr);
    const std::vector<int64_t>* edgeId = column<int64_t>(*panels, pgg::Domain::Faces, "edge_id");
    ASSERT_NE(edgeId, nullptr);

    // Points strictly inside one slope face (z > 0: facade edge 1; z < 0:
    // back edge 3). y from the BTG query vs the PGG panel plane.
    const struct {
        double x, z;
        int64_t edge;
    } samples[] = {{-2.0, 1.5, 1}, {0.0, 2.0, 1}, {2.5, 3.0, 1}, {-1.0, -2.0, 3}, {3.0, -1.0, 3}};
    for (const auto& s : samples) {
        double yBtg = 0.0;
        ASSERT_TRUE(roof_y_at(p.model, s.x, s.z, yBtg)) << s.x << ", " << s.z;
        const int f = faceByEdge(*edgeId, s.edge);
        ASSERT_GE(f, 0);
        const glm::dvec3 nrm = panelNormal(*panels, static_cast<size_t>(f));
        const glm::dvec3 pt = panelPoint(*panels, static_cast<size_t>(f));
        ASSERT_GT(std::abs(nrm.y), 1e-6);
        const double yPgg = pt.y - (nrm.x * (s.x - pt.x) + nrm.z * (s.z - pt.z)) / nrm.y;
        EXPECT_NEAR(yBtg, yPgg, kTol) << s.x << ", " << s.z;
    }
}

}  // namespace
}  // namespace building_topology_generator
