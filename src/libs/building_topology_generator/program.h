#pragma once

// Building program (requirements §4.1, stage G1): loads and validates
// "building-program/0" (F1: an error names the key, the expectation, the
// fact and the fix), expands count into instances (bed.0, bed.1) with
// optional marking, and estimates the required net area per storey band
// for massing (G2): sum of the room targets + the communications share.

#include <cstdint>
#include <string>
#include <vector>

namespace building_topology_generator {

inline constexpr const char* kProgramFormat = "building-program/0";

// Fn defaults (§4.1): the room function sets the minimum plan dimension,
// the daylight need, whether an entrance may lead into it and the door
// count limit. G5/G6 consume these from B2 on; G1/G2 already need
// habitable (attic usefulness) and min_dim (candidate sanity).
struct FnDefaults {
    double min_dim = 1.5;   // m, smaller plan side (I10)
    bool habitable = true;  // needs a window (I6)
    bool entrance_ok = false;
    int max_doors = 2;
};
const FnDefaults& fn_defaults(int fn_code);

// Attic usage declared by the program (§4.1 storeys.attic).
enum class AtticMode { Forbidden, Allowed, Required };

struct ProgramRoom {
    std::string id;
    int fn_code = 0;
    double area_min = 0.0, area_max = 0.0;
    uint32_t storeys_mask = 0;  // bit kStorey* (1<<code)
    int zone_code = 0;
    int hearth_code = 0;
    int count_min = 1, count_max = 1;
    bool optional = false;
};

struct ProgramLink {
    std::string a, b;   // room ids; a count room means every instance
    int kind = 0;       // kLink*
    std::string kind_name;
};

// Link kinds (§4.1). Not in codes.h: links never reach the IR/PGG.
inline constexpr int kLinkDoor = 1;
inline constexpr int kLinkNear = 2;
inline constexpr int kLinkNoDoor = 3;
inline constexpr int kLinkSameStorey = 4;

struct ProgramStyle {
    int roof_kind = 0;                 // kRoof*
    double pitch_min = 45.0, pitch_max = 55.0;  // degrees
    std::string chimney = "central";   // central | gable | free (§9.2, B4)
    std::string regularity = "loose";  // loose | strict (C6)
    double prop_min = 1.0, prop_max = 2.0;  // facade W/D range (G2)
};

struct Program {
    std::string type;  // "house" | "bakery" | "tavern" | ...
    int storeys_min = 1, storeys_max = 2;
    AtticMode attic = AtticMode::Allowed;
    std::string entrance_facade = "front";          // front|back|left|right
    std::vector<std::string> entrance_into;         // room ids
    bool has_back_door = false;
    std::string back_door_facade = "back";
    std::vector<std::string> back_door_into;
    std::vector<ProgramRoom> rooms;
    std::vector<ProgramLink> links;
    ProgramStyle style;
    double h_ground = 0.0;  // 0 = default from IrParams (§5.3: tavern/workshop may raise it)
};

// F1: unknown key, wrong type, broken range, unknown enum value, dangling
// reference — every error names the JSON path, the expectation, the fact
// and the fix. Foreign format is rejected with a hint (N7).
bool read_program_json(const std::string& text, Program& out, std::string& err);
bool load_program_file(const std::string& path, Program& out, std::string& err);

// G1 expansion: count -> instances id.0..id.(max-1); instances beyond
// count_min and optional rooms are marked optional (G2/G3 decide whether
// they fit). The area target is the range midpoint.
struct RoomInstance {
    std::string id;  // "bed.1" for count rooms, "hall" otherwise
    std::string base_id;
    int fn_code = 0;
    int zone_code = 0;
    double area_min = 0.0, area_max = 0.0, target = 0.0;
    uint32_t storeys_mask = 0;
    int hearth_code = 0;
    bool optional = false;
};

struct ExpandedProgram {
    std::string type;
    ProgramStyle style;
    int storeys_min = 1, storeys_max = 2;
    AtticMode attic = AtticMode::Allowed;
    double h_ground = 0.0;
    std::vector<RoomInstance> rooms;  // required first, then optional; stable id order inside
    std::vector<ProgramLink> links;   // instance-resolved (count rooms fan out)
    std::string entrance_facade;
    std::vector<std::string> entrance_into;    // instance ids (all instances)
    bool has_back_door = false;
    std::string back_door_facade;
    std::vector<std::string> back_door_into;

    double required_area() const;   // sum of targets of non-optional rooms
    double optional_area() const;   // sum of targets of optional rooms
    int hearth_count() const;       // rooms carrying a hearth (required + optional)
};

ExpandedProgram expand_program(const Program& p);

}  // namespace building_topology_generator
