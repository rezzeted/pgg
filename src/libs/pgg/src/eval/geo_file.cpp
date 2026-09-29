#include "pch.h"

#include "geo_file.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace {

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

bool asFloat(const nlohmann::json& j, float& out) {
    if (!j.is_number()) return false;
    out = static_cast<float>(j.get<double>());
    return true;
}

bool asInt64(const nlohmann::json& j, int64_t& out) {
    if (j.is_number_integer()) {
        out = j.get<int64_t>();
        return true;
    }
    if (j.is_number_unsigned()) {
        out = static_cast<int64_t>(j.get<uint64_t>());
        return true;
    }
    if (j.is_number_float()) {
        const double d = j.get<double>();
        if (std::trunc(d) != d) return false;
        out = static_cast<int64_t>(d);
        return true;
    }
    return false;
}

bool asBool(const nlohmann::json& j, uint8_t& out) {
    if (j.is_boolean()) {
        out = j.get<bool>() ? 1 : 0;
        return true;
    }
    int64_t i = 0;
    if (asInt64(j, i) && (i == 0 || i == 1)) {
        out = static_cast<uint8_t>(i);
        return true;
    }
    return false;
}

bool asVec3(const nlohmann::json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() != 3) return false;
    float x = 0, y = 0, z = 0;
    if (!asFloat(j[0], x) || !asFloat(j[1], y) || !asFloat(j[2], z)) return false;
    out = glm::vec3(x, y, z);
    return true;
}

bool asVec2(const nlohmann::json& j, glm::vec2& out) {
    if (!j.is_array() || j.size() != 2) return false;
    float x = 0, y = 0;
    if (!asFloat(j[0], x) || !asFloat(j[1], y)) return false;
    out = glm::vec2(x, y);
    return true;
}

bool asVec4(const nlohmann::json& j, glm::vec4& out) {
    if (!j.is_array() || j.size() != 4) return false;
    float x = 0, y = 0, z = 0, w = 0;
    if (!asFloat(j[0], x) || !asFloat(j[1], y) || !asFloat(j[2], z) || !asFloat(j[3], w)) return false;
    out = glm::vec4(x, y, z, w);
    return true;
}

std::string stripAt(std::string name) {
    if (!name.empty() && name.front() == '@') name.erase(name.begin());
    return name;
}

}  // namespace

