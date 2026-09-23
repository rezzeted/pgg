#include "../../pch.h"

// §8 L1 roof_wavefront (architecture plan A4, spec v1.30): the weighted
// straight skeleton of the outline drives a roof over an arbitrary plan.
// Inputs: geo<points> outline (CCW seen from +Y, the lib/arch/plan
// convention), uniform pitch in degrees, optional per-vertex @pitch on the
// points (outgoing edge; 90 = vertical gable wall, plan speed 0), rise_max
// (cut height for mansard decks; 0 = no cut), y0 (eave height). Outputs:
//   panels — geo<mesh> slope faces (flat-shaded, per-face points) tagged as
//     the arch face-scope contract: @kind = K_SLOPE(7), @slope_id = source
//     edge, @slope_pitch, @eave_yaw (outward azimuth), @island_id, @rk = 0,
//     group roof;
//   edges — geo<points> in the §5.2 edge model (@p0/@p1/@len/@yaw/@out/@role/
//     @pitch/@y0/@y1/@tilt/@orient): outline edges as R_EAVE, skeleton arcs
//     classified R_RIDGE/R_HIP/R_VALLEY/R_RAKE;
//   top — geo<points> cut contour(s) at rise_max (@ring), empty when uncut.

#include <cmath>

#include "builtins.h"
#include "straight_skeleton.h"

namespace pgg {
namespace {

constexpr int32_t kKindSlope = 7;  // K_SLOPE (lib/arch/kinds.pgg)

float deg2rad(float d) { return d * 3.14159265358979323846f / 180.0f; }
float rad2deg(float r) { return r * 180.0f / 3.14159265358979323846f; }

float cross2(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

float signedArea2(const std::vector<glm::vec2>& ring) {
    float a = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) a += ring[i].x * ring[(i + 1) % ring.size()].y - ring[(i + 1) % ring.size()].x * ring[i].y;
    return a * 0.5f;
}

// One column of the §5.2 edge model for a batch of edges.
struct EdgeColumns {
    std::vector<glm::vec3> p;      // one point per edge (at p0)
    std::vector<glm::vec3> p0;
    std::vector<glm::vec3> p1;
    std::vector<float> len;
    std::vector<float> yaw;
    std::vector<glm::vec4> orient;
    std::vector<glm::vec3> out;
    std::vector<int64_t> role;
    std::vector<float> pitch;
    std::vector<float> y0;
    std::vector<float> y1;
    std::vector<float> tilt;

    void push(const glm::vec3& a, const glm::vec3& b, int32_t role_, const glm::vec3& outN, float pitch_) {
        const glm::vec3 d = b - a;
        const float dxz = std::sqrt(d.x * d.x + d.z * d.z);
        const float yawDeg = rad2deg(std::atan2(d.x, d.z));
        p.push_back(a);
        p0.push_back(a);
        p1.push_back(b);
        len.push_back(glm::length(d));
        yaw.push_back(yawDeg);
        // orient_from_euler((0, yaw, 0)): quat of the yaw rotation (x=0, z=0).
        const float h = deg2rad(yawDeg) * 0.5f;
        orient.push_back(glm::vec4(0.0f, std::sin(h), 0.0f, std::cos(h)));
        out.push_back(outN);
        role.push_back(role_);
        pitch.push_back(pitch_);
        y0.push_back(a.y);
        y1.push_back(b.y);
        tilt.push_back(rad2deg(std::atan2(d.y, dxz)));
    }
};

GeoPtr buildEdgeGeo(const EdgeColumns& ec) {
    GeoPtr base = makePoints(ec.p);
    AttrSet attrs;
    attrs.columns["p0"] = AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(ec.p0)), AttrTypeInfo::Point};
    attrs.columns["p1"] = AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(ec.p1)), AttrTypeInfo::Point};
    attrs.columns["len"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(ec.len))};
    attrs.columns["yaw"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(ec.yaw))};
    attrs.columns["orient"] = AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec4>>(ec.orient)), AttrTypeInfo::Quaternion};
    attrs.columns["out"] = AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(ec.out)), AttrTypeInfo::Vector};
    attrs.columns["role"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(ec.role))};
    attrs.columns["pitch"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(ec.pitch))};
    attrs.columns["y0"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(ec.y0))};
    attrs.columns["y1"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(ec.y1))};
    attrs.columns["tilt"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(ec.tilt))};
    return withAttrs(*base, Domain::Points, std::make_shared<const AttrSet>(std::move(attrs)));
}

