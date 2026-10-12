#pragma once

#include <imgui.h>

namespace vv {

// Center map canvas: terrain texture + vector overlays (roads, parcels,
// buildings, anchors). Pan: middle-drag; zoom: wheel; reset: double-click.
void draw_map_preview(ImVec2 pos, ImVec2 size);

}  // namespace vv
