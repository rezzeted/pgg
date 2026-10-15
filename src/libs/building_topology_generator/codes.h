#pragma once

// Building domain int codes (requirements §10): every string field in the IR
// is duplicated by an int code for the PGG assets (PGG cannot read string
// fields). These tables must match content/pgg/Building/codes.pgg — the
// parity is enforced by building_assets_test.

#include <string_view>

namespace building_topology_generator {

// Room functions (program dictionary, requirements §4.1).
inline constexpr int kFnLobby = 1;
inline constexpr int kFnHall = 2;
inline constexpr int kFnKitchen = 3;
inline constexpr int kFnLiving = 4;
inline constexpr int kFnBedroom = 5;
inline constexpr int kFnStorage = 6;
inline constexpr int kFnShop = 7;
inline constexpr int kFnWorkshop = 8;
inline constexpr int kFnBakehouse = 9;
inline constexpr int kFnTaproom = 10;
inline constexpr int kFnCorridor = 11;

// Privacy zones (§4.1).
inline constexpr int kZonePublic = 1;
inline constexpr int kZoneService = 2;
inline constexpr int kZonePrivate = 3;

// Wall classes (§5.2, §7.8).
inline constexpr int kWallExterior = 1;
inline constexpr int kWallBearing = 2;
inline constexpr int kWallPartition = 3;
inline constexpr int kWallGable = 4;
inline constexpr int kWallKnee = 5;

// Opening kinds (§5.4).
inline constexpr int kOpeningDoor = 1;
inline constexpr int kOpeningWindow = 2;

// Roof kinds (§7.2).
inline constexpr int kRoofGable = 1;
inline constexpr int kRoofHip = 2;
inline constexpr int kRoofShed = 3;

// Hearth kinds (§9.2).
inline constexpr int kHearthFireplace = 1;
inline constexpr int kHearthOven = 2;
inline constexpr int kHearthForge = 3;

// Stair types (§9.1).
inline constexpr int kStairStraight = 1;
inline constexpr int kStairL = 2;
inline constexpr int kStairLadder = 3;

// Anchor kinds (§10).
inline constexpr int kAnchorEntrance = 1;
inline constexpr int kAnchorHearth = 2;
inline constexpr int kAnchorBedSlot = 3;
inline constexpr int kAnchorLight = 4;

// Storey kinds (§2).
inline constexpr int kStoreyGround = 1;
inline constexpr int kStoreyUpper = 2;
inline constexpr int kStoreyAttic = 3;

std::string_view fn_name(int code);
int fn_code(std::string_view name);
std::string_view zone_name(int code);
int zone_code(std::string_view name);
std::string_view wall_class_name(int code);
int wall_class_code(std::string_view name);
std::string_view opening_kind_name(int code);
int opening_kind_code(std::string_view name);
std::string_view roof_kind_name(int code);
int roof_kind_code(std::string_view name);
std::string_view hearth_kind_name(int code);
int hearth_kind_code(std::string_view name);
std::string_view stair_type_name(int code);
int stair_type_code(std::string_view name);
std::string_view anchor_kind_name(int code);
int anchor_kind_code(std::string_view name);
std::string_view storey_kind_name(int code);
int storey_kind_code(std::string_view name);

}  // namespace building_topology_generator
