#include "pch.h"

#include "roof.h"

namespace building_topology_generator {

namespace {

constexpr double kGablePitch = 89.999;

double signed_area(const std::vector<glm::dvec2>& ring) {
    double a = 0.0;
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::dvec2& p = ring[i];
        const glm::dvec2& q = ring[(i + 1) % ring.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

}  // namespace

// Shared 2D helper (roof faces, plan cell fields): even-odd rule, horizontal
// ray toward +x.
bool point_in_poly(const std::vector<glm::dvec2>& poly, double x, double z) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const double xi = poly[i].x, zi = poly[i].y;
        const double xj = poly[j].x, zj = poly[j].y;
        if ((zi > z) != (zj > z)) {
            const double xat = xj + (xi - xj) * (z - zj) / (zi - zj);
            if (x < xat) inside = !inside;
        }
    }
    return inside;
}

namespace {

glm::dvec2 face_plan_point(const geom2d::StraightSkeleton& sk, int32_t node) {
    return glm::dvec2(sk.nodes[static_cast<size_t>(node)].p);
}

double dist_point_segment_2d(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b, glm::dvec2& closest) {
    const glm::dvec2 d = b - a;
    const double len2 = glm::dot(d, d);
    double t = 0.0;
    if (len2 > 1e-12) t = glm::clamp(glm::dot(p - a, d) / len2, 0.0, 1.0);
    closest = a + t * d;
    return glm::length(p - closest);
}

}  // namespace

bool build_roof_model(const IrVolume& vol, RoofModel& out, std::string& err) {
    const size_t n = vol.outline.size();
    if (n < 3) {
        err = "volume " + vol.id + ": outline needs >= 3 points";
        return false;
    }
    if (vol.roof.pitch_by_edge.size() != n) {
        err = "volume " + vol.id + ": pitch_by_edge size must match the outline";
        return false;
    }
    if (signed_area(vol.outline) >= 0.0) {
        err = "volume " + vol.id + ": outline is not CCW seen from above";
        return false;
    }

    // v1: one pitch for all sloped edges; gable edges are exactly 90.
    double pitch = 0.0;
    bool have_slope = false;
    for (size_t e = 0; e < n; ++e) {
        const double p = vol.roof.pitch_by_edge[e];
        if (p <= 0.0 || p > 90.0) {
            err = "volume " + vol.id + ": pitch must be in (0, 90] degrees";
            return false;
        }
        if (p < kGablePitch) {
            if (have_slope && std::abs(p - pitch) > 1e-6) {
                err = "volume " + vol.id +
                      ": mixed slope pitches need the per-edge base anchor, which is outside B0 (requirements §7.2)";
                return false;
            }
            pitch = p;
            have_slope = true;
        }
    }
    if (!have_slope) {
        err = "volume " + vol.id + ": every edge is a gable (pitch 90) — nothing to sweep";
        return false;
    }

    RoofModel m;
    m.volume_id = vol.id;
    m.outline = vol.outline;
    m.pitch_deg = vol.roof.pitch_by_edge;
    m.eave_y = vol.roof.eave_y;
    m.overhang = vol.roof.overhang;
    m.roof_t = vol.roof.roof_t;
    m.y0 = vol.roof.eave_y - vol.roof.overhang * std::tan(pitch * M_PI / 180.0);

    geom2d::SkeletonInput in;
    in.outline.reserve(n);
    in.speed.reserve(n);
    for (size_t e = 0; e < n; ++e) {
        const glm::dvec2& p = vol.outline[e];
        in.outline.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
        const double pd = vol.roof.pitch_by_edge[e];
        in.speed.push_back(pd >= kGablePitch ? 0.0f
                                             : static_cast<float>(1.0 / std::tan(pd * M_PI / 180.0)));
    }
    if (vol.roof.overhang > 0.0) in.outline = geom2d::offsetOutline(in.outline, static_cast<float>(vol.roof.overhang));
    m.sweep_outline.reserve(in.outline.size());
    for (const glm::vec2& p : in.outline) m.sweep_outline.emplace_back(p.x, p.y);

    m.sk = geom2d::buildStraightSkeleton(in, 0.0f);
    if (m.sk.nodes.empty()) {
        err = "volume " + vol.id + ": the skeleton made no progress (self-intersecting outline?)";
        return false;
    }

    // Planes: one per sloped skeleton face. The face polygon starts with the
    // two outline vertices of its source edge (t = 0), so the plane passes
    // through the offset edge line at y0 and rises inward at tan(pitch).
    for (const geom2d::SkeletonFace& f : m.sk.faces) {
        const int e = f.edge;
        if (e < 0 || static_cast<size_t>(e) >= n) continue;
        if (vol.roof.pitch_by_edge[static_cast<size_t>(e)] >= kGablePitch) continue;
        IrRoofPlane pl;
        pl.volume = vol.id;
        pl.edge = e;
        pl.pitch = vol.roof.pitch_by_edge[static_cast<size_t>(e)];
        const glm::dvec2 a = m.sweep_outline[static_cast<size_t>(e)];
        const glm::dvec2 b = m.sweep_outline[(static_cast<size_t>(e) + 1) % n];
        pl.o = glm::dvec3((a.x + b.x) * 0.5, m.y0, (a.y + b.y) * 0.5);
        // Upward normal from 3 non-collinear 3D points of the face.
        glm::dvec3 p0(pl.o);
        glm::dvec3 p1(b.x, m.y0, b.y);
        glm::dvec3 p2 = p0;
        for (size_t k = 2; k < f.nodes.size(); ++k) {
            const geom2d::SkeletonNode& nd = m.sk.nodes[static_cast<size_t>(f.nodes[k])];
            p2 = glm::dvec3(nd.p.x, m.y0 + nd.t, nd.p.y);
            if (glm::length(glm::cross(p1 - p0, p2 - p0)) > 1e-9) break;
        }
        glm::dvec3 nn = glm::cross(p1 - p0, p2 - p0);
        if (glm::length(nn) < 1e-12) continue;
        nn = glm::normalize(nn);
        if (nn.y < 0.0) nn = -nn;
        pl.n = nn;
        m.planes.push_back(pl);
    }

    // Arc classification mirrors the PGG builtin (builtins_skeleton.cpp):
    // outline arcs are not part of sk.arcs; skeleton arcs between adjacent
    // source edges are vertex bisectors (convex = hip, reflex = valley),
    // arcs between non-adjacent edges are ridges; arcs bounding a gable face
    // are wall lines and are not exported here.
    const int ni = static_cast<int>(n);
    for (const geom2d::SkeletonArc& arc : m.sk.arcs) {
        if (arc.leftEdge < 0 || arc.rightEdge < 0) continue;  // cut rim (tMax): not used in v1
        const double pl_pitch = vol.roof.pitch_by_edge[static_cast<size_t>(arc.leftEdge)];
        const double pr_pitch = vol.roof.pitch_by_edge[static_cast<size_t>(arc.rightEdge)];
        if (pl_pitch >= kGablePitch || pr_pitch >= kGablePitch) continue;  // wall arc
        const geom2d::SkeletonNode& na = m.sk.nodes[static_cast<size_t>(arc.a)];
        const geom2d::SkeletonNode& nb = m.sk.nodes[static_cast<size_t>(arc.b)];
        IrRoofEdge e;
        e.a = glm::dvec3(na.p.x, m.y0 + na.t, na.p.y);
        e.b = glm::dvec3(nb.p.x, m.y0 + nb.t, nb.p.y);
        int vertex = -1;
        if ((arc.leftEdge + 1) % ni == arc.rightEdge) vertex = arc.rightEdge;
        else if ((arc.rightEdge + 1) % ni == arc.leftEdge) vertex = arc.leftEdge;
        if (vertex < 0) {
            m.ridges.push_back(e);
            continue;
        }
        const glm::dvec2 dL = vol.outline[static_cast<size_t>(vertex)] -
                              vol.outline[static_cast<size_t>((vertex + ni - 1) % ni)];
        const glm::dvec2 dR = vol.outline[static_cast<size_t>((vertex + 1) % ni)] -
                              vol.outline[static_cast<size_t>(vertex)];
        const bool convex = dL.x * dR.y - dL.y * dR.x < 0.0;
        (convex ? m.hips : m.valleys).push_back(e);
    }

    out = std::move(m);
    return true;
}

bool roof_y_at(const RoofModel& m, double x, double z, double& y) {
    const glm::dvec2 p(x, z);
    const size_t n = m.sweep_outline.size();
    for (const geom2d::SkeletonFace& f : m.sk.faces) {
        const int e = f.edge;
        if (e < 0 || static_cast<size_t>(e) >= n) continue;
        if (m.pitch_deg[static_cast<size_t>(e)] >= kGablePitch) continue;
        std::vector<glm::dvec2> poly;
        poly.reserve(f.nodes.size());
        for (int32_t idx : f.nodes) poly.push_back(face_plan_point(m.sk, idx));
        if (!point_in_poly(poly, x, z)) continue;
        // Height = y0 + inward plan distance to the source edge line * tan(pitch).
        const glm::dvec2 a = m.sweep_outline[static_cast<size_t>(e)];
        const glm::dvec2 b = m.sweep_outline[(static_cast<size_t>(e) + 1) % n];
        const glm::dvec2 d = glm::normalize(b - a);
        glm::dvec2 nrm(-d.y, d.x);
        // Orient the normal toward the face interior (a skeleton node of it).
        if (f.nodes.size() > 2 && glm::dot(face_plan_point(m.sk, f.nodes[2]) - a, nrm) < 0.0) nrm = -nrm;
        const double dist = glm::dot(p - a, nrm);
        y = m.y0 + dist * std::tan(m.pitch_deg[static_cast<size_t>(e)] * M_PI / 180.0);
        return true;
    }
    return false;
}

bool ceiling_y_at(const RoofModel& m, double x, double z, double& y) {
    if (!roof_y_at(m, x, z, y)) return false;
    for (const geom2d::SkeletonFace& f : m.sk.faces) {
        const int e = f.edge;
        if (e < 0 || static_cast<size_t>(e) >= m.sweep_outline.size()) continue;
        if (m.pitch_deg[static_cast<size_t>(e)] >= kGablePitch) continue;
        std::vector<glm::dvec2> poly;
        poly.reserve(f.nodes.size());
        for (int32_t idx : f.nodes) poly.push_back(face_plan_point(m.sk, idx));
        if (!point_in_poly(poly, x, z)) continue;
        const double pitch = m.pitch_deg[static_cast<size_t>(e)] * M_PI / 180.0;
        y -= m.roof_t / std::cos(pitch);
        return true;
    }
    return false;
}

double chimney_top_required(double d, double ridge_y, double exit_y, double h_above_roof) {
    double req;
    if (d <= 1.5) req = ridge_y + 0.5;
    else if (d <= 3.0) req = ridge_y;
    else req = ridge_y - d * std::tan(10.0 * M_PI / 180.0);
    return std::max(req, exit_y + h_above_roof);
}

bool nearest_ridge(const RoofModel& m, double x, double z, double& dist, double& ridge_y) {
    if (m.ridges.empty()) return false;
    const glm::dvec2 p(x, z);
    double best = std::numeric_limits<double>::max();
    double best_y = 0.0;
    for (const IrRoofEdge& r : m.ridges) {
        glm::dvec2 closest;
        const double d = dist_point_segment_2d(p, glm::dvec2(r.a.x, r.a.z), glm::dvec2(r.b.x, r.b.z), closest);
        if (d < best) {
            // Height at the closest point: interpolate along the segment.
            const glm::dvec2 ab(r.b.x - r.a.x, r.b.z - r.a.z);
            const double len2 = glm::dot(ab, ab);
            double t = 0.0;
            if (len2 > 1e-12) t = glm::clamp(glm::dot(p - glm::dvec2(r.a.x, r.a.z), ab) / len2, 0.0, 1.0);
            best = d;
            best_y = r.a.y + t * (r.b.y - r.a.y);
        }
    }
    dist = best;
    ridge_y = best_y;
    return true;
}

}  // namespace building_topology_generator
