#pragma once

// Debug storey plans (requirements §15, F13): one SVG per storey. B1 draws
// the volume footprints and, on the topmost plan, the roof ridge/valley/hip
// projection; spaces/walls/openings join from B2 on. No dependencies — the
// SVG is assembled as text.

#include <string>

#include "ir.h"
#include "massing.h"

namespace building_topology_generator {

// +Z (the facade) is drawn at the bottom of the sheet. Meters, 40 px/m.
std::string storey_plan_svg(const Ir& ir, int storey_index, const Site& site);

}  // namespace building_topology_generator
