// BuildingGenerator B1 / G1: program loading and validation (F1 — an error
// names the key, the expectation, the fact and the fix; N7 — foreign format
// rejected with a hint) and the count/optional expansion.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "codes.h"
#include "program.h"

namespace {

using building_topology_generator::AtticMode;
using building_topology_generator::ExpandedProgram;
using building_topology_generator::Program;
namespace bt = building_topology_generator;

Program load(const std::string& name) {
    Program p;
    std::string err;
    const bool ok =
        bt::load_program_file(std::string(BUILDING_PROGRAMS_DIR) + "/" + name, p, err);
    EXPECT_TRUE(ok) << err;
    return p;
}

TEST(BuildingProgram, LoadsTemplates) {
    for (const char* name : {"house.json", "bakery.json", "tavern.json"}) {
        SCOPED_TRACE(name);
        const Program p = load(name);
        EXPECT_FALSE(p.rooms.empty());
        EXPECT_FALSE(p.entrance_into.empty());
        EXPECT_GT(p.style.roof_kind, 0);
    }
}

TEST(BuildingProgram, HouseShape) {
    const Program p = load("house.json");
    ASSERT_EQ(p.rooms.size(), 5u);
    EXPECT_EQ(p.storeys_min, 1);
    EXPECT_EQ(p.storeys_max, 2);
    EXPECT_EQ(p.attic, AtticMode::Allowed);
    EXPECT_EQ(p.links.size(), 3u);
    const Program* self = &p;
    const auto bed =
        std::find_if(self->rooms.begin(), self->rooms.end(),
                     [](const auto& r) { return r.id == "bed"; });
    ASSERT_NE(bed, p.rooms.end());
    EXPECT_EQ(bed->count_min, 1);
    EXPECT_EQ(bed->count_max, 3);
}

TEST(BuildingProgram, TavernRaisesGroundFloor) {
    const Program p = load("tavern.json");
    EXPECT_GT(p.h_ground, 2.8) << "§5.3: the tavern raises the ground floor";
}

TEST(BuildingProgram, ExpandCountAndOptional) {
    const Program p = load("house.json");
    const ExpandedProgram e = bt::expand_program(p);
    // lobby, hall, parlour, pantry, bed x3 = 7 instances.
    ASSERT_EQ(e.rooms.size(), 7u);
    int required = 0, optional = 0;
    bool saw_bed0 = false, saw_bed2 = false;
    for (const auto& r : e.rooms) {
        (r.optional ? optional : required) += 1;
        if (r.id == "bed.0") {
            saw_bed0 = true;
            EXPECT_FALSE(r.optional);
        }
        if (r.id == "bed.2") {
            saw_bed2 = true;
            EXPECT_TRUE(r.optional);
        }
    }
    EXPECT_TRUE(saw_bed0 && saw_bed2);
    EXPECT_EQ(required, 4);
    EXPECT_EQ(optional, 3);  // pantry + bed.1 + bed.2
    EXPECT_DOUBLE_EQ(e.required_area(), 3.5 + 15.0 + 13.0 + 11.0);
    // Links fan out over instances; same-id self links are dropped.
    for (const auto& l : e.links) EXPECT_NE(l.a, l.b);
    // Required first, then optional.
    size_t first_optional = 0;
    while (first_optional < e.rooms.size() && !e.rooms[first_optional].optional)
        ++first_optional;
    for (size_t i = first_optional; i < e.rooms.size(); ++i) EXPECT_TRUE(e.rooms[i].optional);
}

TEST(BuildingProgram, RejectsForeignFormat) {
    Program p;
    std::string err;
    EXPECT_FALSE(bt::read_program_json("{\"format\": \"building-ir/0\"}", p, err));
    EXPECT_NE(err.find("unsupported program format"), std::string::npos) << err;
    EXPECT_NE(err.find(bt::kProgramFormat), std::string::npos) << "N7 hint: " << err;
}

TEST(BuildingProgram, ErrorsNameKeyExpectationFactFix) {
    struct Case {
        const char* json;
        const char* want;  // substring of the error
    };
    const Case cases[] = {
        // Unknown key: lists the valid keys (the fix).
        {"{\"format\":\"building-program/0\",\"type\":\"x\",\"storeys\":{\"min\":1,\"max\":1},"
         "\"entrance\":{\"facade\":\"front\",\"into\":[\"a\"]},"
         "\"rooms\":[{\"id\":\"a\",\"fn\":\"lobby\",\"area\":[2,4],\"storey\":[\"ground\"],"
         "\"zone\":\"public\",\"windows\":1}]}",
         "unknown key \"windows\""},
        // Bad range: states the expectation and the fact.
        {"{\"format\":\"building-program/0\",\"type\":\"x\",\"storeys\":{\"min\":1,\"max\":1},"
         "\"entrance\":{\"facade\":\"front\",\"into\":[\"a\"]},"
         "\"rooms\":[{\"id\":\"a\",\"fn\":\"lobby\",\"area\":[4,2],\"storey\":[\"ground\"],"
         "\"zone\":\"public\"}]}",
         "0 < min <= max"},
        // Unknown fn.
        {"{\"format\":\"building-program/0\",\"type\":\"x\",\"storeys\":{\"min\":1,\"max\":1},"
         "\"entrance\":{\"facade\":\"front\",\"into\":[\"a\"]},"
         "\"rooms\":[{\"id\":\"a\",\"fn\":\"dungeon\",\"area\":[2,4],\"storey\":[\"ground\"],"
         "\"zone\":\"public\"}]}",
         "unknown function \"dungeon\""},
        // Dangling link reference.
        {"{\"format\":\"building-program/0\",\"type\":\"x\",\"storeys\":{\"min\":1,\"max\":1},"
         "\"entrance\":{\"facade\":\"front\",\"into\":[\"a\"]},"
         "\"rooms\":[{\"id\":\"a\",\"fn\":\"lobby\",\"area\":[2,4],\"storey\":[\"ground\"],"
         "\"zone\":\"public\"}],\"links\":[{\"a\":\"a\",\"b\":\"ghost\",\"kind\":\"door\"}]}",
         "unknown room id \"ghost\""},
        // Entrance into a room that cannot take it.
        {"{\"format\":\"building-program/0\",\"type\":\"x\",\"storeys\":{\"min\":1,\"max\":1},"
         "\"entrance\":{\"facade\":\"front\",\"into\":[\"a\"]},"
         "\"rooms\":[{\"id\":\"a\",\"fn\":\"storage\",\"area\":[2,4],\"storey\":[\"ground\"],"
         "\"zone\":\"service\"}]}",
         "cannot take the entrance"},
        // Attic-only room with a forbidden attic.
        {"{\"format\":\"building-program/0\",\"type\":\"x\",\"storeys\":{\"min\":1,\"max\":1,"
         "\"attic\":\"forbidden\"},\"entrance\":{\"facade\":\"front\",\"into\":[\"a\"]},"
         "\"rooms\":[{\"id\":\"a\",\"fn\":\"lobby\",\"area\":[2,4],\"storey\":[\"ground\"],"
         "\"zone\":\"public\"},{\"id\":\"b\",\"fn\":\"bedroom\",\"area\":[8,12],"
         "\"storey\":[\"attic\"],\"zone\":\"private\"}]}",
         "attic"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.want);
        Program p;
        std::string err;
        EXPECT_FALSE(bt::read_program_json(c.json, p, err));
        EXPECT_NE(err.find(c.want), std::string::npos) << err;
    }
}

TEST(BuildingProgram, FnDefaults) {
    EXPECT_TRUE(bt::fn_defaults(bt::kFnBedroom).habitable);
    EXPECT_FALSE(bt::fn_defaults(bt::kFnStorage).habitable);
    EXPECT_TRUE(bt::fn_defaults(bt::kFnLobby).entrance_ok);
    EXPECT_FALSE(bt::fn_defaults(bt::kFnBedroom).entrance_ok);
}

}  // namespace
