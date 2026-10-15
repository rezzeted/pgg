#include "pch.h"

#include "unit.h"

#include "roof.h"
#include "straight_skeleton.h"

namespace building_geometry_generator {

namespace btg = building_topology_generator;

namespace {

std::string fmt_num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

// R_y(yaw): local -> world (village rot_y convention).
glm::dvec3 rot_y(const glm::dvec3& p, double c, double s) {
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

}  // namespace

pgg::GeoPtr PointsBuilder::build() const {
    auto g = pgg::makePoints(positions);
    pgg::AttrSet attrs;
    for (const auto& [name, values] : f32_attrs) {
        pgg::AttrColumn col;
        col.typeInfo = pgg::AttrTypeInfo::None;
        col.data = std::make_shared<const std::vector<float>>(values);
        attrs.columns[name] = std::move(col);
    }
    for (const auto& [name, vt] : vec3_attrs) {
        pgg::AttrColumn col;
        col.typeInfo = vt.second;
        col.data = std::make_shared<const std::vector<glm::vec3>>(vt.first);
        attrs.columns[name] = std::move(col);
    }
    return pgg::withAttrs(*g, pgg::Domain::Points,
                          std::make_shared<const pgg::AttrSet>(std::move(attrs)));
}

namespace {

std::string ternary_chain(size_t n, const std::function<std::string(size_t)>& elem) {
    std::ostringstream out;
    for (size_t i = 0; i < n; ++i) {
        if (i + 1 == n) {
            out << elem(i);
        } else {
            out << "@index == " << i << " ? " << elem(i) << " : (";
        }
    }
    for (size_t i = 0; i + 1 < n; ++i) out << ")";
    return out.str();
}

}  // namespace

std::string PointsBuilder::emit_to(const std::string& prefix, std::ostringstream& out) const {
    if (positions.empty()) {
        out << prefix << " = empty_points()\n";
        return prefix;
    }
    out << prefix << " = set_position(mesh_line(count = " << positions.size()
        << ", length = 0.0), pos = "
        << ternary_chain(positions.size(), [&](size_t i) {
               const glm::vec3& p = positions[i];
               return "vec3(" + fmt_num(p.x) + ", " + fmt_num(p.y) + ", " + fmt_num(p.z) + ")";
           })
        << ")\n";
    std::string cur = prefix;
    int anon = 0;
    for (const auto& [name, values] : f32_attrs) {
        const std::string next = prefix + "_a" + std::to_string(anon++);
        out << next << " = set(" << cur << ", \"" << name << "\", "
            << ternary_chain(values.size(), [&](size_t i) { return fmt_num(values[i]); })
            << ", domain = points)\n";
        cur = next;
    }
    for (const auto& [name, vt] : vec3_attrs) {
        const std::string next = prefix + "_a" + std::to_string(anon++);
        const char* info = vt.second == pgg::AttrTypeInfo::Point    ? "point"
                           : vt.second == pgg::AttrTypeInfo::Normal ? "normal"
                           : vt.second == pgg::AttrTypeInfo::Vector  ? "vector"
                                                                     : "none";
        out << next << " = set(" << cur << ", \"" << name << "\", "
            << ternary_chain(vt.first.size(),
                             [&](size_t i) {
                                 const glm::vec3& p = vt.first[i];
                                 return "vec3(" + fmt_num(p.x) + ", " + fmt_num(p.y) + ", " +
                                        fmt_num(p.z) + ")";
                             })
            << ", domain = points, typeinfo = " << info << ")\n";
        cur = next;
    }
    return cur;
}

glm::dvec2 plan_dir(const glm::dvec2& a, const glm::dvec2& b) {
    const glm::dvec2 d = b - a;
    const double l = glm::length(d);
    return l > 0.0 ? d / l : glm::dvec2(1.0, 0.0);
}

glm::dvec2 plan_left(const glm::dvec2& d) { return {-d.y, d.x}; }

double yaw_for_axis_x(const glm::dvec2& d) {
    // rot_y(yaw) * (1, 0, 0) = (cos, 0, -sin) = (d.x, 0, d.z)
    return std::atan2(-d.y, d.x) * 180.0 / 3.14159265358979323846;
}

double yaw_for_axis_z(const glm::dvec2& d) {
    // rot_y(yaw) * (0, 0, 1) = (sin, 0, cos) = (d.x, 0, d.z)
    return std::atan2(d.x, d.y) * 180.0 / 3.14159265358979323846;
}

const btg::IrStorey* find_storey(const btg::Ir& ir, const std::string& id) {
    for (const auto& s : ir.storeys)
        if (s.id == id) return &s;
    return nullptr;
}

WallUnit wall_unit(const btg::Ir& ir, const btg::IrWall& w) {
    WallUnit u;
    const btg::IrStorey* st = find_storey(ir, w.storey);
    const double base_y = st ? st->y : 0.0;
    const glm::dvec2 d = plan_dir(w.a, w.b);
    u.len = glm::length(w.b - w.a);
    u.thick = w.thickness;
    u.h = w.h;
    u.class_code = w.class_code;
    u.pl.yaw_deg = yaw_for_axis_x(d);
    u.pl.at = {w.a.x, base_y, w.a.y};
    for (const btg::IrOpening& o : w.openings) {
        u.cuts.positions.push_back({static_cast<float>(o.s0), 0.0f, 0.0f});
        u.cuts.positions.push_back({static_cast<float>(o.s1), 0.0f, 0.0f});
        auto& y0 = u.cuts.f32_attrs["y0"];
        auto& y1 = u.cuts.f32_attrs["y1"];
        y0.insert(y0.end(), 2, static_cast<float>(o.y0));
        y1.insert(y1.end(), 2, static_cast<float>(o.y1));
    }
    if (!w.top_profile.empty()) {
        // World -> wall local: R_y(-yaw) * (p - at).
        const double rad = u.pl.yaw_deg * 3.14159265358979323846 / 180.0;
        const double c = std::cos(rad), s = -std::sin(rad);
        auto& ov = u.top_planes.vec3_attrs["o"].first;
        auto& nv = u.top_planes.vec3_attrs["n"].first;
        u.top_planes.vec3_attrs["o"].second = pgg::AttrTypeInfo::Point;
        u.top_planes.vec3_attrs["n"].second = pgg::AttrTypeInfo::Normal;
        for (const btg::IrPlane& p : w.top_profile) {
            const glm::dvec3 o_l = rot_y(p.o - u.pl.at, c, s);
            const glm::dvec3 n_l = rot_y(p.n, c, s);
            u.top_planes.positions.push_back(
                {static_cast<float>(o_l.x), static_cast<float>(o_l.y), static_cast<float>(o_l.z)});
            ov.push_back(u.top_planes.positions.back());
            nv.push_back({static_cast<float>(n_l.x), static_cast<float>(n_l.y),
                          static_cast<float>(n_l.z)});
        }
    }
    return u;
}

NodeUnit node_unit(const btg::Ir& ir, const btg::IrNode& n) {
    NodeUnit u;
    const btg::IrStorey* st = find_storey(ir, n.storey);
    const double base_y = st ? st->y : 0.0;
    u.pl.at = {n.p.x, base_y, n.p.y};
    double t = 0.0, h = 0.0;
    for (const auto& f : n.faces) h = std::max(h, f.h);
    for (const auto& w : ir.walls) {
        if (w.storey != n.storey) continue;
        const double da = glm::length(w.a - n.p);
        const double db = glm::length(w.b - n.p);
        if (std::min(da, db) > 1e-4) continue;
        t = std::max(t, w.thickness);
        h = std::max(h, w.h);
    }
    if (h <= 0.0 && st) h = st->h;
    if (t <= 0.0) t = ir.params.t_exterior;
    if (h <= 0.0) h = 2.8;
    u.size = {static_cast<float>(t), static_cast<float>(h), static_cast<float>(t)};
    return u;
}

SlabUnit slab_unit(const btg::IrSlab& s) {
    SlabUnit u;
    u.id = "slab:" + s.storey_below + ">" + s.storey_above;
    u.t = s.t;
    for (const auto& r : s.rects) {
        u.rects.positions.push_back(
            {static_cast<float>(r.x0), static_cast<float>(s.y), static_cast<float>(r.z0)});
        u.rects.positions.push_back(
            {static_cast<float>(r.x1), static_cast<float>(s.y), static_cast<float>(r.z1)});
    }
    return u;
}

StairUnit stair_unit(const btg::Ir& ir, const btg::IrStair& s) {
    StairUnit u;
    const btg::IrStorey* from = find_storey(ir, s.from);
    u.pl.yaw_deg = yaw_for_axis_x(s.dir);
    u.pl.at = {s.start.x, from ? from->y : 0.0, s.start.y};
    u.risers = s.risers;
    u.risers_low = s.risers_low;
    u.rise = s.rise;
    u.going = s.going;
    u.w = s.width;
    u.turn = glm::dot(s.dir2, plan_left(s.dir)) > 0.0 ? 1 : -1;
    return u;
}

RailingUnit railing_unit(const btg::Ir& ir, const btg::IrRailing& r) {
    RailingUnit u;
    const btg::IrStorey* st = find_storey(ir, r.storey);
    const glm::dvec2 d = plan_dir(r.a, r.b);
    u.len = glm::length(r.b - r.a);
    u.h = r.h;
    u.pl.yaw_deg = yaw_for_axis_z(d);
    u.pl.at = {r.a.x, st ? st->y : 0.0, r.a.y};
    return u;
}

OpeningUnit opening_unit(const btg::Ir& ir, const btg::IrWall& w, const btg::IrOpening& o) {
    OpeningUnit u;
    u.id = o.id;
    u.type = o.type.empty() ? (o.kind_code == btg::kOpeningWindow ? "win_flat" : "door_plain")
                            : o.type;
    const btg::IrStorey* st = find_storey(ir, w.storey);
    const glm::dvec2 d = plan_dir(w.a, w.b);
    const glm::dvec2 left = plan_left(d);
    // Face the street when the opening is in an exterior wall, else side_l.
    const glm::dvec2 facing =
        w.side_r == "outside" && w.side_l != "outside" ? glm::dvec2(-left) : left;
    const double mid = (o.s0 + o.s1) * 0.5;
    const glm::dvec2 at = w.a + d * mid;
    u.w = o.s1 - o.s0;
    u.h = o.y1 - o.y0;
    u.pl.yaw_deg = yaw_for_axis_z(facing);
    u.pl.at = {at.x, (st ? st->y : 0.0) + o.y0, at.y};
    return u;
}

ChimneyUnit chimney_unit(const btg::IrChimney& c) {
    ChimneyUnit u;
    u.w = c.section.x;
    u.d = c.section.y;
    u.h = c.top.y;
    u.pl.at = {c.top.x, 0.0, c.top.z};
    return u;
}

bool hearth_unit(const btg::Ir& ir, const btg::IrHearthRef& h, HearthUnit& out) {
    const btg::IrWall* wall = nullptr;
    for (const auto& w : ir.walls) {
        if (w.id == h.wall) {
            wall = &w;
            break;
        }
    }
    if (!wall) return false;
    const btg::IrStorey* st = find_storey(ir, wall->storey);
    const glm::dvec2 d = plan_dir(wall->a, wall->b);
    const glm::dvec2 left = plan_left(d);
    const glm::dvec2 facing = wall->side_l == h.space ? left : glm::dvec2(-left);
    const glm::dvec2 at = wall->a + d * h.s;
    out.pl.yaw_deg = yaw_for_axis_z(facing);
    out.pl.at = {at.x, st ? st->y : 0.0, at.y};
    out.kind_code = h.kind_code;
    return true;
}

RoofUnit roof_unit(const btg::Ir& ir, const btg::IrVolume& v,
                   std::vector<std::string>& diagnostics) {
    RoofUnit u;
    u.volume = v.id;
    btg::RoofModel model;
    std::string err;
    if (!btg::build_roof_model(v, model, err)) {
        diagnostics.push_back("volume " + v.id + ": roof model failed: " + err);
    }
    u.y0 = model.y0;
    u.overhang = v.roof.overhang;
    u.roof_t = v.roof.roof_t;
    auto& pitch = u.outline.f32_attrs["pitch"];
    for (size_t i = 0; i < v.outline.size(); ++i) {
        u.outline.positions.push_back(
            {static_cast<float>(v.outline[i].x), 0.0f, static_cast<float>(v.outline[i].y)});
        const double p = i < v.roof.pitch_by_edge.size() ? v.roof.pitch_by_edge[i] : 45.0;
        pitch.push_back(static_cast<float>(p));
        if (p < 89.9 && u.pitch > 44.9 && u.pitch < 45.1) u.pitch = p;
    }
    for (const auto& c : ir.chimneys) {
        if (c.roof_plane < 0 || c.roof_plane >= static_cast<int>(ir.roof.planes.size()))
            continue;
        if (ir.roof.planes[static_cast<size_t>(c.roof_plane)].volume != v.id) continue;
        const double hx = c.section.x * 0.5, hz = c.section.y * 0.5;
        u.holes.positions.push_back(
            {static_cast<float>(c.top.x - hx), 0.0f, static_cast<float>(c.top.z - hz)});
        u.holes.positions.push_back(
            {static_cast<float>(c.top.x + hx), 0.0f, static_cast<float>(c.top.z + hz)});
    }
    std::vector<glm::vec2> outline2;
    for (const auto& p : v.outline)
        outline2.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
    const std::vector<glm::vec2> inner2 =
        geom2d::offsetOutline(outline2, static_cast<float>(-ir.params.t_exterior));
    for (const auto& p : inner2) u.inner.positions.push_back({p.x, 0.0f, p.y});
    return u;
}

}  // namespace building_geometry_generator
