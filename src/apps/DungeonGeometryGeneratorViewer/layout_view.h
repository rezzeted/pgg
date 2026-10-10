#pragma once

// DungeonGeometryGeneratorViewer Layout tab: the project passage graph (the layout
// tier) as a node view, opened from the project tree's "Layout" row. Unlike
// the Topo tab's graph pane it does not need a generated layout: before
// Generate every room is placed by a deterministic BFS-layered fallback, and
// once the layout exists the real centroids take over (same world-cell
// space, so the camera carries over). Rendering reuses the Topo graph pane.

#include "level.h"
#include "panel.h"
#include "topo_view.h"

// Builds the node-view model: build_topo over the generated layout when the
// level is generated, else build_topo over an empty layout plus fallback
// positions for every graph room.
void buildLayoutGraphModel(const Level& level, dungeon_geometry_generator::TopoModel& out);

// Tab body: an info line (fallback notice before the first generate) over
// the Topo graph canvas.
TopoGraphResult drawLayoutGraphView(const Level& level,
                                    const dungeon_geometry_generator::TopoModel& model,
                                    Selection& selection, TopoGraphState& st);
