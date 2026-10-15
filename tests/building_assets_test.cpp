// BuildingGenerator B0: asset library autonomy (every slot asset runs from its
// fixture: no E-diagnostics, non-empty mesh, mesh + anchors outputs) + codes
// parity (codes.pgg def-consts vs building_topology_generator codes.h tables).

#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "pgg/eval.h"
#include "pgg/src/eval/param_text.h"

#include "codes.h"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Fixture JSON value -> param text: strings verbatim (@file refs), arrays as
// (x, y, z) vectors, scalars via JSON dump.
std::string jsonToParamText(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_array()) {
        std::string t = "(";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) t += ", ";
            t += v[i].dump();
        }
        return t + ")";
    }
    return v.dump();
}

std::string diagText(const std::vector<pgg::Diagnostic>& ds) {
    std::string t;
    for (const auto& d : ds) {
        t += (d.isWarning ? "[W] " : "[E] ") + d.code + ": " + d.message;
        if (!d.hint.empty()) t += " (" + d.hint + ")";
        t += "\n";
    }
    return t;
}

const pgg::RunOutput* findOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return &o;
    return nullptr;
}

TEST(BuildingAssets, Autonomy) {
    const std::string assets = BUILDING_ASSETS_DIR;
    struct Case {
        const char* asset;    // relative to BUILDING_ASSETS_DIR
        const char* fixture;  // relative to BUILDING_ASSETS_DIR
    };
    const std::vector<Case> cases = {
        {"walls/body.pgg", "walls/body.fixture.json"},
        {"walls/body.pgg", "walls/body.gable.fixture.json"},
        {"walls/node.pgg", "walls/node.fixture.json"},
        {"floors/slab.pgg", "floors/slab.fixture.json"},
        {"floors/finish.pgg", "floors/finish.fixture.json"},
        {"stairs/stair_l.pgg", "stairs/stair_l.fixture.json"},
        {"roof/roof.pgg", "roof/roof.fixture.json"},
        {"ceilings/attic.pgg", "ceilings/attic.fixture.json"},
        {"chimneys/stack.pgg", "chimneys/stack.fixture.json"},
        {"hearths/fireplace.pgg", "hearths/fireplace.fixture.json"},
        {"openings/door_plain.pgg", "openings/door_plain.fixture.json"},
        {"openings/win_flat.pgg", "openings/win_flat.fixture.json"},
        {"rails/rail.pgg", "rails/rail.fixture.json"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.fixture);
        const std::string fixturePath = assets + "/" + c.fixture;
        const std::string text = readFile(fixturePath);
        ASSERT_FALSE(text.empty()) << "missing " << fixturePath;
        const nlohmann::json fixture = nlohmann::json::parse(text);
        ASSERT_TRUE(fixture.contains("params") && fixture["params"].is_object());

        const std::string fixtureDir =
            std::filesystem::path(fixturePath).parent_path().string();
        pgg::RunParams rp;
        // Slots import Building.lib / Building.codes / lib.arch.* (product lib).
        rp.importRoots = {assets, BUILDING_PGG_CONTENT_DIR};
        for (const auto& [name, value] : fixture["params"].items()) {
            pgg::Value bound;
            std::string err;
            const std::string paramText = jsonToParamText(value);
            ASSERT_TRUE(pgg::parseParamText(paramText, fixtureDir, bound, &err))
                << name << " = " << paramText << ": " << err;
            rp.values.emplace_back(name, bound);
        }
        const pgg::RunResult r = pgg::runFile(assets + "/" + c.asset, rp);
        EXPECT_FALSE(r.hasErrors()) << diagText(r.diagnostics);
        ASSERT_EQ(r.outputs.size(), 2u) << "slot contract: exactly mesh + anchors";

        const pgg::RunOutput* mesh = findOutput(r, "mesh");
        const pgg::RunOutput* anchors = findOutput(r, "anchors");
        ASSERT_NE(mesh, nullptr);
        ASSERT_NE(anchors, nullptr);
        ASSERT_EQ(pgg::asGeo(mesh->value)->kind, pgg::GeoKind::Mesh);
        EXPECT_GT(pgg::asGeo(mesh->value)->pointCount(), 0u);
        ASSERT_EQ(pgg::asGeo(anchors->value)->kind, pgg::GeoKind::Points);
        // B0 slots emit no anchors yet (anchors come from IrAnchor in fill).
        EXPECT_EQ(pgg::asGeo(anchors->value)->pointCount(), 0u);
    }
}

TEST(BuildingAssets, CodesParity) {
    struct Table {
        const char* prefix;
        int (*code)(std::string_view);
        std::string_view (*name)(int);
    };
    const std::vector<Table> tables = {
        {"FN", building_topology_generator::fn_code, building_topology_generator::fn_name},
        {"ZONE", building_topology_generator::zone_code, building_topology_generator::zone_name},
        {"WALL", building_topology_generator::wall_class_code,
         building_topology_generator::wall_class_name},
        {"OP", building_topology_generator::opening_kind_code,
         building_topology_generator::opening_kind_name},
        {"ROOF", building_topology_generator::roof_kind_code,
         building_topology_generator::roof_kind_name},
        {"HEARTH", building_topology_generator::hearth_kind_code,
         building_topology_generator::hearth_kind_name},
        {"STAIR", building_topology_generator::stair_type_code,
         building_topology_generator::stair_type_name},
        {"ANCHOR", building_topology_generator::anchor_kind_code,
         building_topology_generator::anchor_kind_name},
        {"STOREY", building_topology_generator::storey_kind_code,
         building_topology_generator::storey_kind_name},
    };
    const std::string text = readFile(std::string(BUILDING_ASSETS_DIR) + "/codes.pgg");
    ASSERT_FALSE(text.empty());
    const std::regex defRe(
        "def ([A-Z]+)_([A-Z_]+)\\(\\) -> \\(out: int\\) \\{[\\s\\S]*?out = (\\d+)");
    std::map<std::string, int> pggCodes;  // "PREFIX_lower_name" -> value
    for (std::sregex_iterator it(text.begin(), text.end(), defRe), end; it != end; ++it)
        pggCodes[(*it)[1].str() + "_" + lower((*it)[2].str())] = std::stoi((*it)[3].str());
    // 11 functions + 3 zones + 5 wall classes + 2 opening kinds + 3 roof kinds
    // + 3 hearth kinds + 3 stair types + 4 anchor kinds + 3 storey kinds.
    constexpr size_t kExpectedDefs = 11 + 3 + 5 + 2 + 3 + 3 + 3 + 4 + 3;
    EXPECT_EQ(pggCodes.size(), kExpectedDefs) << "codes.pgg gained/lost a def-const";
    for (const Table& t : tables) {
        // Every table entry appears in codes.pgg with the same value.
        for (int code = 1; code < 16; ++code) {
            const std::string_view name = t.name(code);
            if (name.empty()) break;
            SCOPED_TRACE(std::string(t.prefix) + "_" + std::string(name));
            const auto it = pggCodes.find(std::string(t.prefix) + "_" + lower(std::string(name)));
            ASSERT_NE(it, pggCodes.end()) << "missing in codes.pgg";
            EXPECT_EQ(it->second, code) << "codes.pgg value drift";
            EXPECT_EQ(t.code(name), code) << "table lookup drift";
        }
        // Bogus names map to 0 (unset).
        EXPECT_EQ(t.code("no_such_name"), 0) << t.prefix;
    }
}

}  // namespace
