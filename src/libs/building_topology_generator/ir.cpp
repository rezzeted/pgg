#include "pch.h"

#include "ir.h"

#include <nlohmann/json.hpp>

#include "codes.h"

namespace building_topology_generator {

using nlohmann::ordered_json;

namespace {

double q4(double v) { return std::round(v * 1e4) / 1e4; }

ordered_json qnum(double v) {
    const double q = q4(v);
    // Integral values serialize as integers: shorter, still round-trips (N6).
    if (q == std::floor(q) && std::abs(q) < 9e15) return ordered_json(static_cast<int64_t>(q));
    return ordered_json(q);
}

ordered_json vec2_json(const glm::dvec2& v) { return ordered_json::array({qnum(v.x), qnum(v.y)}); }
ordered_json vec3_json(const glm::dvec3& v) { return ordered_json::array({qnum(v.x), qnum(v.y), qnum(v.z)}); }
ordered_json ivec2_json(const glm::ivec2& v) { return ordered_json::array({v.x, v.y}); }

bool read_double(const nlohmann::json& j, const char* key, double& out, std::string& err, const std::string& path) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    if (!j[key].is_number()) {
        err = path + "." + key + ": expected a number";
        return false;
    }
    out = j[key].get<double>();
    return true;
}

bool read_double_def(const nlohmann::json& j, const char* key, double& out) {
    if (!j.contains(key) || !j[key].is_number()) return false;
    out = j[key].get<double>();
    return true;
}

bool read_string(const nlohmann::json& j, const char* key, std::string& out, std::string& err, const std::string& path) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    if (!j[key].is_string()) {
        err = path + "." + key + ": expected a string";
        return false;
    }
    out = j[key].get<std::string>();
    return true;
}

bool read_int(const nlohmann::json& j, const char* key, int& out, std::string& err, const std::string& path) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    if (!j[key].is_number_integer()) {
        err = path + "." + key + ": expected an integer";
        return false;
    }
    out = j[key].get<int>();
    return true;
}

bool read_vec2(const nlohmann::json& j, glm::dvec2& out, std::string& err, const std::string& path) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        err = path + ": expected [x, z]";
        return false;
    }
    out = glm::dvec2(j[0].get<double>(), j[1].get<double>());
    return true;
}

bool read_vec3(const nlohmann::json& j, glm::dvec3& out, std::string& err, const std::string& path) {
    if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) {
        err = path + ": expected [x, y, z]";
        return false;
    }
    out = glm::dvec3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
    return true;
}

bool read_ivec2(const nlohmann::json& j, glm::ivec2& out, std::string& err, const std::string& path) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) {
        err = path + ": expected [i, j]";
        return false;
    }
    out = glm::ivec2(j[0].get<int>(), j[1].get<int>());
    return true;
}

// Coded field: the string is authoritative for humans, the int for the assets.
// Both present -> must agree; only one -> derive the other.
bool read_coded(const nlohmann::json& j, const char* name_key, const char* code_key, std::string_view (*name_fn)(int),
                int (*code_fn)(std::string_view), int& code_out, std::string& err, const std::string& path) {
    const bool has_name = j.contains(name_key);
    const bool has_code = j.contains(code_key);
    if (!has_name && !has_code) {
        err = path + ": missing \"" + name_key + "\"/\"" + code_key + "\"";
        return false;
    }
    int code = 0;
    if (has_code) {
        if (!j[code_key].is_number_integer()) {
            err = path + "." + code_key + ": expected an integer";
            return false;
        }
        code = j[code_key].get<int>();
        if (name_fn(code).empty()) {
            err = path + "." + code_key + ": unknown code " + std::to_string(code);
            return false;
        }
    }
    if (has_name) {
        if (!j[name_key].is_string()) {
            err = path + "." + name_key + ": expected a string";
            return false;
        }
        const std::string name = j[name_key].get<std::string>();
        const int from_name = code_fn(name);
        if (from_name == 0) {
            err = path + "." + name_key + ": unknown value \"" + name + "\"";
            return false;
        }
        if (has_code && from_name != code) {
            err = path + ": \"" + name_key + "\" (\"" + name + "\") disagrees with \"" + code_key + "\" (" +
                  std::to_string(code) + ")";
            return false;
        }
        code = from_name;
    }
    code_out = code;
    return true;
}

void put_coded(ordered_json& j, const char* name_key, const char* code_key, std::string_view (*name_fn)(int), int code) {
    j[name_key] = std::string(name_fn(code));
    j[code_key] = code;
}

