#pragma once

// Solution pins (requirements §4.5): "pin what you liked, regenerate the
// rest". A pin is a separate input from the program template — it belongs to
// a building instance (plot, seed). Format "building-pin/0". B1 pins the
// massing (G2); cores are parsed and carried, they constrain G4 from B3 on.
//
// --pin-from <ir.json> extracts the pin from a saved IR: the massing is the
// volumes + roof specs, the cores are the stairs/chimneys (B3+).

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "massing.h"

namespace building_topology_generator {

inline constexpr const char* kPinFormat = "building-pin/0";

struct Pin {
    bool has_massing = false;
    std::vector<MassingVolume> massing_volumes;
    bool has_cores = false;          // parsed, carried; applied from B3 (G4)
    std::string cores_note;          // raw count summary for the report
};

bool read_pin_json(const std::string& text, Pin& out, std::string& err);
bool load_pin_file(const std::string& path, Pin& out, std::string& err);

// what: comma-separated selector, "massing" and/or "cores" (§4.5).
bool pin_from_ir(const Ir& ir, const std::string& what, Pin& out, std::string& err);

}  // namespace building_topology_generator
