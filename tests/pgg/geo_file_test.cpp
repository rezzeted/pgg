// geo<points> file payloads (pgg-points/1) and launch-param text parsing
// (scalars + @path): round-trips, validation errors, scalar parity with the
// historical --param rules, and the geo<points> runtime binding check.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "pgg/eval.h"
#include "pgg/src/eval/geo_file.h"
#include "pgg/src/eval/param_text.h"

namespace {

std::filesystem::path testDir() {
    static const std::filesystem::path dir =
        std::filesystem::path(testing::TempDir()) / "pgg_geo_file_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeFile(const std::filesystem::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    ASSERT_TRUE(out) << p.string();
}

pgg::GeoPtr makeSamplePoints() {
    std::vector<glm::vec3> pos{glm::vec3(1.5f, 0.0f, -2.0f), glm::vec3(0.25f, 3.0f, 4.5f),
                               glm::vec3(-1.0f, -0.5f, 0.0f)};
    pgg::GeoPtr g = pgg::makePoints(std::move(pos));
    g = pgg::withNormals(*g, {glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)});
    pgg::AttrSet attrs;
    attrs.columns["room"] = pgg::AttrColumn{
        std::make_shared<const std::vector<int64_t>>(std::vector<int64_t>{7, 7, 9}), pgg::AttrTypeInfo::None};
    attrs.columns["w"] = pgg::AttrColumn{
        std::make_shared<const std::vector<float>>(std::vector<float>{0.5f, 1.5f, 2.5f}),
        pgg::AttrTypeInfo::None};
    attrs.columns["Cd"] = pgg::AttrColumn{
        std::make_shared<const std::vector<glm::vec3>>(
            std::vector<glm::vec3>{glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)}),
        pgg::AttrTypeInfo::None};
    attrs.columns["tag"] = pgg::AttrColumn{
        std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"a", "b", "c"}),
        pgg::AttrTypeInfo::None};
    g = pgg::withAttrs(*g, pgg::Domain::Points, std::make_shared<const pgg::AttrSet>(std::move(attrs)));
    pgg::GroupSet groups;
    groups.columns["sel"] = std::make_shared<const pgg::BoolColumn>(pgg::BoolColumn{1, 0, 1});
    g = pgg::withGroups(*g, pgg::Domain::Points, std::make_shared<const pgg::GroupSet>(std::move(groups)));
    return g;
}

const std::vector<int64_t>& intColumn(const pgg::Geo& geo, const std::string& name) {
    const pgg::AttrColumn* col = geo.pointAttrs->find(name);
    EXPECT_NE(col, nullptr) << name;
    return *std::get<std::shared_ptr<const std::vector<int64_t>>>(col->data);
}

TEST(GeoFile, SaveLoadRoundTrip) {
    const std::filesystem::path p = testDir() / "roundtrip.points.json";
    const pgg::GeoPtr src = makeSamplePoints();
    std::string err;
    ASSERT_TRUE(pgg::savePointsGeo(p.string(), *src, &err)) << err;

    pgg::GeoPtr loaded;
    ASSERT_TRUE(pgg::loadPointsGeo(p.string(), loaded, &err)) << err;
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->kind, pgg::GeoKind::Points);
    EXPECT_EQ(loaded->pointCount(), 3u);
    EXPECT_EQ(*loaded->positions, *src->positions);
    ASSERT_NE(loaded->normals, nullptr);
    EXPECT_EQ(*loaded->normals, *src->normals);
    ASSERT_NE(loaded->pointAttrs, nullptr);
    EXPECT_EQ(intColumn(*loaded, "room"), (std::vector<int64_t>{7, 7, 9}));
    const pgg::AttrColumn* w = loaded->pointAttrs->find("w");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(*std::get<std::shared_ptr<const std::vector<float>>>(w->data),
              (std::vector<float>{0.5f, 1.5f, 2.5f}));
    const pgg::AttrColumn* cd = loaded->pointAttrs->find("Cd");
    ASSERT_NE(cd, nullptr);
    EXPECT_EQ(*std::get<std::shared_ptr<const std::vector<glm::vec3>>>(cd->data),
              (std::vector<glm::vec3>{glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)}));
    const pgg::AttrColumn* tag = loaded->pointAttrs->find("tag");
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(*std::get<std::shared_ptr<const std::vector<std::string>>>(tag->data),
              (std::vector<std::string>{"a", "b", "c"}));
    ASSERT_NE(loaded->pointGroups, nullptr);
    const pgg::ConstBoolColumnPtr sel = loaded->pointGroups->find("sel");
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(*sel, (pgg::BoolColumn{1, 0, 1}));
}

TEST(GeoFile, SaveLoadEmpty) {
    const std::filesystem::path p = testDir() / "empty.points.json";
    const pgg::GeoPtr src = pgg::makePoints({});
    std::string err;
    ASSERT_TRUE(pgg::savePointsGeo(p.string(), *src, &err)) << err;
    pgg::GeoPtr loaded;
    ASSERT_TRUE(pgg::loadPointsGeo(p.string(), loaded, &err)) << err;
    EXPECT_EQ(loaded->pointCount(), 0u);
    EXPECT_EQ(loaded->pointAttrs, nullptr);
}

