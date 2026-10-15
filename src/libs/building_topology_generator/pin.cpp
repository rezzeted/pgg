#include "pch.h"

#include "pin.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "codes.h"

namespace building_topology_generator {

namespace {

using nlohmann::json;

bool read_vec2(const json& j, glm::dvec2& out, std::string& err, const std::string& path) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        err = path + ": expected [x, z]";
        return false;
    }
    out = glm::dvec2(j[0].get<double>(), j[1].get<double>());
    return true;
}

// One pinned volume: outline in meters (building frame, §5.1), storeys,
// attic flag, roof spec. pitch_by_edge is authoritative; a bare "pitch" is
// expanded by the roof kind for rect outlines only.
bool read_pin_volume(const json& j, MassingVolume& out, std::string& err, const std::string& path) {
    if (!j.is_object()) {
        err = path + ": expected an object";
        return false;
    }
    if (!j.contains("outline") || !j["outline"].is_array() || j["outline"].size() < 3) {
        err = path + ": \"outline\" needs >= 3 points";
        return false;
    }
    for (const json& p : j["outline"]) {
        glm::dvec2 v;
        if (!read_vec2(p, v, err, path + ".outline")) return false;
        out.outline.push_back(v);
    }
    if (!j.contains("storeys") || !j["storeys"].is_number_integer()) {
        err = path + ": \"storeys\": expected an integer";
        return false;
    }
    out.storeys = j["storeys"].get<int>();
    if (out.storeys < 1 || out.storeys > 4) {
        err = path + ".storeys: expected 1..4";
        return false;
    }
    out.attic = j.value("attic", false);

    if (!j.contains("roof") || !j["roof"].is_object()) {
        err = path + ": missing \"roof\"";
        return false;
    }
    const json& r = j["roof"];
    const std::string kind = r.value("kind", "");
    out.roof_kind = roof_kind_code(kind);
    if (out.roof_kind == 0) {
        err = path + ".roof.kind: unknown roof \"" + kind + "\" (valid: gable, hip, shed)";
        return false;
    }
    out.eave_rise = r.value("eave_rise", 0.0);
    if (r.contains("pitch_by_edge")) {
        if (!r["pitch_by_edge"].is_array() ||
            r["pitch_by_edge"].size() != out.outline.size()) {
            err = path + ".roof.pitch_by_edge: size must match the outline vertex count";
            return false;
        }
        for (const json& p : r["pitch_by_edge"]) {
            if (!p.is_number()) {
                err = path + ".roof.pitch_by_edge: expected numbers";
                return false;
            }
            out.pitch_by_edge.push_back(p.get<double>());
        }
    } else if (r.contains("pitch")) {
        if (!r["pitch"].is_number()) {
            err = path + ".roof.pitch: expected a number";
            return false;
        }
        const double pitch = r["pitch"].get<double>();
        if (out.outline.size() != 4) {
            err = path + ".roof: \"pitch\" expands only for rect outlines; use "
                         "\"pitch_by_edge\" for L outlines";
            return false;
        }
        out.pitch_by_edge = out.roof_kind == kRoofGable
                                ? std::vector<double>{90.0, pitch, 90.0, pitch}
                                : std::vector<double>(4, pitch);
    } else {
        err = path + ".roof: missing \"pitch\" or \"pitch_by_edge\"";
        return false;
    }
    return true;
}

}  // namespace

bool read_pin_json(const std::string& text, Pin& out, std::string& err) {
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
    if (format != kPinFormat) {
        err = "unsupported pin format \"" + format + "\", expected \"" + kPinFormat +
              "\" (check the file is a building pin, not a program or an IR)";
        return false;
    }
    Pin pin;
    if (j.contains("massing")) {
        const json& m = j["massing"];
        if (!m.is_object() || !m.contains("volumes") || !m["volumes"].is_array() ||
            m["volumes"].empty()) {
            err = "pin.massing: expected an object with a non-empty \"volumes\" array";
            return false;
        }
        for (size_t i = 0; i < m["volumes"].size(); ++i) {
            MassingVolume v;
            const std::string path =
                "pin.massing.volumes[" + std::to_string(i) + "]";
            if (!read_pin_volume(m["volumes"][i], v, err, path)) return false;
            pin.massing_volumes.push_back(v);
        }
        pin.has_massing = true;
    }
    if (j.contains("cores")) {
        if (!j["cores"].is_array()) {
            err = "pin.cores: expected an array";
            return false;
        }
        pin.has_cores = true;
        pin.cores_note = std::to_string(j["cores"].size()) + " core(s)";
    }
    out = std::move(pin);
    return true;
}

bool load_pin_file(const std::string& path, Pin& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    std::ostringstream s;
    s << in.rdbuf();
    if (!read_pin_json(s.str(), out, err)) {
        err = path + ": " + err;
        return false;
    }
    return true;
}

bool pin_from_ir(const Ir& ir, const std::string& what, Pin& out, std::string& err) {
    bool want_massing = false, want_cores = false;
    std::string cur;
    std::istringstream parts(what);
    while (std::getline(parts, cur, ',')) {
        if (cur == "massing") want_massing = true;
        else if (cur == "cores") want_cores = true;
        else {
            err = "unknown pin selector \"" + cur + "\" (valid: massing, cores)";
            return false;
        }
    }
    if (!want_massing && !want_cores) {
        err = "empty pin selector (valid: massing, cores)";
        return false;
    }
    Pin pin;
    if (want_massing) {
        if (ir.volumes.empty()) {
            err = "pin-from: the IR has no volumes to pin";
            return false;
        }
        for (const IrVolume& vol : ir.volumes) {
            MassingVolume v;
            v.outline = vol.outline;
            int full = 0;
            bool attic = false;
            for (int k = vol.storey0; k <= vol.storey1 && k < static_cast<int>(ir.storeys.size());
                 ++k) {
                if (ir.storeys[k].kind_code == kStoreyAttic) attic = true;
                else ++full;
            }
            v.storeys = full;
            v.attic = attic;
            v.roof_kind = vol.roof.kind_code;
            v.pitch_by_edge = vol.roof.pitch_by_edge;
            v.eave_rise = vol.roof.eave_rise;
            pin.massing_volumes.push_back(v);
        }
        pin.has_massing = true;
    }
    if (want_cores) {
        // G4 is B3: the cores are parsed and carried, they constrain nothing
        // yet. The report notes the carry.
        pin.has_cores = true;
        pin.cores_note = std::to_string(ir.stairs.size()) + " stair(s), " +
                         std::to_string(ir.chimneys.size()) + " chimney(s) from the IR";
    }
    out = std::move(pin);
    return true;
}

}  // namespace building_topology_generator
