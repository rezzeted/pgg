#include "pch.h"

#include "codes.h"

namespace building_topology_generator {

namespace {

struct Entry {
    int code;
    std::string_view name;
};

constexpr Entry kFnNames[] = {
    {kFnLobby, "lobby"},       {kFnHall, "hall"},         {kFnKitchen, "kitchen"},
    {kFnLiving, "living"},     {kFnBedroom, "bedroom"},   {kFnStorage, "storage"},
    {kFnShop, "shop"},         {kFnWorkshop, "workshop"}, {kFnBakehouse, "bakehouse"},
    {kFnTaproom, "taproom"},   {kFnCorridor, "corridor"},
};
constexpr Entry kZoneNames[] = {
    {kZonePublic, "public"}, {kZoneService, "service"}, {kZonePrivate, "private"},
};
constexpr Entry kWallClassNames[] = {
    {kWallExterior, "exterior"}, {kWallBearing, "bearing"}, {kWallPartition, "partition"},
    {kWallGable, "gable"},       {kWallKnee, "knee"},
};
constexpr Entry kOpeningKindNames[] = {{kOpeningDoor, "door"}, {kOpeningWindow, "window"}};
constexpr Entry kRoofKindNames[] = {
    {kRoofGable, "gable"}, {kRoofHip, "hip"}, {kRoofShed, "shed"},
};
constexpr Entry kHearthKindNames[] = {
    {kHearthFireplace, "fireplace"}, {kHearthOven, "oven"}, {kHearthForge, "forge"},
};
constexpr Entry kStairTypeNames[] = {
    {kStairStraight, "straight"}, {kStairL, "l"}, {kStairLadder, "ladder"},
};
constexpr Entry kAnchorKindNames[] = {
    {kAnchorEntrance, "entrance"}, {kAnchorHearth, "hearth"},
    {kAnchorBedSlot, "bed_slot"},  {kAnchorLight, "light"},
};
constexpr Entry kStoreyKindNames[] = {
    {kStoreyGround, "ground"}, {kStoreyUpper, "upper"}, {kStoreyAttic, "attic"},
};

std::string_view name_of(const Entry* table, size_t n, int code) {
    for (size_t i = 0; i < n; ++i)
        if (table[i].code == code) return table[i].name;
    return {};
}
int code_of(const Entry* table, size_t n, std::string_view name) {
    for (size_t i = 0; i < n; ++i)
        if (table[i].name == name) return table[i].code;
    return 0;
}

}  // namespace

std::string_view fn_name(int code) { return name_of(kFnNames, std::size(kFnNames), code); }
int fn_code(std::string_view name) { return code_of(kFnNames, std::size(kFnNames), name); }
std::string_view zone_name(int code) { return name_of(kZoneNames, std::size(kZoneNames), code); }
int zone_code(std::string_view name) { return code_of(kZoneNames, std::size(kZoneNames), name); }
std::string_view wall_class_name(int code) { return name_of(kWallClassNames, std::size(kWallClassNames), code); }
int wall_class_code(std::string_view name) { return code_of(kWallClassNames, std::size(kWallClassNames), name); }
std::string_view opening_kind_name(int code) { return name_of(kOpeningKindNames, std::size(kOpeningKindNames), code); }
int opening_kind_code(std::string_view name) { return code_of(kOpeningKindNames, std::size(kOpeningKindNames), name); }
std::string_view roof_kind_name(int code) { return name_of(kRoofKindNames, std::size(kRoofKindNames), code); }
int roof_kind_code(std::string_view name) { return code_of(kRoofKindNames, std::size(kRoofKindNames), name); }
std::string_view hearth_kind_name(int code) { return name_of(kHearthKindNames, std::size(kHearthKindNames), code); }
int hearth_kind_code(std::string_view name) { return code_of(kHearthKindNames, std::size(kHearthKindNames), name); }
std::string_view stair_type_name(int code) { return name_of(kStairTypeNames, std::size(kStairTypeNames), code); }
int stair_type_code(std::string_view name) { return code_of(kStairTypeNames, std::size(kStairTypeNames), name); }
std::string_view anchor_kind_name(int code) { return name_of(kAnchorKindNames, std::size(kAnchorKindNames), code); }
int anchor_kind_code(std::string_view name) { return code_of(kAnchorKindNames, std::size(kAnchorKindNames), name); }
std::string_view storey_kind_name(int code) { return name_of(kStoreyKindNames, std::size(kStoreyKindNames), code); }
int storey_kind_code(std::string_view name) { return code_of(kStoreyKindNames, std::size(kStoreyKindNames), name); }

}  // namespace building_topology_generator