ordered_json params_json(const IrParams& p) {
    ordered_json j;
    j["cell"] = qnum(p.cell);
    j["h_ground"] = qnum(p.h_ground);
    j["h_upper"] = qnum(p.h_upper);
    j["slab_t"] = qnum(p.slab_t);
    j["t_exterior"] = qnum(p.t_exterior);
    j["t_bearing"] = qnum(p.t_bearing);
    j["t_partition"] = qnum(p.t_partition);
    j["t_knee"] = qnum(p.t_knee);
    j["player_height"] = qnum(p.player_height);
    j["player_radius"] = qnum(p.player_radius);
    j["headroom_margin"] = qnum(p.headroom_margin);
    j["glass_ratio"] = qnum(p.glass_ratio);
    j["h_knee"] = qnum(p.h_knee);
    j["roof_t"] = qnum(p.roof_t);
    j["h_above_roof"] = qnum(p.h_above_roof);
    j["valley_margin"] = qnum(p.valley_margin);
    return j;
}

bool read_params(const nlohmann::json& j, IrParams& p, std::string& err) {
    if (!j.is_object()) {
        err = "params: expected an object";
        return false;
    }
    read_double_def(j, "cell", p.cell);
    read_double_def(j, "h_ground", p.h_ground);
    read_double_def(j, "h_upper", p.h_upper);
    read_double_def(j, "slab_t", p.slab_t);
    read_double_def(j, "t_exterior", p.t_exterior);
    read_double_def(j, "t_bearing", p.t_bearing);
    read_double_def(j, "t_partition", p.t_partition);
    read_double_def(j, "t_knee", p.t_knee);
    read_double_def(j, "player_height", p.player_height);
    read_double_def(j, "player_radius", p.player_radius);
    read_double_def(j, "headroom_margin", p.headroom_margin);
    read_double_def(j, "glass_ratio", p.glass_ratio);
    read_double_def(j, "h_knee", p.h_knee);
    read_double_def(j, "roof_t", p.roof_t);
    read_double_def(j, "h_above_roof", p.h_above_roof);
    read_double_def(j, "valley_margin", p.valley_margin);
    return true;
}

}  // namespace

void sort_ir(Ir& ir) {
    auto by_id = [](const auto& a, const auto& b) { return a.id < b.id; };
    std::sort(ir.storeys.begin(), ir.storeys.end(), by_id);
    std::sort(ir.volumes.begin(), ir.volumes.end(), by_id);
    std::sort(ir.spaces.begin(), ir.spaces.end(), by_id);
    std::sort(ir.walls.begin(), ir.walls.end(), by_id);
    std::sort(ir.nodes.begin(), ir.nodes.end(), by_id);
    std::sort(ir.railings.begin(), ir.railings.end(), by_id);
    std::sort(ir.stairs.begin(), ir.stairs.end(), by_id);
    std::sort(ir.chimneys.begin(), ir.chimneys.end(), by_id);
    std::sort(ir.dormers.begin(), ir.dormers.end(), by_id);
    std::sort(ir.slabs.begin(), ir.slabs.end(), [](const IrSlab& a, const IrSlab& b) {
        return a.storey_below != b.storey_below ? a.storey_below < b.storey_below : a.storey_above < b.storey_above;
    });
}

