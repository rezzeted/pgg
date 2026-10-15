#include "pch.h"

#include "program.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "codes.h"

namespace building_topology_generator {

namespace {

// ---------------------------------------------------------------------------
// Fn defaults (§4.1)
// ---------------------------------------------------------------------------

FnDefaults make_fn(double min_dim, bool habitable, bool entrance_ok, int max_doors) {
    FnDefaults d;
    d.min_dim = min_dim;
    d.habitable = habitable;
    d.entrance_ok = entrance_ok;
    d.max_doors = max_doors;
    return d;
}

}  // namespace

const FnDefaults& fn_defaults(int fn_code) {
    static const FnDefaults kLobby = make_fn(1.2, false, true, 3);
    static const FnDefaults kHall = make_fn(2.5, true, true, 4);
    static const FnDefaults kKitchen = make_fn(2.0, true, true, 2);
    static const FnDefaults kLiving = make_fn(2.5, true, false, 2);
    static const FnDefaults kBedroom = make_fn(2.2, true, false, 1);
    static const FnDefaults kStorage = make_fn(1.2, false, false, 1);
    static const FnDefaults kShop = make_fn(2.5, true, true, 2);
    static const FnDefaults kWorkshop = make_fn(2.5, true, false, 2);
    static const FnDefaults kBakehouse = make_fn(2.5, true, false, 2);
    static const FnDefaults kTaproom = make_fn(3.0, true, true, 3);
    static const FnDefaults kCorridor = make_fn(1.0, false, false, 8);
    static const FnDefaults kFallback = make_fn(1.5, true, false, 2);
    switch (fn_code) {
        case kFnLobby: return kLobby;
        case kFnHall: return kHall;
        case kFnKitchen: return kKitchen;
        case kFnLiving: return kLiving;
        case kFnBedroom: return kBedroom;
        case kFnStorage: return kStorage;
        case kFnShop: return kShop;
        case kFnWorkshop: return kWorkshop;
        case kFnBakehouse: return kBakehouse;
        case kFnTaproom: return kTaproom;
        case kFnCorridor: return kCorridor;
        default: return kFallback;
    }
}

namespace {

using nlohmann::json;

std::string valid_keys(std::initializer_list<const char*> keys) {
    std::string t;
    for (const char* k : keys) {
        if (!t.empty()) t += ", ";
        t += k;
    }
    return t;
}

// F1: reject keys the schema does not know; the message lists the valid set.
bool no_unknown_keys(const json& j, std::initializer_list<const char*> keys, const std::string& path,
                     std::string& err) {
    for (const auto& [key, _] : j.items()) {
        bool known = false;
        for (const char* k : keys)
            if (key == k) {
                known = true;
                break;
            }
        if (!known) {
            err = path + ": unknown key \"" + key + "\" (valid: " + valid_keys(keys) + ")";
            return false;
        }
    }
    return true;
}

bool read_num(const json& j, const char* key, double& out, const std::string& path, std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    if (!j[key].is_number()) {
        err = path + "." + key + ": expected a number, got " + j[key].type_name();
        return false;
    }
    out = j[key].get<double>();
    return true;
}

bool read_int(const json& j, const char* key, int& out, const std::string& path, std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    if (!j[key].is_number_integer()) {
        err = path + "." + key + ": expected an integer, got " + j[key].type_name();
        return false;
    }
    out = j[key].get<int>();
    return true;
}

bool read_str(const json& j, const char* key, std::string& out, const std::string& path, std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    if (!j[key].is_string()) {
        err = path + "." + key + ": expected a string, got " + j[key].type_name();
        return false;
    }
    out = j[key].get<std::string>();
    return true;
}

bool read_range(const json& j, const char* key, double& lo, double& hi, const std::string& path,
                std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    const json& r = j[key];
    if (!r.is_array() || r.size() != 2 || !r[0].is_number() || !r[1].is_number()) {
        err = path + "." + key + ": expected [min, max] numbers";
        return false;
    }
    lo = r[0].get<double>();
    hi = r[1].get<double>();
    if (!(lo > 0.0) || !(hi >= lo)) {
        err = path + "." + key + ": expected 0 < min <= max, got [" + r[0].dump() + ", " + r[1].dump() +
              "]";
        return false;
    }
    return true;
}

bool read_irange(const json& j, const char* key, int& lo, int& hi, const std::string& path,
                 std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    const json& r = j[key];
    if (!r.is_array() || r.size() != 2 || !r[0].is_number_integer() || !r[1].is_number_integer()) {
        err = path + "." + key + ": expected [min, max] integers";
        return false;
    }
    lo = r[0].get<int>();
    hi = r[1].get<int>();
    if (lo < 1 || hi < lo) {
        err = path + "." + key + ": expected 1 <= min <= max, got [" + r[0].dump() + ", " + r[1].dump() +
              "]";
        return false;
    }
    return true;
}

bool read_str_list(const json& j, const char* key, std::vector<std::string>& out, const std::string& path,
                   std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    const json& a = j[key];
    if (!a.is_array() || a.empty()) {
        err = path + "." + key + ": expected a non-empty array of room ids";
        return false;
    }
    for (const json& e : a) {
        if (!e.is_string()) {
            err = path + "." + key + ": expected room id strings";
            return false;
        }
        out.push_back(e.get<std::string>());
    }
    return true;
}

uint32_t storey_bit(int storey_code) { return 1u << storey_code; }

bool read_storeys_list(const json& j, const char* key, uint32_t& mask, const std::string& path,
                       std::string& err) {
    if (!j.contains(key)) {
        err = path + ": missing \"" + key + "\"";
        return false;
    }
    const json& a = j[key];
    if (!a.is_array() || a.empty()) {
        err = path + "." + key + ": expected a non-empty array (\"ground\", \"upper\", \"attic\")";
        return false;
    }
    mask = 0;
    for (const json& e : a) {
        if (!e.is_string()) {
            err = path + "." + key + ": expected storey name strings";
            return false;
        }
        const std::string name = e.get<std::string>();
        const int code = storey_kind_code(name);
        if (code == 0) {
            err = path + "." + key + ": unknown storey \"" + name +
                  "\" (valid: ground, upper, attic)";
            return false;
        }
        mask |= storey_bit(code);
    }
    return true;
}

bool read_facade(const json& j, const char* key, std::string& out, const std::string& path,
                 std::string& err) {
    if (!read_str(j, key, out, path, err)) return false;
    if (out != "front" && out != "back" && out != "left" && out != "right") {
        err = path + "." + key + ": unknown facade \"" + out + "\" (valid: front, back, left, right)";
        return false;
    }
    return true;
}

bool read_room(const json& j, ProgramRoom& room, const std::string& path, std::string& err) {
    if (!j.is_object()) {
        err = path + ": expected an object";
        return false;
    }
    if (!no_unknown_keys(j,
                         {"id", "fn", "area", "storey", "zone", "hearth", "count", "optional"}, path,
                         err))
        return false;
    if (!read_str(j, "id", room.id, path, err)) return false;
    std::string fn;
    if (!read_str(j, "fn", fn, path, err)) return false;
    room.fn_code = fn_code(fn);
    if (room.fn_code == 0) {
        err = path + ".fn: unknown function \"" + fn + "\" (check docs/building_generator/requirements.md §4.1)";
        return false;
    }
    if (!read_range(j, "area", room.area_min, room.area_max, path, err)) return false;
    if (!read_storeys_list(j, "storey", room.storeys_mask, path, err)) return false;
    std::string zone;
    if (!read_str(j, "zone", zone, path, err)) return false;
    room.zone_code = zone_code(zone);
    if (room.zone_code == 0) {
        err = path + ".zone: unknown zone \"" + zone + "\" (valid: public, service, private)";
        return false;
    }
    if (j.contains("hearth")) {
        if (!j["hearth"].is_string()) {
            err = path + ".hearth: expected a string";
            return false;
        }
        const std::string h = j["hearth"].get<std::string>();
        room.hearth_code = hearth_kind_code(h);
        if (room.hearth_code == 0) {
            err = path + ".hearth: unknown hearth \"" + h + "\" (valid: fireplace, oven, forge)";
            return false;
        }
    }
    if (j.contains("count")) {
        if (!read_irange(j, "count", room.count_min, room.count_max, path, err)) return false;
    }
    if (j.contains("optional")) {
        if (!j["optional"].is_boolean()) {
            err = path + ".optional: expected a boolean";
            return false;
        }
        room.optional = j["optional"].get<bool>();
    }
    return true;
}

bool read_link(const json& j, ProgramLink& link, const std::string& path, std::string& err) {
    if (!j.is_object()) {
        err = path + ": expected an object";
        return false;
    }
    if (!no_unknown_keys(j, {"a", "b", "kind"}, path, err)) return false;
    if (!read_str(j, "a", link.a, path, err)) return false;
    if (!read_str(j, "b", link.b, path, err)) return false;
    if (!read_str(j, "kind", link.kind_name, path, err)) return false;
    if (link.kind_name == "door") link.kind = kLinkDoor;
    else if (link.kind_name == "near") link.kind = kLinkNear;
    else if (link.kind_name == "no_door") link.kind = kLinkNoDoor;
    else if (link.kind_name == "same_storey") link.kind = kLinkSameStorey;
    else {
        err = path + ".kind: unknown link \"" + link.kind_name +
              "\" (valid: door, near, no_door, same_storey)";
        return false;
    }
    return true;
}

bool find_room(const Program& p, const std::string& id) {
    for (const ProgramRoom& r : p.rooms)
        if (r.id == id) return true;
    return false;
}

// §4.1: an instance reference "bed.1" resolves to the base room bed.
bool find_ref(const Program& p, const std::string& ref, std::string& base) {
    base = ref;
    const size_t dot = ref.rfind('.');
    if (dot != std::string::npos) {
        const std::string stem = ref.substr(0, dot);
        const std::string tail = ref.substr(dot + 1);
        if (!tail.empty() &&
            std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; }))
            base = stem;
    }
    return find_room(p, base);
}

}  // namespace