namespace pgg {

bool loadPointsGeo(const std::string& path, GeoPtr& out, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setErr(err, "cannot open " + path);
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text.str());
    } catch (const nlohmann::json::parse_error& e) {
        setErr(err, path + ": invalid JSON at byte " + std::to_string(e.byte) + ": " + e.what());
        return false;
    }
    if (!doc.is_object()) {
        setErr(err, path + ": expected a JSON object with \"format\": \"" + kPointsFileFormat + "\"");
        return false;
    }
    const nlohmann::json fmt = doc.value("format", nlohmann::json());
    if (fmt != kPointsFileFormat) {
        setErr(err, path + ": unsupported format " +
                           (fmt.is_string() ? "\"" + fmt.get<std::string>() + "\"" : "(missing)") +
                           ", expected \"" + kPointsFileFormat + "\"");
        return false;
    }

    const auto posIt = doc.find("positions");
    if (posIt == doc.end() || !posIt->is_array()) {
        setErr(err, path + ": missing \"positions\" array");
        return false;
    }
    std::vector<glm::vec3> positions;
    positions.reserve(posIt->size());
    for (size_t i = 0; i < posIt->size(); ++i) {
        glm::vec3 p(0);
        if (!asVec3((*posIt)[i], p)) {
            setErr(err, path + ": positions[" + std::to_string(i) + "]: expected [x, y, z] numbers");
            return false;
        }
        positions.push_back(p);
    }
    const size_t count = positions.size();

    std::vector<glm::vec3> normals;
    bool hasNormals = false;
    if (const auto nIt = doc.find("normals"); nIt != doc.end()) {
        if (!nIt->is_array() || nIt->size() != count) {
            setErr(err, path + ": \"normals\" must be an array of " + std::to_string(count) +
                               " [x, y, z] entries");
            return false;
        }
        normals.reserve(count);
        for (size_t i = 0; i < nIt->size(); ++i) {
            glm::vec3 n(0);
            if (!asVec3((*nIt)[i], n)) {
                setErr(err, path + ": normals[" + std::to_string(i) + "]: expected [x, y, z] numbers");
                return false;
            }
            normals.push_back(n);
        }
        hasNormals = true;
    }

    AttrSet attrs;
    if (const auto aIt = doc.find("attrs"); aIt != doc.end()) {
        if (!aIt->is_object()) {
            setErr(err, path + ": \"attrs\" must be an object of {\"type\", \"values\"} entries");
            return false;
        }
        for (const auto& [rawName, spec] : aIt->items()) {
            const std::string name = stripAt(rawName);
            if (name.empty() || attrs.columns.count(name)) {
                setErr(err, path + ": attrs: invalid or duplicate name \"" + rawName + "\"");
                return false;
            }
            if (!spec.is_object()) {
                setErr(err, path + ": attrs." + rawName + ": expected {\"type\", \"values\"}");
                return false;
            }
            const std::string type = spec.value("type", std::string{});
            const auto vIt = spec.find("values");
            if (vIt == spec.end() || !vIt->is_array() || vIt->size() != count) {
                setErr(err, path + ": attrs." + rawName + ".values: expected an array of " +
                                   std::to_string(count) + " " + (type.empty() ? "values" : type) + " entries");
                return false;
            }
            const std::string where = path + ": attrs." + rawName + ".values";
            if (type == "f32") {
                std::vector<float> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    float f = 0;
                    if (!asFloat((*vIt)[i], f)) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected a number");
                        return false;
                    }
                    col.push_back(f);
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<float>>(std::move(col))};
            } else if (type == "int") {
                std::vector<int64_t> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    int64_t v = 0;
                    if (!asInt64((*vIt)[i], v)) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected an integer");
                        return false;
                    }
                    col.push_back(v);
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<int64_t>>(std::move(col))};
            } else if (type == "bool") {
                std::vector<uint8_t> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    uint8_t b = 0;
                    if (!asBool((*vIt)[i], b)) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected true/false or 0/1");
                        return false;
                    }
                    col.push_back(b);
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<uint8_t>>(std::move(col))};
            } else if (type == "vec2") {
                std::vector<glm::vec2> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    glm::vec2 v(0);
                    if (!asVec2((*vIt)[i], v)) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected [x, y] numbers");
                        return false;
                    }
                    col.push_back(v);
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<glm::vec2>>(std::move(col))};
            } else if (type == "vec3") {
                std::vector<glm::vec3> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    glm::vec3 v(0);
                    if (!asVec3((*vIt)[i], v)) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected [x, y, z] numbers");
                        return false;
                    }
                    col.push_back(v);
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<glm::vec3>>(std::move(col))};
            } else if (type == "vec4") {
                std::vector<glm::vec4> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    glm::vec4 v(0);
                    if (!asVec4((*vIt)[i], v)) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected [x, y, z, w] numbers");
                        return false;
                    }
                    col.push_back(v);
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<glm::vec4>>(std::move(col))};
            } else if (type == "string") {
                std::vector<std::string> col;
                col.reserve(count);
                for (size_t i = 0; i < vIt->size(); ++i) {
                    const nlohmann::json& j = (*vIt)[i];
                    if (!j.is_string()) {
                        setErr(err, where + "[" + std::to_string(i) + "]: expected a string");
                        return false;
                    }
                    col.push_back(j.get<std::string>());
                }
                attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<std::string>>(std::move(col))};
            } else {
                setErr(err, path + ": attrs." + rawName + ".type: expected one of " +
                                   "bool/int/f32/vec2/vec3/vec4/string, got \"" + type + "\"");
                return false;
            }
        }
    }

    GroupSet groups;
    if (const auto gIt = doc.find("groups"); gIt != doc.end()) {
        if (!gIt->is_object()) {
            setErr(err, path + ": \"groups\" must be an object of name -> [0/1, ...] arrays");
            return false;
        }
        for (const auto& [rawName, arr] : gIt->items()) {
            const std::string name = stripAt(rawName);
            if (name.empty() || groups.columns.count(name)) {
                setErr(err, path + ": groups: invalid or duplicate name \"" + rawName + "\"");
                return false;
            }
            if (!arr.is_array() || arr.size() != count) {
                setErr(err, path + ": groups." + rawName + ": expected an array of " +
                                   std::to_string(count) + " 0/1 entries");
                return false;
            }
            auto col = std::make_shared<BoolColumn>();
            col->reserve(count);
            for (size_t i = 0; i < arr.size(); ++i) {
                uint8_t b = 0;
                if (!asBool(arr[i], b)) {
                    setErr(err, path + ": groups." + rawName + "[" + std::to_string(i) +
                                       "]: expected true/false or 0/1");
                    return false;
                }
                col->push_back(b);
            }
            groups.columns[name] = std::move(col);
        }
    }

    GeoPtr geo = makePoints(std::move(positions));
    if (hasNormals) geo = withNormals(*geo, std::move(normals));
    if (!attrs.columns.empty())
        geo = withAttrs(*geo, Domain::Points, std::make_shared<const AttrSet>(std::move(attrs)));
    if (!groups.columns.empty())
        geo = withGroups(*geo, Domain::Points, std::make_shared<const GroupSet>(std::move(groups)));
    out = std::move(geo);
    return true;
}

