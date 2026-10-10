#pragma once

// DungeonGeometryGeneratorViewer Layout tab: the project passage graph (the layout
// tier) as an EDITABLE node view, opened from the project tree's "Layout"
// row. The canvas is the user's stable schematic: node positions are the
// pinned editor_node_pos or the deterministic BFS-layered fallback from the
// entry room — never the generated layout's centroids (the read-only Topo
// tab owns the "how the rooms actually landed" projection). Dragged
// positions persist in the project (layout.editor.node_pos, view-only
// metadata the generator ignores); broken pins (a missing room id, a
// non-finite/absurd coordinate — hand-edit damage) are dropped on load by
// sanitizeEditorPins with a Log line, the affected nodes fall back to the
// auto arrangement. Rooms/passages can be added (toolbar modals or a wire
// drag from a node's port dot) and deleted, with the project cross-tier
// checks (validate_project_v1) gating Save/Generate, not the edits
// themselves (a fresh room is legitimately disconnected until it gets a
// passage).

#include "level.h"
#include "panel.h"
#include "topo_view.h"

// Builds the node-view model: build_topo over an empty layout (structure
// only), then pin-or-fallback positions for every node.
void buildLayoutGraphModel(const Level& level, dungeon_geometry_generator::TopoModel& out);

// Drop broken editor_node_pos entries (a room id that does not exist or a
// non-finite/absurd coordinate). Returns the count; `dropped` gets the
// comma-separated room ids for the log line.
size_t sanitizeEditorPins(dungeon_geometry_generator::LayoutParams& g, std::string& dropped);

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
