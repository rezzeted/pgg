#pragma once

// DungeonGeometryGeneratorViewer template tabs (project-tree iteration A/B): a card per
// catalog entry (explicit template or parametric instance) with the contour
// canvas, door mode, transforms, fill overrides and usage lists, and cards
// for the two parametric generators (ranges + thumbnails of the generated
// templates). Explicit templates and generators are editable: field edits
// write straight into level.project (single-project editor — the panel.h
// action-struct pattern does not scale to per-field edits); cross-cutting
// state (dirty flag, save+apply, tab opens, room selection) still goes back
// to main.cpp as actions.

#include <string>

#include "project_tree.h"

struct Level;

// Persistent per-tab UI state (the contour canvas camera).
struct TemplateViewState {
    float offsetX = 40.0f;
    float offsetY = 40.0f;
    float zoom = 28.0f;  // points per cell
    bool fitted = false; // first-frame auto-fit done
};

struct TemplateViewActions {
    bool openTab = false;      // generator thumbnail / "open generator" click
    ProjectTreeSelection tab;
    bool selectRoom = false;   // usage row click: select the room
    std::string roomId;
    bool markDirty = false;    // a field edit mutated level.project
    bool saveApply = false;    // "Save & Re-layout" button
};

// Tab label ("vault_L", "rect_4x6", "gen: rooms_rect").
std::string templateTabLabel(const ProjectTreeSelection& sel);

// Body of one template/generator tab. projectDirty shapes the Save button.
TemplateViewActions drawTemplateView(Level& level, const ProjectTreeSelection& sel,
                                     TemplateViewState& st, bool projectDirty);
