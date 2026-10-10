// DungeonGeometryGenerator project writer: Project -> dungeon-geometry-generator-project/1 JSON,
// the mirror of load_project (project.cpp / layout.cpp parse). Keys are
// emitted in a fixed order (N6); role entries write only their explicit
// set_fields keys so the F12 provenance survives a load -> save -> load round
// trip untouched. Absent keys keep their parser defaults, so optional values
// (overrides, tags, transforms without an explicit list) are omitted.

#include "pch.h"

#include "project.h"

#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace dungeon_geometry_generator {
namespace {

using nlohmann::ordered_json;

ordered_json range_to_json(const IntRange& r) {
    ordered_json j = ordered_json::array();
    j.push_back(r.lo);
    j.push_back(r.hi);
    return j;
}

ordered_json cell_pt_to_json(const CellPt& p) {
    ordered_json j = ordered_json::array();
    j.push_back(p.first);
    j.push_back(p.second);
    return j;
}

ordered_json fill_override_to_json(const FillOverride& o) {
    ordered_json j = ordered_json::object();
    if (o.h) j["h"] = *o.h;
    if (o.wall_t) j["wall_t"] = *o.wall_t;
    if (o.style) j["style"] = *o.style;
    if (o.floor) j["floor"] = *o.floor;
    if (o.ceil) j["ceil"] = *o.ceil;
    return j;
}

// Simple doors without overrides return an empty object — the caller then
// omits the key (the canonical form; the "simple" string parses the same).
ordered_json doors_to_json(const TemplateDoors& d) {
    if (d.manual) {
        ordered_json segs = ordered_json::array();
        for (const auto& [a, b] : d.segments) {
            ordered_json seg = ordered_json::array();
            seg.push_back(cell_pt_to_json(a));
            seg.push_back(cell_pt_to_json(b));
            segs.push_back(std::move(seg));
        }
        return ordered_json{{"manual", std::move(segs)}};
    }
    ordered_json j = ordered_json::object();
    if (d.length) j["length"] = *d.length;
    if (d.corner_distance) j["corner_distance"] = *d.corner_distance;
    return j;
}

// Only the keys explicit on this level (set_fields, F12); a programmatic
// editor that sets a field must also insert the key here (project.h).
ordered_json role_entry_to_json(const RoleEntry& re) {
    ordered_json j = ordered_json::object();
    for (const std::string& key : re.set_fields) {  // std::set: alphabetical, stable
        if (key == "h")
            j["h"] = re.h;
        else if (key == "style")
            j["style"] = re.style;
        else if (key == "floor")
            j["floor"] = re.floor;
        else if (key == "ceil")
            j["ceil"] = re.ceil;
        else if (key == "wall_t" && re.wall_t)
            j["wall_t"] = *re.wall_t;
    }
    return j;
}

ordered_json layout_to_json(const LayoutParams& l) {
    ordered_json layout;
    layout["corridors"] =
        ordered_json{{"width", l.corridor_width}, {"length", range_to_json(l.corridor_length)}};
    ordered_json rect = ordered_json{{"w", range_to_json(l.rect_w)}, {"h", range_to_json(l.rect_h)}};
    if (l.rect_roles_set && !l.rect_roles.empty()) rect["roles"] = l.rect_roles;
    layout["rooms_rect"] = std::move(rect);
    layout["door_length"] = l.door_length;
    layout["door_corner_distance"] = l.door_corner_distance;
    layout["min_room_distance"] = l.min_room_distance;
    layout["catalog_budget"] = l.catalog_budget;

    ordered_json rooms = ordered_json::array();
    for (const GraphRoom& r : l.rooms) {
        ordered_json jr;
        jr["id"] = r.id;
        jr["role"] = r.role;
        if (!r.tags.empty()) jr["tags"] = r.tags;
        if (!r.fill.empty()) jr["fill"] = fill_override_to_json(r.fill);
        rooms.push_back(std::move(jr));
    }
    layout["rooms"] = std::move(rooms);

    ordered_json passages = ordered_json::array();
    for (const Passage& p : l.passages)
        passages.push_back(ordered_json{{"a", p.a}, {"b", p.b}, {"door", p.door}});
    layout["passages"] = std::move(passages);

    if (!l.templates.empty()) {
        ordered_json templates = ordered_json::array();
        for (const TemplateDecl& t : l.templates) {
            ordered_json jt;
            jt["name"] = t.name;
            jt["roles"] = t.roles;
            ordered_json contour = ordered_json::array();
            for (const CellPt& p : t.contour) contour.push_back(cell_pt_to_json(p));
            jt["contour"] = std::move(contour);
            ordered_json doors = doors_to_json(t.doors);
            if (t.doors.manual || !doors.empty()) jt["doors"] = std::move(doors);
            if (t.transforms_set) jt["transforms"] = t.transforms;
            if (!t.fill.empty()) jt["fill"] = fill_override_to_json(t.fill);
            templates.push_back(std::move(jt));
        }
        layout["templates"] = std::move(templates);
    }

    // Viewer metadata: editor canvas positions of existing rooms only
    // (std::map iteration keeps the ids sorted, N6).
    if (!l.editor_node_pos.empty()) {
        std::set<std::string> ids;
        for (const GraphRoom& r : l.rooms) ids.insert(r.id);
        ordered_json pos = ordered_json::object();
        for (const auto& [id, p] : l.editor_node_pos) {
            if (!ids.count(id)) continue;
            pos[id] = ordered_json::array({p.first, p.second});
        }
        if (!pos.empty()) layout["editor"] = ordered_json{{"node_pos", std::move(pos)}};
    }
    return layout;
}

ordered_json fill_to_json(const FillParams& f) {
    ordered_json fill;
    fill["cell"] = f.cell;
    fill["wall_t"] = f.wall_t;
    fill["min_passage"] = f.min_passage;
    fill["min_opening"] = f.min_opening;
    fill["room_h"] = f.room_h;
    fill["door_h"] = f.door_h;
    fill["frame"] = f.frame;
    fill["lamp_step"] = f.lamp_step;
    if (f.lamp_place != "ceil") fill["lamp_place"] = f.lamp_place;  // parser default
    fill["row_module"] = f.row_module;
    if (!f.roles.empty()) {
        ordered_json roles = ordered_json::object();
        for (const auto& [name, re] : f.roles) roles[name] = role_entry_to_json(re);
        fill["roles"] = std::move(roles);
    }
    fill["transitions"] = ordered_json{{"pattern", f.transitions.pattern},
                                       {"width", f.transitions.width},
                                       {"place", f.transitions.place}};
    if (!f.side_rules.empty()) {
        ordered_json rules = ordered_json::array();
        for (const SideRule& r : f.side_rules) {
            ordered_json jr;
            ordered_json match = ordered_json::object();
            if (!r.side.empty()) match["side"] = r.side;
            if (!r.adjacent_role.empty()) match["adjacent_role"] = r.adjacent_role;
            if (!match.empty()) jr["match"] = std::move(match);
            jr["style"] = r.style;
            rules.push_back(std::move(jr));
        }
        fill["side_rules"] = std::move(rules);
    }
    if (!f.decor.empty()) {
        ordered_json rules = ordered_json::array();
        for (const DecorRule& r : f.decor) {
            ordered_json jr;
            jr["tag"] = r.tag;
            if (r.place != "floor") jr["place"] = r.place;
            if (!r.roles.empty()) jr["roles"] = r.roles;
            if (r.chance != 1.0) jr["chance"] = r.chance;
            if (r.count != 1) jr["count"] = r.count;
            if (r.min_dist != 0.0) jr["min_dist"] = r.min_dist;
            if (r.place == "floor" && r.align != "any") jr["align"] = r.align;
            if (r.radius != 0.5) jr["radius"] = r.radius;
            // align/cut_r are floor-only keys (the parser rejects them on wall rules).
            if (r.place == "floor" && r.cut_r != 0.0) jr["cut_r"] = r.cut_r;
            rules.push_back(std::move(jr));
        }
        fill["decor"] = std::move(rules);
    }
    return fill;
}

}  // namespace

bool write_project_json(const Project& project, std::string& out, std::string& err) {
    if (!project.layout) {
        err = "cannot write \"" + project.format + "\" (no layout tier); only \"" +
              std::string(kProjectFormatV1) + "\" is writable";
        return false;
    }
    ordered_json doc;
    doc["format"] = kProjectFormatV1;
    doc["seed"] = project.seed;
    doc["layout"] = layout_to_json(*project.layout);
    doc["fill"] = fill_to_json(project.fill);
    if (!project.slots.empty()) {
        ordered_json slots = ordered_json::object();
        for (const auto& [kind, path] : project.slots) slots[kind] = path;
        doc["slots"] = std::move(slots);
    }
    if (!project.asset_roots.empty()) doc["asset_roots"] = project.asset_roots;
    out = doc.dump(2);
    out += '\n';
    return true;
}

bool save_project(const std::string& path, const Project& project, std::string& err) {
    std::string text;
    if (!write_project_json(project, text, err)) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot open " + tmp + " for writing";
            return false;
        }
        out << text;
        if (!out.good()) {
            err = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        err = "cannot move " + tmp + " over " + path + ": " + ec.message();
        return false;
    }
    return true;
}

}  // namespace dungeon_geometry_generator