std::string write_ir_json(const Ir& ir) {
    ordered_json j;
    j["format"] = kIrFormat;
    j["seed"] = ir.seed;
    j["program"] = ir.program;
    j["params"] = params_json(ir.params);

    j["storeys"] = ordered_json::array();
    for (const IrStorey& s : ir.storeys) {
        ordered_json e;
        e["id"] = s.id;
        put_coded(e, "kind", "kind_code", storey_kind_name, s.kind_code);
        e["y"] = qnum(s.y);
        e["h"] = qnum(s.h);
        e["slab_t"] = qnum(s.slab_t);
        j["storeys"].push_back(std::move(e));
    }

    j["volumes"] = ordered_json::array();
    for (const IrVolume& v : ir.volumes) {
        ordered_json e;
        e["id"] = v.id;
        e["outline"] = ordered_json::array();
        for (const glm::dvec2& p : v.outline) e["outline"].push_back(vec2_json(p));
        e["storeys"] = ordered_json::array({v.storey0, v.storey1});
        ordered_json r;
        put_coded(r, "kind", "kind_code", roof_kind_name, v.roof.kind_code);
        r["pitch_by_edge"] = ordered_json::array();
        for (double p : v.roof.pitch_by_edge) r["pitch_by_edge"].push_back(qnum(p));
        r["overhang"] = qnum(v.roof.overhang);
        r["eave_rise"] = qnum(v.roof.eave_rise);
        r["eave_y"] = qnum(v.roof.eave_y);
        r["roof_t"] = qnum(v.roof.roof_t);
        e["roof"] = std::move(r);
        j["volumes"].push_back(std::move(e));
    }

    ordered_json roof;
    roof["planes"] = ordered_json::array();
    for (const IrRoofPlane& p : ir.roof.planes) {
        ordered_json e;
        e["volume"] = p.volume;
        e["edge"] = p.edge;
        e["pitch"] = qnum(p.pitch);
        e["o"] = vec3_json(p.o);
        e["n"] = vec3_json(p.n);
        roof["planes"].push_back(std::move(e));
    }
    auto edges_json = [](const std::vector<IrRoofEdge>& edges) {
        ordered_json arr = ordered_json::array();
        for (const IrRoofEdge& e : edges) {
            ordered_json x;
            x["a"] = vec3_json(e.a);
            x["b"] = vec3_json(e.b);
            arr.push_back(std::move(x));
        }
        return arr;
    };
    roof["ridges"] = edges_json(ir.roof.ridges);
    roof["valleys"] = edges_json(ir.roof.valleys);
    roof["hips"] = edges_json(ir.roof.hips);
    j["roof"] = std::move(roof);

    j["spaces"] = ordered_json::array();
    for (const IrSpace& s : ir.spaces) {
        ordered_json e;
        e["id"] = s.id;
        e["storey"] = s.storey;
        put_coded(e, "fn", "fn_code", fn_name, s.fn_code);
        put_coded(e, "zone", "zone_code", zone_name, s.zone_code);
        e["cells"] = ordered_json::array();
        for (const glm::ivec2& c : s.cells) e["cells"].push_back(ivec2_json(c));
        e["outline"] = ordered_json::array();
        for (const glm::dvec2& p : s.outline) e["outline"].push_back(vec2_json(p));
        e["area_net"] = qnum(s.area_net);
        e["habitable"] = s.habitable;
        if (s.hearth_code != 0) put_coded(e, "hearth", "hearth_code", hearth_kind_name, s.hearth_code);
        j["spaces"].push_back(std::move(e));
    }

    j["walls"] = ordered_json::array();
    for (const IrWall& w : ir.walls) {
        ordered_json e;
        e["id"] = w.id;
        e["storey"] = w.storey;
        e["a"] = vec2_json(w.a);
        e["b"] = vec2_json(w.b);
        put_coded(e, "class", "class_code", wall_class_name, w.class_code);
        e["thickness"] = qnum(w.thickness);
        e["owner"] = w.owner;
        e["side_l"] = w.side_l;
        e["side_r"] = w.side_r;
        e["h"] = qnum(w.h);
        if (!w.top_profile.empty()) {
            e["top_profile"] = ordered_json::array();
            for (const IrPlane& pl : w.top_profile) {
                ordered_json x;
                x["o"] = vec3_json(pl.o);
                x["n"] = vec3_json(pl.n);
                e["top_profile"].push_back(std::move(x));
            }
        }
        e["openings"] = ordered_json::array();
        for (const IrOpening& o : w.openings) {
            ordered_json x;
            x["id"] = o.id;
            put_coded(x, "kind", "kind_code", opening_kind_name, o.kind_code);
            x["type"] = o.type;
            x["s0"] = qnum(o.s0);
            x["s1"] = qnum(o.s1);
            x["y0"] = qnum(o.y0);
            x["y1"] = qnum(o.y1);
            e["openings"].push_back(std::move(x));
        }
        j["walls"].push_back(std::move(e));
    }

    j["nodes"] = ordered_json::array();
    for (const IrNode& nd : ir.nodes) {
        ordered_json e;
        e["id"] = nd.id;
        e["storey"] = nd.storey;
        e["p"] = vec2_json(nd.p);
        e["owner"] = nd.owner;
        e["faces"] = ordered_json::array();
        for (const IrNodeFace& f : nd.faces) {
            ordered_json x;
            x["dir"] = vec2_json(f.dir);
            x["h"] = qnum(f.h);
            e["faces"].push_back(std::move(x));
        }
        j["nodes"].push_back(std::move(e));
    }

    j["slabs"] = ordered_json::array();
    for (const IrSlab& s : ir.slabs) {
        ordered_json e;
        e["storey_below"] = s.storey_below;
        e["storey_above"] = s.storey_above;
        e["rects"] = ordered_json::array();
        for (const IrSlabRect& r : s.rects) {
            ordered_json x;
            x["x0"] = qnum(r.x0);
            x["z0"] = qnum(r.z0);
            x["x1"] = qnum(r.x1);
            x["z1"] = qnum(r.z1);
            e["rects"].push_back(std::move(x));
        }
        e["y"] = qnum(s.y);
        e["t"] = qnum(s.t);
        j["slabs"].push_back(std::move(e));
    }

    j["railings"] = ordered_json::array();
    for (const IrRailing& r : ir.railings) {
        ordered_json e;
        e["id"] = r.id;
        e["storey"] = r.storey;
        e["a"] = vec2_json(r.a);
        e["b"] = vec2_json(r.b);
        e["h"] = qnum(r.h);
        j["railings"].push_back(std::move(e));
    }

    j["stairs"] = ordered_json::array();
    for (const IrStair& s : ir.stairs) {
        ordered_json e;
        e["id"] = s.id;
        put_coded(e, "type", "type_code", stair_type_name, s.type_code);
        e["from"] = s.from;
        e["to"] = s.to;
        e["start"] = vec2_json(s.start);
        e["dir"] = vec2_json(s.dir);
        e["width"] = qnum(s.width);
        e["risers"] = s.risers;
        e["rise"] = qnum(s.rise);
        e["going"] = qnum(s.going);
        if (s.type_code == kStairL) {
            e["risers_low"] = s.risers_low;
            e["dir2"] = vec2_json(s.dir2);
            e["landing"] = ordered_json::array({vec2_json(s.landing_min), vec2_json(s.landing_max)});
        }
        e["well"] = ordered_json::array();
        for (const IrSlabRect& r : s.well) {
            e["well"].push_back(
                ordered_json::array({qnum(r.x0), qnum(r.z0), qnum(r.x1), qnum(r.z1)}));
        }
        j["stairs"].push_back(std::move(e));
    }

    j["chimneys"] = ordered_json::array();
    for (const IrChimney& c : ir.chimneys) {
        ordered_json e;
        e["id"] = c.id;
        e["hearths"] = ordered_json::array();
        for (const IrHearthRef& h : c.hearths) {
            ordered_json x;
            x["space"] = h.space;
            put_coded(x, "kind", "kind_code", hearth_kind_name, h.kind_code);
            x["wall"] = h.wall;
            x["s"] = qnum(h.s);
            e["hearths"].push_back(std::move(x));
        }
        e["section"] = vec2_json(c.section);
        ordered_json cells;
        for (const auto& [storey, sc] : c.cells_by_storey) {
            cells[storey] = ordered_json::array();
            for (const glm::ivec2& cell : sc) cells[storey].push_back(ivec2_json(cell));
        }
        e["cells_by_storey"] = std::move(cells);
        e["top"] = vec3_json(c.top);
        e["roof_plane"] = c.roof_plane;
        j["chimneys"].push_back(std::move(e));
    }

    j["dormers"] = ordered_json::array();
    for (const IrDormer& d : ir.dormers) {
        ordered_json e;
        e["id"] = d.id;
        e["plane"] = d.plane;
        e["at"] = vec2_json(d.at);
        e["w"] = qnum(d.w);
        e["h"] = qnum(d.h);
        j["dormers"].push_back(std::move(e));
    }

    ordered_json access;
    access["nodes"] = ordered_json::array();
    for (const std::string& n : ir.access.nodes) access["nodes"].push_back(n);
    access["edges"] = ordered_json::array();
    for (const IrAccessEdge& e : ir.access.edges) {
        ordered_json x;
        x["a"] = e.a;
        x["b"] = e.b;
        x["via"] = e.via;
        if (!e.ref.empty()) x["ref"] = e.ref;
        access["edges"].push_back(std::move(x));
    }
    j["access"] = std::move(access);

    j["anchors"] = ordered_json::array();
    for (const IrAnchor& a : ir.anchors) {
        ordered_json e;
        put_coded(e, "kind", "kind_code", anchor_kind_name, a.kind_code);
        e["p"] = vec3_json(a.p);
        e["dir"] = vec3_json(a.dir);
        e["space"] = a.space;
        j["anchors"].push_back(std::move(e));
    }

    ordered_json report;
    ordered_json cost = ordered_json::object();
    for (const auto& [term, value] : ir.report_cost) cost[term] = qnum(value);
    report["cost"] = std::move(cost);
    report["moves"] = ordered_json::array();
    for (const std::string& m : ir.report_moves) report["moves"].push_back(m);
    j["report"] = std::move(report);

    return j.dump(1) + "\n";
}