bool read_program_json(const std::string& text, Program& out, std::string& err) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        err = "invalid JSON";
        return false;
    }
    if (!j.is_object()) {
        err = "expected a JSON object at the top level";
        return false;
    }
    const std::string format = j.value("format", "");
    if (format != kProgramFormat) {
        err = "unsupported program format \"" + format + "\", expected \"" + kProgramFormat +
              "\" (check the file is a building program, not an IR or a pin)";
        return false;
    }
    if (!no_unknown_keys(j,
                         {"format", "type", "storeys", "entrance", "back_door", "rooms", "links",
                          "style", "h_ground"},
                         "program", err))
        return false;

    Program p;
    if (!read_str(j, "type", p.type, "program", err)) return false;

    if (!j.contains("storeys") || !j["storeys"].is_object()) {
        err = "program: missing \"storeys\" object";
        return false;
    }
    {
        const json& s = j["storeys"];
        if (!no_unknown_keys(s, {"min", "max", "attic"}, "program.storeys", err)) return false;
        if (!read_int(s, "min", p.storeys_min, "program.storeys", err)) return false;
        if (!read_int(s, "max", p.storeys_max, "program.storeys", err)) return false;
        if (p.storeys_min < 1 || p.storeys_max < p.storeys_min || p.storeys_max > 4) {
            err = "program.storeys: expected 1 <= min <= max <= 4, got [" +
                  std::to_string(p.storeys_min) + ", " + std::to_string(p.storeys_max) + "]";
            return false;
        }
        const std::string attic = s.value("attic", "allowed");
        if (attic == "forbidden") p.attic = AtticMode::Forbidden;
        else if (attic == "allowed") p.attic = AtticMode::Allowed;
        else if (attic == "required") p.attic = AtticMode::Required;
        else {
            err = "program.storeys.attic: unknown mode \"" + attic +
                  "\" (valid: forbidden, allowed, required)";
            return false;
        }
    }

    if (j.contains("h_ground")) {
        if (!read_num(j, "h_ground", p.h_ground, "program", err)) return false;
        if (p.h_ground < 2.2 || p.h_ground > 5.0) {
            err = "program.h_ground: expected 2.2..5.0 m, got " + std::to_string(p.h_ground);
            return false;
        }
    }

    if (j.contains("entrance")) {
        const json& e = j["entrance"];
        if (!e.is_object()) {
            err = "program.entrance: expected an object";
            return false;
        }
        if (!no_unknown_keys(e, {"facade", "into"}, "program.entrance", err)) return false;
        if (!read_facade(e, "facade", p.entrance_facade, "program.entrance", err)) return false;
        if (!read_str_list(e, "into", p.entrance_into, "program.entrance", err)) return false;
    } else {
        err = "program: missing \"entrance\"";
        return false;
    }

    if (j.contains("back_door")) {
        const json& e = j["back_door"];
        if (!e.is_object()) {
            err = "program.back_door: expected an object";
            return false;
        }
        if (!no_unknown_keys(e, {"facade", "into"}, "program.back_door", err)) return false;
        p.has_back_door = true;
        if (!read_facade(e, "facade", p.back_door_facade, "program.back_door", err)) return false;
        if (!read_str_list(e, "into", p.back_door_into, "program.back_door", err)) return false;
    }

    if (!j.contains("rooms") || !j["rooms"].is_array() || j["rooms"].empty()) {
        err = "program: missing \"rooms\" (a non-empty array)";
        return false;
    }
    for (size_t i = 0; i < j["rooms"].size(); ++i) {
        ProgramRoom room;
        const std::string path = "program.rooms[" + std::to_string(i) + "]";
        if (!read_room(j["rooms"][i], room, path, err)) return false;
        for (const ProgramRoom& prev : p.rooms)
            if (prev.id == room.id) {
                err = path + ".id: duplicate room id \"" + room.id + "\"";
                return false;
            }
        p.rooms.push_back(room);
    }

    if (j.contains("links")) {
        if (!j["links"].is_array()) {
            err = "program.links: expected an array";
            return false;
        }
        for (size_t i = 0; i < j["links"].size(); ++i) {
            ProgramLink link;
            const std::string path = "program.links[" + std::to_string(i) + "]";
            if (!read_link(j["links"][i], link, path, err)) return false;
            std::string base;
            for (const std::string* ref : {&link.a, &link.b}) {
                if (!find_ref(p, *ref, base)) {
                    err = path + ": unknown room id \"" + *ref + "\" (link endpoints use ids from rooms[])";
                    return false;
                }
            }
            p.links.push_back(link);
        }
    }

    if (j.contains("style")) {
        const json& s = j["style"];
        if (!s.is_object()) {
            err = "program.style: expected an object";
            return false;
        }
        if (!no_unknown_keys(s, {"roof", "pitch", "chimney", "regularity", "proportion"},
                             "program.style", err))
            return false;
        if (s.contains("roof")) {
            std::string roof;
            if (!read_str(s, "roof", roof, "program.style", err)) return false;
            p.style.roof_kind = roof_kind_code(roof);
            if (p.style.roof_kind == 0) {
                err = "program.style.roof: unknown roof \"" + roof + "\" (valid: gable, hip, shed)";
                return false;
            }
        }
        if (s.contains("pitch") &&
            !read_range(s, "pitch", p.style.pitch_min, p.style.pitch_max, "program.style", err))
            return false;
        if (p.style.pitch_min < 20.0 || p.style.pitch_max > 65.0) {
            err = "program.style.pitch: expected 20..65 degrees";
            return false;
        }
        if (s.contains("chimney")) {
            if (!read_str(s, "chimney", p.style.chimney, "program.style", err)) return false;
            if (p.style.chimney != "central" && p.style.chimney != "gable" && p.style.chimney != "free") {
                err = "program.style.chimney: unknown placement \"" + p.style.chimney +
                      "\" (valid: central, gable, free)";
                return false;
            }
        }
        if (s.contains("regularity")) {
            if (!read_str(s, "regularity", p.style.regularity, "program.style", err)) return false;
            if (p.style.regularity != "loose" && p.style.regularity != "strict") {
                err = "program.style.regularity: unknown value \"" + p.style.regularity +
                      "\" (valid: loose, strict)";
                return false;
            }
        }
        if (s.contains("proportion") && !read_range(s, "proportion", p.style.prop_min,
                                                    p.style.prop_max, "program.style", err))
            return false;
    }

    // Cross-reference checks.
    for (size_t i = 0; i < p.entrance_into.size(); ++i) {
        std::string base;
        if (!find_ref(p, p.entrance_into[i], base)) {
            err = "program.entrance.into[" + std::to_string(i) + "]: unknown room id \"" +
                  p.entrance_into[i] + "\"";
            return false;
        }
        for (const ProgramRoom& r : p.rooms)
            if (r.id == base && !fn_defaults(r.fn_code).entrance_ok) {
                err = "program.entrance.into: \"" + p.entrance_into[i] + "\" (" +
                      std::string(fn_name(r.fn_code)) + ") cannot take the entrance; pick a lobby, "
                      "hall, shop or taproom";
                return false;
            }
    }
    for (size_t i = 0; i < p.back_door_into.size(); ++i) {
        std::string base;
        if (!find_ref(p, p.back_door_into[i], base)) {
            err = "program.back_door.into[" + std::to_string(i) + "]: unknown room id \"" +
                  p.back_door_into[i] + "\"";
            return false;
        }
    }
    // A room pinned to the attic needs the attic to exist.
    if (p.attic == AtticMode::Forbidden)
        for (const ProgramRoom& r : p.rooms)
            if (!r.optional && (r.storeys_mask & storey_bit(kStoreyAttic)) &&
                !(r.storeys_mask & (storey_bit(kStoreyGround) | storey_bit(kStoreyUpper)))) {
                err = "program.rooms: \"" + r.id +
                      "\" allows only the attic but storeys.attic is \"forbidden\"";
                return false;
            }

    out = std::move(p);
    return true;
}

