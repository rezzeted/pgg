#pragma once

// DungeonGeometryGeneratorViewer Layout tab: the project passage graph (the layout
// tier) as an EDITABLE node view, opened from the project tree's "Layout"
// row. Unlike the Topo tab's graph pane it does not need a generated layout:
// before Generate every room is placed by a deterministic BFS-layered
// fallback, and once the layout exists the real centroids take over for
// nodes the user has not placed by hand. Dragged positions persist in the
// project (layout.editor.node_pos, view-only metadata the generator
// ignores); rooms/passages can be added (toolbar modals or a wire drag from
// a node's port dot) and deleted, with the project cross-tier checks
// (validate_project_v1) gating Save/Generate, not the edits themselves (a
// fresh room is legitimately disconnected until it gets a passage).

#include "level.h"
#include "panel.h"
#include "topo_view.h"

// Builds the node-view model: build_topo over the generated layout when the
// level is generated, else build_topo over an empty layout. Node positions:
// the user's pinned editor_node_pos win, then the real layout centroids,
// then the fallback arrangement.
void buildLayoutGraphModel(const Level& level, dungeon_geometry_generator::TopoModel& out);

// Persistent per-tab editor state.
struct LayoutGraphState {
    TopoGraphState graph;  // camera, hover, fitted (shared fit helper)
    std::string dragId;    // node being LMB-dragged ("" = none)
    double dragGrabX = 0.0, dragGrabY = 0.0;  // world cursor -> node center offset at grab
    std::string connectFromId;                // port wire-drag source node ("" = none)
    std::string selEdgeA, selEdgeB;           // selected passage (selEdgeA empty = none)
    int hoverEdge = -1;                       // edge index under the cursor (-1 = none)
    // Add-room modal.
    bool addRoomOpen = false;
    char addRoomId[64] = {};
    int addRoomRole = 0;  // index into the fixed role set
    double addRoomX = 0.0, addRoomY = 0.0;  // canvas center at open (world cells)
    std::string addRoomErr;
    // Add-passage modal (endpoints are indices into the sorted room ids).
    bool addPassageOpen = false;
    int addPassageA = 0, addPassageB = 0;
    int addPassageDoor = 0;  // 0 = open, 1 = gate
    std::string addPassageErr;
};

// Actions for main.cpp (the panel.h result-flag pattern).
struct LayoutGraphActions {
    bool markDirty = false;     // level.project.layout was mutated in place
    bool graphChanged = false;  // rooms/passages added/removed (rebuild the Topo models too)
    bool selectionChanged = false;
    bool focus = false;         // double-click on a node: focus the camera on it
};

// Tab body: the toolbar (Add room / Add passage / Delete, Fit, summary), the
// node/wire editor canvas and the two modals. Edits write straight into
// level.project.layout (the template_view pattern: per-field edits do not
// scale through action structs).
LayoutGraphActions drawLayoutGraphView(Level& level, const dungeon_geometry_generator::TopoModel& model,
                                       Selection& selection, LayoutGraphState& st);
