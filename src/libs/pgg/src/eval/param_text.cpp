#include "pch.h"

#include "param_text.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "geo_file.h"

namespace pgg {

bool isFileParamRef(const std::string& text) {
    return text.size() > 1 && text.front() == '@' && text[1] != '@';
}

Value parseScalarParamText(const std::string& text) {
    if (text == "true") return Value(true);
    if (text == "false") return Value(false);
    if (text.size() >= 5 && text.front() == '(' && text.back() == ')') {
        std::vector<float> comps;
        std::stringstream ss(text.substr(1, text.size() - 2));
        std::string item;
        bool ok = true;
        while (std::getline(ss, item, ',')) {
            char* end = nullptr;
            const float f = std::strtof(item.c_str(), &end);
            if (end == item.c_str() || *end != '\0') ok = false;
            comps.push_back(f);
        }
        if (ok && comps.size() == 2) return Value(glm::vec2(comps[0], comps[1]));
        if (ok && comps.size() == 3) return Value(glm::vec3(comps[0], comps[1], comps[2]));
        if (ok && comps.size() == 4) return Value(glm::vec4(comps[0], comps[1], comps[2], comps[3]));
        return Value(text);
    }
    char* end = nullptr;
    const long long iv = std::strtoll(text.c_str(), &end, 10);
    if (end && *end == '\0' && end != text.c_str()) return Value(static_cast<int64_t>(iv));
    const float fv = std::strtof(text.c_str(), &end);
    if (end && *end == '\0' && end != text.c_str()) return Value(fv);
    return Value(text);
}

bool parseParamText(const std::string& text, const std::string& baseDir, Value& out, std::string* err) {
    if (isFileParamRef(text)) {
        std::filesystem::path p(text.substr(1));
        if (p.is_relative() && !baseDir.empty()) p = std::filesystem::path(baseDir) / p;
        GeoPtr geo;
        std::string loadErr;
        if (!loadPointsGeo(p.string(), geo, &loadErr)) {
            if (err) *err = loadErr;
            return false;
        }
        out = Value(std::move(geo));
        return true;
    }
    if (text.size() > 1 && text.front() == '@' && text[1] == '@') {
        out = Value(text.substr(1));
        return true;
    }
    out = parseScalarParamText(text);
    return true;
}

}  // namespace pgg