TEST(GeoFile, LoadErrors) {
    pgg::GeoPtr out;
    std::string err;
    EXPECT_FALSE(pgg::loadPointsGeo((testDir() / "no-such-file.json").string(), out, &err));
    EXPECT_NE(err.find("cannot open"), std::string::npos) << err;

    const std::filesystem::path bad = testDir() / "bad.points.json";
    writeFile(bad, "{not json");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("invalid JSON"), std::string::npos) << err;

    writeFile(bad, R"({"format": "pgg-points/9", "positions": []})");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("unsupported format"), std::string::npos) << err;

    writeFile(bad, R"({"format": "pgg-points/1"})");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("positions"), std::string::npos) << err;

    writeFile(bad, R"({"format": "pgg-points/1", "positions": [[0, 0]]})");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("positions[0]"), std::string::npos) << err;

    writeFile(bad, R"({"format": "pgg-points/1", "positions": [[0, 0, 0]],
        "attrs": {"room": {"type": "int", "values": [1, 2]}}})");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("attrs.room.values"), std::string::npos) << err;

    writeFile(bad, R"({"format": "pgg-points/1", "positions": [[0, 0, 0]],
        "attrs": {"x": {"type": "f128", "values": [1.0]}}})");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("attrs.x.type"), std::string::npos) << err;

    writeFile(bad, R"({"format": "pgg-points/1", "positions": [[0, 0, 0]],
        "groups": {"sel": [0, 1]}})");
    EXPECT_FALSE(pgg::loadPointsGeo(bad.string(), out, &err));
    EXPECT_NE(err.find("groups.sel"), std::string::npos) << err;
}

TEST(GeoFile, SaveRejectsMesh) {
    const pgg::GeoPtr mesh = pgg::makeMesh({glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)}, {0, 1, 2},
                                            {0, 3});
    std::string err;
    EXPECT_FALSE(pgg::savePointsGeo((testDir() / "mesh.points.json").string(), *mesh, &err));
    EXPECT_NE(err.find("geo<points>"), std::string::npos) << err;
}

TEST(ParamText, ScalarParity) {
    using pgg::parseScalarParamText;
    EXPECT_TRUE(pgg::asBool(parseScalarParamText("true")));
    EXPECT_FALSE(pgg::asBool(parseScalarParamText("false")));
    EXPECT_EQ(pgg::asInt(parseScalarParamText("42")), 42);
    EXPECT_EQ(pgg::asInt(parseScalarParamText("-7")), -7);
    EXPECT_FLOAT_EQ(pgg::asF32(parseScalarParamText("1.5")), 1.5f);
    EXPECT_EQ(pgg::asVec2(parseScalarParamText("(1, 2)")), glm::vec2(1, 2));
    EXPECT_EQ(pgg::asVec3(parseScalarParamText("(1, 2, 3)")), glm::vec3(1, 2, 3));
    EXPECT_EQ(pgg::asVec4(parseScalarParamText("(1, 2, 3, 4)")), glm::vec4(1, 2, 3, 4));
    EXPECT_EQ(pgg::asString(parseScalarParamText("hello")), "hello");
    // Malformed vectors fall back to a string (historical behavior).
    EXPECT_EQ(pgg::asString(parseScalarParamText("(1, x)")), "(1, x)");
}

TEST(ParamText, FileRefAndEscape) {
    EXPECT_TRUE(pgg::isFileParamRef("@rooms.json"));
    EXPECT_FALSE(pgg::isFileParamRef("@@rooms.json"));
    EXPECT_FALSE(pgg::isFileParamRef("@"));
    EXPECT_FALSE(pgg::isFileParamRef("plain"));

    const std::filesystem::path dir = testDir() / "params";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    writeFile(dir / "rooms.points.json", R"({"format": "pgg-points/1",
        "positions": [[1, 0, 2], [3, 0, 4]],
        "attrs": {"room": {"type": "int", "values": [5, 6]}}})");

    pgg::Value v;
    std::string err;
    ASSERT_TRUE(pgg::parseParamText("@rooms.points.json", dir.string(), v, &err)) << err;
    EXPECT_EQ(pgg::valueBase(v), pgg::ScalarType::Geo);
    const pgg::GeoPtr g = pgg::asGeo(v);
    EXPECT_EQ(g->pointCount(), 2u);
    EXPECT_EQ(intColumn(*g, "room"), (std::vector<int64_t>{5, 6}));

    EXPECT_TRUE(pgg::parseParamText("@@literal", dir.string(), v, &err));
    EXPECT_EQ(pgg::asString(v), "@literal");

    EXPECT_FALSE(pgg::parseParamText("@missing.json", dir.string(), v, &err));
    EXPECT_NE(err.find("cannot open"), std::string::npos) << err;
}

TEST(ParamText, GeoPointsRuntimeBinding) {
    // DungeonGeometryGenerator requirements §9.2: a host binds geometry into a geo<points> param
    // through RunParams — the declaration typechecks and the run sees the value.
    const std::string src = "param pts: geo<points>\nn = count(pts)\noutput n\n";
    pgg::RunParams rp;
    rp.values.push_back({"pts", pgg::Value(makeSamplePoints())});
    const pgg::RunResult r = pgg::run(src, rp);
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    ASSERT_FALSE(r.hasErrors());
    ASSERT_EQ(r.outputs.size(), 1u);
    EXPECT_EQ(r.outputs[0].name, "n");
    EXPECT_EQ(pgg::asInt(r.outputs[0].value), 3);
}

}  // namespace