namespace {

bool read_hearth_ref(const nlohmann::json& j, IrHearthRef& h, std::string& err, const std::string& path) {
    if (!read_string(j, "space", h.space, err, path)) return false;
    if (!read_coded(j, "kind", "kind_code", hearth_kind_name, hearth_kind_code, h.kind_code, err, path)) return false;
    if (!read_string(j, "wall", h.wall, err, path)) return false;
    if (!read_double(j, "s", h.s, err, path)) return false;
    return true;
}

}  // namespace

bool read_ir_json(const std::string& text, Ir& out, std::string& err) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        err = "invalid JSON";
        return false;
    }
    if (!j.is_object()) {
        err = "expected a JSON object at the top level";
        return false;
    }
    const std::string format = j.value("format", "");
    if (format != kIrFormat) {
        err = "unsupported IR format \"" + format + "\", expected \"" + kIrFormat +
              "\" — regenerate the IR with this version of the BuildingGenerator toolchain";
        return false;
    }

    Ir ir;
    ir.seed = j.value("seed", 0);
    ir.program = j.value("program", "");
    if (j.contains("params") && !read_params(j["params"], ir.params, err)) return false;

    if (!j.contains("storeys") || !j["storeys"].is_array()) {
        err = "missing \"storeys\"";
        return false;
    }
    for (const nlohmann::json& e : j["storeys"]) {
        IrStorey s;
        const std::string path = "storeys[]";
        if (!read_string(e, "id", s.id, err, path)) return false;
        if (!read_coded(e, "kind", "kind_code", storey_kind_name, storey_kind_code, s.kind_code, err, path)) return false;
        if (!read_double(e, "y", s.y, err, path)) return false;
        if (!read_double(e, "h", s.h, err, path)) return false;
        if (!read_double(e, "slab_t", s.slab_t, err, path)) return false;
        ir.storeys.push_back(std::move(s));
    }

    if (j.contains("volumes")) {
        for (const nlohmann::json& e : j["volumes"]) {
            IrVolume v;
            const std::string path = "volumes[]";
            if (!read_string(e, "id", v.id, err, path)) return false;
            if (!e.contains("outline") || !e["outline"].is_array() || e["outline"].size() < 3) {
                err = path + ": \"outline\" needs >= 3 points";
                return false;
            }
            for (const nlohmann::json& p : e["outline"]) {
                glm::dvec2 q;
                if (!read_vec2(p, q, err, path + ".outline")) return false;
                v.outline.push_back(q);
            }
            if (!e.contains("storeys") || !e["storeys"].is_array() || e["storeys"].size() != 2) {
                err = path + ": \"storeys\" must be [k0, k1]";
                return false;
            }
            v.storey0 = e["storeys"][0].get<int>();
            v.storey1 = e["storeys"][1].get<int>();
            if (!e.contains("roof")) {
                err = path + ": missing \"roof\"";
                return false;
            }
            const nlohmann::json& r = e["roof"];
            if (!read_coded(r, "kind", "kind_code", roof_kind_name, roof_kind_code, v.roof.kind_code, err, path + ".roof"))
                return false;
            if (r.contains("pitch_by_edge")) {
                for (const nlohmann::json& p : r["pitch_by_edge"]) {
                    if (!p.is_number()) {
                        err = path + ".roof.pitch_by_edge: expected numbers";
                        return false;
                    }
                    v.roof.pitch_by_edge.push_back(p.get<double>());
                }
            }
            if (v.roof.pitch_by_edge.size() != v.outline.size()) {
                err = path + ".roof.pitch_by_edge: size must match the outline vertex count";
                return false;
            }
            if (!read_double(r, "overhang", v.roof.overhang, err, path + ".roof")) return false;
            if (!read_double(r, "eave_rise", v.roof.eave_rise, err, path + ".roof")) return false;
            if (!read_double(r, "eave_y", v.roof.eave_y, err, path + ".roof")) return false;
            if (!read_double(r, "roof_t", v.roof.roof_t, err, path + ".roof")) return false;
            ir.volumes.push_back(std::move(v));
        }
    }

    if (j.contains("roof")) {
        const nlohmann::json& r = j["roof"];
        if (r.contains("planes")) {
            for (const nlohmann::json& e : r["planes"]) {
                IrRoofPlane p;
                const std::string path = "roof.planes[]";
                if (!read_string(e, "volume", p.volume, err, path)) return false;
                if (!read_int(e, "edge", p.edge, err, path)) return false;
                if (!read_double(e, "pitch", p.pitch, err, path)) return false;
                if (!read_vec3(e["o"], p.o, err, path + ".o")) return false;
                if (!read_vec3(e["n"], p.n, err, path + ".n")) return false;
                ir.roof.planes.push_back(std::move(p));
            }
        }
        auto read_edges = [&](const char* key, std::vector<IrRoofEdge>& out_edges) -> bool {
            if (!r.contains(key)) return true;
            for (const nlohmann::json& e : r[key]) {
                IrRoofEdge x;
                const std::string path = std::string("roof.") + key + "[]";
                if (!read_vec3(e["a"], x.a, err, path + ".a")) return false;
                if (!read_vec3(e["b"], x.b, err, path + ".b")) return false;
                out_edges.push_back(x);
            }
            return true;
        };
        if (!read_edges("ridges", ir.roof.ridges)) return false;
        if (!read_edges("valleys", ir.roof.valleys)) return false;
        if (!read_edges("hips", ir.roof.hips)) return false;
    }

    if (!j.contains("spaces") || !j["spaces"].is_array()) {
        err = "missing \"spaces\"";
        return false;
    }
    for (const nlohmann::json& e : j["spaces"]) {
        IrSpace s;
        const std::string path = "spaces[]";
        if (!read_string(e, "id", s.id, err, path)) return false;
        if (!read_string(e, "storey", s.storey, err, path)) return false;
        if (!read_coded(e, "fn", "fn_code", fn_name, fn_code, s.fn_code, err, path)) return false;
        if (!read_coded(e, "zone", "zone_code", zone_name, zone_code, s.zone_code, err, path)) return false;
        if (e.contains("cells")) {
            for (const nlohmann::json& c : e["cells"]) {
                glm::ivec2 cell;
                if (!read_ivec2(c, cell, err, path + ".cells")) return false;
                s.cells.push_back(cell);
            }
        }
        if (!e.contains("outline") || !e["outline"].is_array() || e["outline"].size() < 3) {
            err = path + ": \"outline\" needs >= 3 points";
            return false;
        }
        for (const nlohmann::json& p : e["outline"]) {
            glm::dvec2 q;
            if (!read_vec2(p, q, err, path + ".outline")) return false;
            s.outline.push_back(q);
        }
        if (!read_double(e, "area_net", s.area_net, err, path)) return false;
        s.habitable = e.value("habitable", false);
        if (e.contains("hearth") || e.contains("hearth_code")) {
            if (!read_coded(e, "hearth", "hearth_code", hearth_kind_name, hearth_kind_code, s.hearth_code, err, path))
                return false;
        }
        ir.spaces.push_back(std::move(s));
    }

    if (!j.contains("walls") || !j["walls"].is_array()) {
        err = "missing \"walls\"";
        return false;
    }
    for (const nlohmann::json& e : j["walls"]) {
        IrWall w;
        const std::string path = "walls[]";
        if (!read_string(e, "id", w.id, err, path)) return false;
        if (!read_string(e, "storey", w.storey, err, path)) return false;
        if (!read_vec2(e["a"], w.a, err, path + ".a")) return false;
        if (!read_vec2(e["b"], w.b, err, path + ".b")) return false;
        if (!read_coded(e, "class", "class_code", wall_class_name, wall_class_code, w.class_code, err, path)) return false;
        if (!read_double(e, "thickness", w.thickness, err, path)) return false;
        if (!read_string(e, "owner", w.owner, err, path)) return false;
        if (!read_string(e, "side_l", w.side_l, err, path)) return false;
        if (!read_string(e, "side_r", w.side_r, err, path)) return false;
        if (!read_double(e, "h", w.h, err, path)) return false;
        if (e.contains("top_profile")) {
            for (const nlohmann::json& x : e["top_profile"]) {
                IrPlane pl;
                if (!read_vec3(x["o"], pl.o, err, path + ".top_profile.o")) return false;
                if (!read_vec3(x["n"], pl.n, err, path + ".top_profile.n")) return false;
                w.top_profile.push_back(pl);
            }
        }
        if (e.contains("openings")) {
            for (const nlohmann::json& x : e["openings"]) {
                IrOpening o;
                if (!read_string(x, "id", o.id, err, path + ".openings")) return false;
                if (!read_coded(x, "kind", "kind_code", opening_kind_name, opening_kind_code, o.kind_code, err,
                                path + ".openings"))
                    return false;
                if (!read_string(x, "type", o.type, err, path + ".openings")) return false;
                if (!read_double(x, "s0", o.s0, err, path + ".openings")) return false;
                if (!read_double(x, "s1", o.s1, err, path + ".openings")) return false;
                if (!read_double(x, "y0", o.y0, err, path + ".openings")) return false;
                if (!read_double(x, "y1", o.y1, err, path + ".openings")) return false;
                w.openings.push_back(std::move(o));
            }
        }
        ir.walls.push_back(std::move(w));
    }

    if (j.contains("nodes")) {
        for (const nlohmann::json& e : j["nodes"]) {
            IrNode nd;
            const std::string path = "nodes[]";
            if (!read_string(e, "id", nd.id, err, path)) return false;
            if (!read_string(e, "storey", nd.storey, err, path)) return false;
            if (!read_vec2(e["p"], nd.p, err, path + ".p")) return false;
            if (!read_string(e, "owner", nd.owner, err, path)) return false;
            if (e.contains("faces")) {
                for (const nlohmann::json& x : e["faces"]) {
                    IrNodeFace f;
                    if (!read_vec2(x["dir"], f.dir, err, path + ".faces.dir")) return false;
                    if (!read_double(x, "h", f.h, err, path + ".faces")) return false;
                    nd.faces.push_back(f);
                }
            }
            ir.nodes.push_back(std::move(nd));
        }
    }

    if (j.contains("slabs")) {
        for (const nlohmann::json& e : j["slabs"]) {
            IrSlab s;
            const std::string path = "slabs[]";
            if (!read_string(e, "storey_below", s.storey_below, err, path)) return false;
            if (!read_string(e, "storey_above", s.storey_above, err, path)) return false;
            if (e.contains("rects")) {
                for (const nlohmann::json& x : e["rects"]) {
                    IrSlabRect r;
                    if (!read_double(x, "x0", r.x0, err, path + ".rects")) return false;
                    if (!read_double(x, "z0", r.z0, err, path + ".rects")) return false;
                    if (!read_double(x, "x1", r.x1, err, path + ".rects")) return false;
                    if (!read_double(x, "z1", r.z1, err, path + ".rects")) return false;
                    s.rects.push_back(r);
                }
            }
            if (!read_double(e, "y", s.y, err, path)) return false;
            if (!read_double(e, "t", s.t, err, path)) return false;
            ir.slabs.push_back(std::move(s));
        }
    }

    if (j.contains("railings")) {
        for (const nlohmann::json& e : j["railings"]) {
            IrRailing r;
            const std::string path = "railings[]";
            if (!read_string(e, "id", r.id, err, path)) return false;
            if (!read_string(e, "storey", r.storey, err, path)) return false;
            if (!read_vec2(e["a"], r.a, err, path + ".a")) return false;
            if (!read_vec2(e["b"], r.b, err, path + ".b")) return false;
            if (!read_double(e, "h", r.h, err, path)) return false;
            ir.railings.push_back(std::move(r));
        }
    }

    if (j.contains("stairs")) {
        for (const nlohmann::json& e : j["stairs"]) {
            IrStair s;
            const std::string path = "stairs[]";
            if (!read_string(e, "id", s.id, err, path)) return false;
            if (!read_coded(e, "type", "type_code", stair_type_name, stair_type_code, s.type_code, err, path)) return false;
            if (!read_string(e, "from", s.from, err, path)) return false;
            if (!read_string(e, "to", s.to, err, path)) return false;
            if (!read_vec2(e["start"], s.start, err, path + ".start")) return false;
            if (!read_vec2(e["dir"], s.dir, err, path + ".dir")) return false;
            if (!read_double(e, "width", s.width, err, path)) return false;
            if (!read_int(e, "risers", s.risers, err, path)) return false;
            if (!read_double(e, "rise", s.rise, err, path)) return false;
            if (!read_double(e, "going", s.going, err, path)) return false;
            if (s.type_code == kStairL) {
                if (!read_int(e, "risers_low", s.risers_low, err, path)) return false;
                if (!read_vec2(e["dir2"], s.dir2, err, path + ".dir2")) return false;
                if (!e.contains("landing") || !e["landing"].is_array() || e["landing"].size() != 2) {
                    err = path + ": \"landing\" must be [[x0, z0], [x1, z1]]";
                    return false;
                }
                if (!read_vec2(e["landing"][0], s.landing_min, err, path + ".landing")) return false;
                if (!read_vec2(e["landing"][1], s.landing_max, err, path + ".landing")) return false;
            }
            if (!e.contains("well") || !e["well"].is_array() || e["well"].empty()) {
                err = path + ": \"well\" must be a non-empty array of [x0, z0, x1, z1]";
                return false;
            }
            for (const nlohmann::json& r : e["well"]) {
                if (!r.is_array() || r.size() != 4) {
                    err = path + ".well: each entry must be [x0, z0, x1, z1]";
                    return false;
                }
                IrSlabRect w;
                for (int k = 0; k < 4; ++k) {
                    if (!r[k].is_number()) {
                        err = path + ".well: expected numbers";
                        return false;
                    }
                }
                w.x0 = r[0].get<double>();
                w.z0 = r[1].get<double>();
                w.x1 = r[2].get<double>();
                w.z1 = r[3].get<double>();
                s.well.push_back(w);
            }
            ir.stairs.push_back(std::move(s));
        }
    }

    if (j.contains("chimneys")) {
        for (const nlohmann::json& e : j["chimneys"]) {
            IrChimney c;
            const std::string path = "chimneys[]";
            if (!read_string(e, "id", c.id, err, path)) return false;
            if (!e.contains("hearths") || !e["hearths"].is_array() || e["hearths"].empty()) {
                err = path + ": \"hearths\" must be a non-empty array";
                return false;
            }
            for (const nlohmann::json& x : e["hearths"]) {
                IrHearthRef h;
                if (!read_hearth_ref(x, h, err, path + ".hearths")) return false;
                c.hearths.push_back(std::move(h));
            }
            if (!read_vec2(e["section"], c.section, err, path + ".section")) return false;
            if (e.contains("cells_by_storey")) {
                for (const auto& [storey, arr] : e["cells_by_storey"].items()) {
                    std::vector<glm::ivec2> cells;
                    for (const nlohmann::json& cell : arr) {
                        glm::ivec2 q;
                        if (!read_ivec2(cell, q, err, path + ".cells_by_storey")) return false;
                        cells.push_back(q);
                    }
                    c.cells_by_storey[storey] = std::move(cells);
                }
            }
            if (!read_vec3(e["top"], c.top, err, path + ".top")) return false;
            if (!read_int(e, "roof_plane", c.roof_plane, err, path)) return false;
            ir.chimneys.push_back(std::move(c));
        }
    }

    if (j.contains("dormers")) {
        for (const nlohmann::json& e : j["dormers"]) {
            IrDormer d;
            const std::string path = "dormers[]";
            if (!read_string(e, "id", d.id, err, path)) return false;
            if (!read_int(e, "plane", d.plane, err, path)) return false;
            if (!read_vec2(e["at"], d.at, err, path + ".at")) return false;
            if (!read_double(e, "w", d.w, err, path)) return false;
            if (!read_double(e, "h", d.h, err, path)) return false;
            ir.dormers.push_back(std::move(d));
        }
    }

    if (j.contains("access")) {
        const nlohmann::json& a = j["access"];
        if (a.contains("nodes"))
            for (const nlohmann::json& n : a["nodes"]) ir.access.nodes.push_back(n.get<std::string>());
        if (a.contains("edges")) {
            for (const nlohmann::json& e : a["edges"]) {
                IrAccessEdge x;
                const std::string path = "access.edges[]";
                if (!read_string(e, "a", x.a, err, path)) return false;
                if (!read_string(e, "b", x.b, err, path)) return false;
                if (!read_string(e, "via", x.via, err, path)) return false;
                x.ref = e.value("ref", "");
                ir.access.edges.push_back(std::move(x));
            }
        }
    }

    if (j.contains("anchors")) {
        for (const nlohmann::json& e : j["anchors"]) {
            IrAnchor a;
            const std::string path = "anchors[]";
            if (!read_coded(e, "kind", "kind_code", anchor_kind_name, anchor_kind_code, a.kind_code, err, path))
                return false;
            if (!read_vec3(e["p"], a.p, err, path + ".p")) return false;
            if (!read_vec3(e["dir"], a.dir, err, path + ".dir")) return false;
            if (!read_string(e, "space", a.space, err, path)) return false;
            ir.anchors.push_back(std::move(a));
        }
    }

    if (j.contains("report")) {
        const nlohmann::json& r = j["report"];
        if (r.contains("cost"))
            for (const auto& [term, value] : r["cost"].items())
                if (value.is_number()) ir.report_cost[term] = value.get<double>();
        if (r.contains("moves"))
            for (const nlohmann::json& m : r["moves"]) ir.report_moves.push_back(m.get<std::string>());
    }

    out = std::move(ir);
    return true;
}

bool load_ir_file(const std::string& path, Ir& out, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!read_ir_json(ss.str(), out, err)) {
        err = path + ": " + err;
        return false;
    }
    return true;
}

bool save_ir_file(const Ir& ir, const std::string& path, std::string& err) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot write " + path;
        return false;
    }
    out << write_ir_json(ir);
    return true;
}

}  // namespace building_topology_generator