bool savePointsGeo(const std::string& path, const Geo& geo, std::string* err) {
    if (geo.kind != GeoKind::Points) {
        setErr(err, path + ": expected geo<points>, got geo<" + geoKindName(geo.kind) + ">");
        return false;
    }
    const size_t count = geo.pointCount();
    if (geo.normals && geo.normals->size() != count) {
        setErr(err, path + ": @N column size does not match the point count");
        return false;
    }
    nlohmann::ordered_json doc;
    doc["format"] = kPointsFileFormat;
    nlohmann::ordered_json pos = nlohmann::ordered_json::array();
    for (size_t i = 0; i < count; ++i) {
        const glm::vec3& p = (*geo.positions)[i];
        pos.push_back({p.x, p.y, p.z});
    }
    doc["positions"] = std::move(pos);
    if (geo.normals) {
        nlohmann::ordered_json nrm = nlohmann::ordered_json::array();
        for (size_t i = 0; i < count; ++i) {
            const glm::vec3& n = (*geo.normals)[i];
            nrm.push_back({n.x, n.y, n.z});
        }
        doc["normals"] = std::move(nrm);
    }
    if (geo.pointAttrs && !geo.pointAttrs->columns.empty()) {
        std::vector<std::string> names;
        for (const auto& [name, col] : geo.pointAttrs->columns) names.push_back(name);
        std::sort(names.begin(), names.end());
        nlohmann::ordered_json attrs = nlohmann::ordered_json::object();
        for (const std::string& name : names) {
            const AttrColumn& col = geo.pointAttrs->columns.at(name);
            if (col.size() != count) {
                setErr(err, path + ": attrs." + name + ": column size does not match the point count");
                return false;
            }
            nlohmann::ordered_json spec;
            nlohmann::ordered_json vals = nlohmann::ordered_json::array();
            switch (col.data.index()) {
                case 0: {  // float
                    spec["type"] = "f32";
                    for (float v : *std::get<0>(col.data)) vals.push_back(v);
                    break;
                }
                case 1: {  // int64_t
                    spec["type"] = "int";
                    for (int64_t v : *std::get<1>(col.data)) vals.push_back(v);
                    break;
                }
                case 2: {  // bool
                    spec["type"] = "bool";
                    for (uint8_t v : *std::get<2>(col.data)) vals.push_back(v != 0);
                    break;
                }
                case 3: {  // vec2
                    spec["type"] = "vec2";
                    for (const glm::vec2& v : *std::get<3>(col.data)) vals.push_back({v.x, v.y});
                    break;
                }
                case 4: {  // vec3
                    spec["type"] = "vec3";
                    for (const glm::vec3& v : *std::get<4>(col.data)) vals.push_back({v.x, v.y, v.z});
                    break;
                }
                case 5: {  // vec4
                    spec["type"] = "vec4";
                    for (const glm::vec4& v : *std::get<5>(col.data))
                        vals.push_back({v.x, v.y, v.z, v.w});
                    break;
                }
                case 6: {  // string
                    spec["type"] = "string";
                    for (const std::string& v : *std::get<6>(col.data)) vals.push_back(v);
                    break;
                }
                default:
                    setErr(err, path + ": attrs." + name + ": unsupported column type");
                    return false;
            }
            spec["values"] = std::move(vals);
            attrs[name] = std::move(spec);
        }
        doc["attrs"] = std::move(attrs);
    }
    if (geo.pointGroups && !geo.pointGroups->columns.empty()) {
        std::vector<std::string> names;
        for (const auto& [name, col] : geo.pointGroups->columns) names.push_back(name);
        std::sort(names.begin(), names.end());
        nlohmann::ordered_json groups = nlohmann::ordered_json::object();
        for (const std::string& name : names) {
            const ConstBoolColumnPtr& col = geo.pointGroups->columns.at(name);
            if (!col || col->size() != count) {
                setErr(err, path + ": groups." + name + ": column size does not match the point count");
                return false;
            }
            nlohmann::ordered_json arr = nlohmann::ordered_json::array();
            for (uint8_t v : *col) arr.push_back(v != 0);
            groups[name] = std::move(arr);
        }
        doc["groups"] = std::move(groups);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        setErr(err, "cannot write " + path);
        return false;
    }
    out << doc.dump(1) << "\n";
    out.close();
    if (!out) {
        setErr(err, "cannot write " + path);
        return false;
    }
    return true;
}

}  // namespace pgg