Value fail(Span span, RunContext& run, const char* code, const std::string& msg, const char* hint) {
    run.report(code, span, msg, hint);
    auto elems = std::make_shared<std::vector<Value>>();
    elems->push_back(Value(makeMesh({}, {}, {0})));
    elems->push_back(Value(makePoints({})));
    elems->push_back(Value(makePoints({})));
    elems->push_back(Value(makePoints({})));
    return Value(ListValuePtr(elems));
}

Value opRoofWavefront(const BoundCall& bound, RunContext& run) {
    const Geo& outline = *asGeo(bound.values[0]);
    const float pitch = asF32(bound.values[1]);
    const float riseMax = asF32(bound.values[2]);
    const float y0 = asF32(bound.values[3]);

    const size_t n = outline.pointCount();
    if (n < 3) return fail(bound.span, run, "E612", "roof_wavefront: outline needs >= 3 points", "pass a closed plan contour");
    if (!(pitch > 0.0f) || pitch > 90.0f)
        return fail(bound.span, run, "E612", "roof_wavefront: pitch must be in (0, 90] degrees", "pitch = 90 is a vertical gable wall");
    if (riseMax < 0.0f)
        return fail(bound.span, run, "E612", "roof_wavefront: rise_max must be >= 0", "0 = no cut");

    // Optional per-vertex @pitch (outgoing edge), degrees.
    std::vector<float> pitchE(n, pitch);
    if (const AttrSet* pa = outline.attrs(Domain::Points)) {
        if (const AttrColumn* col = pa->find("pitch")) {
            if (const auto* buf = std::get_if<std::shared_ptr<const std::vector<float>>>(&col->data)) {
                if ((*buf)->size() == n)
                    for (size_t i = 0; i < n; ++i) pitchE[i] = (**buf)[i];
            }
        }
    }

    SkeletonInput in;
    in.outline.resize(n);
    in.speed.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3& q = (*outline.positions)[i];
        in.outline[i] = glm::vec2(q.x, q.z);
        const float p = pitchE[i];
        if (!(p > 0.0f) || p > 90.0f)
            return fail(bound.span, run, "E612", "roof_wavefront: @pitch on the outline must be in (0, 90] degrees",
                        "pitch = 90 is a vertical gable wall");
        in.speed[i] = p >= 89.999f ? 0.0f : 1.0f / std::tan(deg2rad(p));
    }
    if (signedArea2(in.outline) >= 0.0f)
        return fail(bound.span, run, "E612", "roof_wavefront: outline is not CCW seen from above (signed area >= 0)",
                    "reverse the vertex order (lib/arch/plan convention)");
    {
        bool anySpeed = false;
        for (float s : in.speed) anySpeed = anySpeed || s > 0.0f;
        if (!anySpeed)
            return fail(bound.span, run, "E612", "roof_wavefront: every edge is a gable wall (pitch 90) — nothing to sweep",
                        "leave at least one edge with pitch < 90");
    }

    // overhang: offset every edge line outward, re-intersect neighbours. The
    // polygon structure (vertex/edge count, winding, speeds) is preserved, so
    // slope_id still matches the source edge index.
    const float overhang = asF32(bound.values[4]);
    if (overhang < 0.0f)
        return fail(bound.span, run, "E612", "roof_wavefront: overhang must be >= 0", "0 = eaves on the wall line");
    if (overhang > 0.0f) in.outline = offsetOutline(in.outline, overhang);

    const StraightSkeleton sk = buildStraightSkeleton(in, riseMax);
    if (sk.nodes.size() == n) {
        return fail(bound.span, run, "E612", "roof_wavefront: the skeleton made no progress (self-intersecting outline?)",
                    "check the contour for self-intersections or duplicate vertices");
    }

    // --- panels: one flat-shaded face per source edge, per-face points.
    std::vector<glm::vec3> pos;
    std::vector<glm::vec3> nrm;
    std::vector<int32_t> cornerVerts;
    std::vector<int32_t> faceOffsets{0};
    std::vector<int64_t> fKind, fSlopeId, fIsland, fRk, fVariant;
    std::vector<float> fPitch, fEaveYaw;
    auto nodePos = [&](int32_t idx) {
        const SkeletonNode& nd = sk.nodes[static_cast<size_t>(idx)];
        return glm::vec3(nd.p.x, y0 + nd.t, nd.p.y);
    };
    // Polygon of a face, wound so its Newell normal points outward from the
    // roof (up for slopes, out of the gable wall for vertical panels).
    auto orientedPoly = [&](const SkeletonFace& f) {
        std::vector<glm::vec3> poly;
        for (int32_t idx : f.nodes) poly.push_back(nodePos(idx));
        // Two anti-parallel edges that met on a ridge below rise_max stay in the
        // front as a zero-width sliver up to the cut: their slope faces get a
        // vertical step (ridge node -> cut node at the same plan point). The
        // step top is not on the slope plane (the fan-triangulated panel would
        // bulge over the coverage) — drop it; the ridge node closes the face.
        if (pitchE[static_cast<size_t>(f.edge)] < 89.999f) {
            bool changed = true;
            while (changed && poly.size() > 3) {
                changed = false;
                for (size_t k = 0; k < poly.size(); ++k) {
                    const glm::vec3& c = poly[k];
                    const glm::vec3& pv = poly[(k + poly.size() - 1) % poly.size()];
                    const glm::vec3& nx = poly[(k + 1) % poly.size()];
                    auto stepOver = [&](const glm::vec3& o) {
                        const float dx = c.x - o.x, dz = c.z - o.z;
                        return dx * dx + dz * dz < 1e-8f && c.y > o.y + 1e-4f;
                    };
                    if (stepOver(pv) || stepOver(nx)) {
                        poly.erase(poly.begin() + static_cast<std::ptrdiff_t>(k));
                        changed = true;
                        break;
                    }
                }
            }
        }
        glm::vec3 pn(0.0f);
        for (size_t k = 0; k < poly.size(); ++k) {
            const glm::vec3& a = poly[k];
            const glm::vec3& b = poly[(k + 1) % poly.size()];
            pn += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
        }
        glm::vec3 expect(0, 1, 0);
        if (pitchE[static_cast<size_t>(f.edge)] >= 89.999f) {
            const glm::vec2 d = in.outline[static_cast<size_t>((f.edge + 1) % n)] - in.outline[static_cast<size_t>(f.edge)];
            const float len = glm::length(d);
            if (len > 0.0f) expect = glm::vec3(-d.y / len, 0.0f, d.x / len);  // outward from the gable wall
        }
        if (glm::dot(pn, expect) < 0.0f) std::reverse(poly.begin(), poly.end());
        return poly;
    };
    for (const SkeletonFace& f : sk.faces) {
        const int e = f.edge;
        const glm::vec2 d = in.outline[static_cast<size_t>((e + 1) % n)] - in.outline[static_cast<size_t>(e)];
        const float outYaw = rad2deg(std::atan2(-d.y, d.x));  // outward azimuth (lib/arch/plan formula)
        std::vector<int32_t> faceIdx;
        glm::vec3 fn(0.0f);
        std::vector<glm::vec3> poly = orientedPoly(f);
        for (size_t k = 0; k < poly.size(); ++k) {
            const glm::vec3& a = poly[k];
            const glm::vec3& b = poly[(k + 1) % poly.size()];
            fn += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));  // Newell
            faceIdx.push_back(static_cast<int32_t>(pos.size()));
            pos.push_back(a);
        }
        if (glm::length(fn) < 1e-9f) fn = glm::vec3(0, 1, 0);
        fn = glm::normalize(fn);
        for (size_t k = 0; k < faceIdx.size(); ++k) nrm.push_back(fn);
        for (int32_t ci : faceIdx) cornerVerts.push_back(ci);
        faceOffsets.push_back(static_cast<int32_t>(cornerVerts.size()));
        fKind.push_back(kKindSlope);
        fSlopeId.push_back(e);
        fPitch.push_back(pitchE[static_cast<size_t>(e)]);
        fEaveYaw.push_back(outYaw);
        fIsland.push_back(e);
        fRk.push_back(0);
        fVariant.push_back(0);
    }
    GeoPtr panels = makeMesh(std::move(pos), std::move(cornerVerts), std::move(faceOffsets));
    panels = withNormals(*panels, std::move(nrm));
    {
        AttrSet fa;
        fa.columns["kind"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(fKind))};
        fa.columns["slope_id"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(fSlopeId))};
        fa.columns["slope_pitch"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(fPitch))};
        fa.columns["eave_yaw"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(fEaveYaw))};
        fa.columns["island_id"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(fIsland))};
        fa.columns["rk"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(fRk))};
        fa.columns["variant"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(fVariant))};
        panels = withAttrs(*panels, Domain::Faces, std::make_shared<const AttrSet>(std::move(fa)));
        GroupSet fg;
        fg.columns["roof"] = std::make_shared<const BoolColumn>(panels->faceCount(), uint8_t(1));
        panels = withGroups(*panels, Domain::Faces, std::make_shared<const GroupSet>(std::move(fg)));
    }

    // --- planes: per panel edge a boundary half-plane in the panel's own plane
    // (o on the edge, n = outward in-plane normal), the eave edge carrying the
    // tile frame (o = eave midpoint, len = eave length, w_len = slope depth).
    // A skeleton face may be non-convex (a main slope widening behind a
    // valley): it is ear-clipped into convex parts, each part a closed set of
    // rows (@part); the diagonals between parts are cls = 2 (no margin). An
    // anchor is inside the panel when it is inside any of its parts. Convex
    // faces are one part (0) — the analytic roofs' row model (cls = 0).
    struct PlaneRow {
        glm::vec3 o;
        glm::vec3 n;
        int64_t island;
        uint8_t eave;
        float len;
        float wLen;
        int64_t part;
        float cls;
    };
    std::vector<PlaneRow> planeRows;
    {
        for (const SkeletonFace& f : sk.faces) {
            const glm::vec3 v0 = nodePos(f.nodes[0]);
            const glm::vec3 v1 = nodePos(f.nodes[1]);
            const std::vector<glm::vec3> poly = orientedPoly(f);
            const size_t fn = poly.size();
            glm::vec3 pn(0.0f);
            for (size_t k = 0; k < fn; ++k) {
                const glm::vec3& a = poly[k];
                const glm::vec3& b = poly[(k + 1) % fn];
                pn += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
            }
            pn = glm::length(pn) < 1e-9f ? glm::vec3(0, 1, 0) : glm::normalize(pn);
            auto isEaveEdge = [&](const glm::vec3& a, const glm::vec3& b) {
                return (glm::length(a - v0) < 1e-4f && glm::length(b - v1) < 1e-4f) ||
                       (glm::length(a - v1) < 1e-4f && glm::length(b - v0) < 1e-4f);
            };
            // The polygon is wound CCW around pn: cross(b - a, pn) points out of it.
            auto pushRow = [&](size_t ia, size_t ib, int64_t part, bool diagonal) {
                const glm::vec3& a = poly[ia];
                const glm::vec3& b = poly[ib];
                if (glm::length(b - a) < 1e-6f && !isEaveEdge(a, b)) return;
                PlaneRow pr;
                pr.o = 0.5f * (a + b);
                pr.island = f.edge;
                pr.part = part;
                pr.eave = !diagonal && isEaveEdge(a, b) ? 1 : 0;
                pr.cls = diagonal ? 2.0f : 0.0f;
                if (pr.eave) {
                    // Vertical plane through the eave edge, outward horizontal.
                    const glm::vec2 d = in.outline[static_cast<size_t>((f.edge + 1) % n)] - in.outline[static_cast<size_t>(f.edge)];
                    const float len = glm::length(d);
                    pr.n = len > 0.0f ? glm::vec3(-d.y / len, 0.0f, d.x / len) : glm::vec3(0, 0, 1);
                } else {
                    const glm::vec3 c = glm::cross(b - a, pn);
                    pr.n = glm::length(c) < 1e-9f ? glm::vec3(0, 1, 0) : glm::normalize(c);
                }
                pr.len = pr.eave ? glm::length(v1 - v0) : 0.0f;
                pr.wLen = 0.0f;
                planeRows.push_back(pr);
            };
            // In-plane 2D frame (CCW preserved: e2 = pn x e1).
            glm::vec3 e1 = poly[fn > 1 ? 1 : 0] - poly[0];
            e1 = glm::length(e1) < 1e-9f ? glm::vec3(1, 0, 0) : glm::normalize(e1);
            const glm::vec3 e2 = glm::cross(pn, e1);
            std::vector<glm::vec2> q(fn);
            for (size_t k = 0; k < fn; ++k) q[k] = glm::vec2(glm::dot(poly[k] - poly[0], e1), glm::dot(poly[k] - poly[0], e2));
            auto turn = [&](size_t a, size_t b, size_t c) { return cross2(q[b] - q[a], q[c] - q[b]); };
            bool convex = true;
            for (size_t k = 0; k < fn && convex; ++k)
                if (turn((k + fn - 1) % fn, k, (k + 1) % fn) < -1e-5f) convex = false;
            const size_t rowsBefore = planeRows.size();
            if (convex || fn < 4) {
                for (size_t k = 0; k < fn; ++k) pushRow(k, (k + 1) % fn, 0, false);
            } else {
                // Ear clipping on the index ring; every ear is one convex part.
                std::vector<size_t> ring(fn);
                for (size_t k = 0; k < fn; ++k) ring[k] = k;
                auto isPolyEdge = [&](size_t a, size_t b) { return (a + 1) % fn == b; };
                auto inTri = [&](const glm::vec2& p, size_t a, size_t b, size_t c) {
                    return cross2(q[b] - q[a], p - q[a]) >= -1e-6f && cross2(q[c] - q[b], p - q[b]) >= -1e-6f &&
                           cross2(q[a] - q[c], p - q[c]) >= -1e-6f;
                };
                int64_t part = 0;
                size_t guard = 0;
                while (ring.size() > 3 && guard++ < fn * fn) {
                    bool clipped = false;
                    for (size_t k = 0; k < ring.size(); ++k) {
                        const size_t ia = ring[(k + ring.size() - 1) % ring.size()];
                        const size_t ib = ring[k];
                        const size_t ic = ring[(k + 1) % ring.size()];
                        if (turn(ia, ib, ic) <= 1e-6f) continue;
                        bool empty = true;
                        for (size_t m : ring)
                            if (m != ia && m != ib && m != ic && inTri(q[m], ia, ib, ic)) {
                                empty = false;
                                break;
                            }
                        if (!empty) continue;
                        pushRow(ia, ib, part, !isPolyEdge(ia, ib));
                        pushRow(ib, ic, part, !isPolyEdge(ib, ic));
                        pushRow(ic, ia, part, true);
                        ++part;
                        ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(k));
                        clipped = true;
                        break;
                    }
                    if (!clipped) break;
                }
                const size_t m = ring.size();
                for (size_t k = 0; k < m; ++k) {
                    const size_t ia = ring[k];
                    const size_t ib = ring[(k + 1) % m];
                    pushRow(ia, ib, part, !isPolyEdge(ia, ib));
                }
            }
            // Slope depth for the eave frame: farthest in-plane distance from
            // the eave line measured along the panel normal's up-slope axis.
            {
                const glm::vec3 mid = 0.5f * (v0 + v1);
                glm::vec3 edgeDir3 = v1 - v0;
                const float elen = glm::length(edgeDir3);
                if (elen > 1e-9f) edgeDir3 /= elen;
                glm::vec3 W = glm::cross(pn, edgeDir3);  // up-slope, into the panel
                const glm::vec3 some = nodePos(f.nodes[f.nodes.size() > 2 ? 2 : 0]);
                if (glm::dot(W, some - mid) < 0.0f) W = -W;
                float wMax = 0.0f;
                for (const glm::vec3& qq : poly) wMax = std::max(wMax, glm::dot(qq - mid, W));
                for (size_t k = rowsBefore; k < planeRows.size(); ++k)
                    if (planeRows[k].eave) planeRows[k].wLen = wMax;
            }
        }
    }
    GeoPtr planes = makePoints({});
    {
        std::vector<glm::vec3> ppos;
        std::vector<glm::vec3> po, pn2;
        std::vector<int64_t> pis, ppart;
        std::vector<uint8_t> pev;
        std::vector<float> plen, pwlen, pcls;
        for (const PlaneRow& pr : planeRows) {
            ppos.push_back(pr.o);
            po.push_back(pr.o);
            pn2.push_back(pr.n);
            pis.push_back(pr.island);
            pev.push_back(pr.eave);
            plen.push_back(pr.len);
            pwlen.push_back(pr.wLen);
            pcls.push_back(pr.cls);
            ppart.push_back(pr.part);
        }
        planes = makePoints(std::move(ppos));
        AttrSet pa;
        pa.columns["o"] = AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(po)), AttrTypeInfo::Point};
        pa.columns["n"] = AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(pn2)), AttrTypeInfo::Vector};
        pa.columns["island_id"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(pis))};
        pa.columns["eave"] = AttrColumn{ColumnData(std::make_shared<const std::vector<uint8_t>>(pev))};
        pa.columns["len"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(plen))};
        pa.columns["w_len"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(pwlen))};
        pa.columns["cls"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(pcls))};
        pa.columns["part"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(ppart))};
        planes = withAttrs(*planes, Domain::Points, std::make_shared<const AttrSet>(std::move(pa)));
    }

    // --- edges: outline as R_EAVE, skeleton arcs classified.
    EdgeColumns ec;
    for (size_t e = 0; e < n; ++e) {
        const glm::vec2 a = in.outline[e];
        const glm::vec2 b = in.outline[(e + 1) % n];
        const glm::vec2 d = b - a;
        const float len = glm::length(d);
        const glm::vec3 outN = len > 0.0f ? glm::vec3(d.y / len, 0.0f, -d.x / len) : glm::vec3(0, 0, 1);
        ec.push(glm::vec3(a.x, y0, a.y), glm::vec3(b.x, y0, b.y), 3 /*R_EAVE*/, outN, pitchE[e]);
    }
    for (const SkeletonArc& arc : sk.arcs) {
        const SkeletonNode& na = sk.nodes[static_cast<size_t>(arc.a)];
        const SkeletonNode& nb = sk.nodes[static_cast<size_t>(arc.b)];
        const glm::vec3 pa(na.p.x, y0 + na.t, na.p.y);
        const glm::vec3 pb(nb.p.x, y0 + nb.t, nb.p.y);
        if (arc.leftEdge < 0 || arc.rightEdge < 0) {
            // Cut-contour arc (rise_max): the deck rim — R_FLOOR_TOP, like the
            // mansard break edges of the analytic roofs.
            const int e = arc.leftEdge >= 0 ? arc.leftEdge : arc.rightEdge;
            if (e < 0) continue;  // defensive: cut arcs always carry one source edge
            const glm::vec2 d = in.outline[static_cast<size_t>((e + 1) % n)] - in.outline[static_cast<size_t>(e)];
            const float len = glm::length(d);
            const glm::vec3 outN = len > 0.0f ? glm::vec3(d.y / len, 0.0f, -d.x / len) : glm::vec3(0, 1, 0);
            ec.push(pa, pb, 5 /*R_FLOOR_TOP*/, outN, 0.0f);
            continue;
        }
        const bool leftGable = pitchE[static_cast<size_t>(arc.leftEdge)] >= 89.999f;
        const bool rightGable = pitchE[static_cast<size_t>(arc.rightEdge)] >= 89.999f;
        // The ridge-to-cut sliver between two slopes (see orientedPoly): no edge.
        if (!leftGable && !rightGable) {
            const float dx = pb.x - pa.x, dz = pb.z - pa.z;
            if (dx * dx + dz * dz < 1e-8f) continue;
        }
        int32_t role;
        glm::vec3 outN(0, 1, 0);
        if (leftGable || rightGable) {
            role = 4;  // R_RAKE
            const int g = leftGable ? arc.leftEdge : arc.rightEdge;
            const glm::vec2 gd = in.outline[static_cast<size_t>((g + 1) % n)] - in.outline[static_cast<size_t>(g)];
            const float gl = glm::length(gd);
            if (gl > 0.0f) outN = glm::vec3(gd.y / gl, 0.0f, -gd.x / gl);
        } else {
            // Adjacent source edges (either order): the bisector of their
            // shared vertex. The vertex index is the edge STARTING at it.
            int vertex = -1;
            if ((arc.leftEdge + 1) % static_cast<int32_t>(n) == arc.rightEdge) vertex = arc.rightEdge;
            else if ((arc.rightEdge + 1) % static_cast<int32_t>(n) == arc.leftEdge) vertex = arc.leftEdge;
            if (vertex >= 0) {
                const glm::vec2 dL = in.outline[static_cast<size_t>(vertex)] - in.outline[static_cast<size_t>((vertex + n - 1) % n)];
                const glm::vec2 dR = in.outline[static_cast<size_t>((vertex + 1) % n)] - in.outline[static_cast<size_t>(vertex)];
                const bool convex = dL.x * dR.y - dL.y * dR.x < 0.0f;
                role = convex ? 1 /*R_HIP*/ : 2 /*R_VALLEY*/;
                const glm::vec2 oL = glm::vec2(dL.y, -dL.x) / std::max(glm::length(dL), 1e-9f);
                const glm::vec2 oR = glm::vec2(dR.y, -dR.x) / std::max(glm::length(dR), 1e-9f);
                const glm::vec2 om = -(oL + oR);
                if (glm::length(om) > 1e-9f) outN = glm::normalize(glm::vec3(om.x, 0.0f, om.y));
            } else {
                role = 0;  // R_RIDGE
            }
        }
        ec.push(pa, pb, role, outN, pitchE[static_cast<size_t>(arc.leftEdge)]);
    }
    GeoPtr edges = buildEdgeGeo(ec);

    // --- top: cut contour(s) at rise_max (empty when uncut).
    std::vector<glm::vec3> topPos;
    std::vector<int64_t> topRing;
    for (size_t r = 0; r < sk.topRings.size(); ++r)
        for (int32_t idx : sk.topRings[r]) {
            topPos.push_back(nodePos(idx));
            topRing.push_back(static_cast<int64_t>(r));
        }
    GeoPtr top = makePoints(std::move(topPos));
    {
        AttrSet ta;
        ta.columns["ring"] = AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(topRing))};
        top = withAttrs(*top, Domain::Points, std::make_shared<const AttrSet>(std::move(ta)));
    }

    auto elems = std::make_shared<std::vector<Value>>();
    elems->push_back(Value(std::move(panels)));
    elems->push_back(Value(std::move(edges)));
    elems->push_back(Value(std::move(top)));
    elems->push_back(Value(std::move(planes)));
    return Value(ListValuePtr(elems));
}

}  // namespace

Value evalRoofBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::RoofWavefront: return opRoofWavefront(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