bool load_program_file(const std::string& path, Program& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    std::ostringstream s;
    s << in.rdbuf();
    if (!read_program_json(s.str(), out, err)) {
        err = path + ": " + err;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// G1 expansion
// ---------------------------------------------------------------------------

namespace {

void resolve_refs(const Program& p, const std::vector<std::string>& refs, std::vector<std::string>& out) {
    for (const std::string& ref : refs) {
        std::string base;
        find_ref(p, ref, base);
        for (const ProgramRoom& r : p.rooms)
            if (r.id == base) {
                if (r.count_max == 1) {
                    out.push_back(r.id);
                } else {
                    for (int i = 0; i < r.count_max; ++i)
                        out.push_back(r.id + "." + std::to_string(i));
                }
            }
    }
}

}  // namespace

ExpandedProgram expand_program(const Program& p) {
    ExpandedProgram e;
    e.type = p.type;
    e.style = p.style;
    e.storeys_min = p.storeys_min;
    e.storeys_max = p.storeys_max;
    e.attic = p.attic;
    e.h_ground = p.h_ground;
    e.entrance_facade = p.entrance_facade;
    e.has_back_door = p.has_back_door;
    e.back_door_facade = p.back_door_facade;
    resolve_refs(p, p.entrance_into, e.entrance_into);
    resolve_refs(p, p.back_door_into, e.back_door_into);

    std::vector<RoomInstance> required, optional;
    for (const ProgramRoom& r : p.rooms) {
        for (int i = 0; i < r.count_max; ++i) {
            RoomInstance inst;
            inst.base_id = r.id;
            inst.id = r.count_max == 1 ? r.id : r.id + "." + std::to_string(i);
            inst.fn_code = r.fn_code;
            inst.zone_code = r.zone_code;
            inst.area_min = r.area_min;
            inst.area_max = r.area_max;
            inst.target = 0.5 * (r.area_min + r.area_max);
            inst.storeys_mask = r.storeys_mask;
            inst.hearth_code = r.hearth_code;
            inst.optional = r.optional || i >= r.count_min;
            (inst.optional ? optional : required).push_back(inst);
        }
    }
    e.rooms = required;
    e.rooms.insert(e.rooms.end(), optional.begin(), optional.end());

    for (const ProgramLink& link : p.links) {
        std::vector<std::string> as, bs;
        resolve_refs(p, {link.a}, as);
        resolve_refs(p, {link.b}, bs);
        for (const std::string& a : as)
            for (const std::string& b : bs) {
                if (a == b) continue;
                ProgramLink l;
                l.a = a;
                l.b = b;
                l.kind = link.kind;
                l.kind_name = link.kind_name;
                e.links.push_back(l);
            }
    }
    return e;
}

double ExpandedProgram::required_area() const {
    double a = 0.0;
    for (const RoomInstance& r : rooms)
        if (!r.optional) a += r.target;
    return a;
}

double ExpandedProgram::optional_area() const {
    double a = 0.0;
    for (const RoomInstance& r : rooms)
        if (r.optional) a += r.target;
    return a;
}

int ExpandedProgram::hearth_count() const {
    int n = 0;
    for (const RoomInstance& r : rooms)
        if (r.hearth_code != 0 && !r.optional) ++n;
    return n;
}

}  // namespace building_topology_generator
