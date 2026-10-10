#pragma once

// DungeonGeometryGeneratorViewer project tree (the left panel "Project" tab): a read-only
// tree of the loaded project — the layout tier (rooms, passages, templates
// with their parametric generators), fill params and slots. Clicks are
// reported back to main.cpp as actions (open a template tab, select a room);
// the tree itself never mutates viewer state (the panel.h pattern).

#include <string>

struct Level;

// Identity of a View-window tab the tree can ask to open.
struct ProjectTreeSelection {
    enum class Kind { None, Layout, Template, GeneratorRects, GeneratorCorridors } kind = Kind::None;
    std::string name;  // Kind::Template: the catalog entry name

    bool operator==(const ProjectTreeSelection& o) const {
        return kind == o.kind && name == o.name;
    }
};

struct ProjectTreeActions {
    bool openTab = false;      // open/raise the View tab for `tab`
    ProjectTreeSelection tab;
    bool selectRoom = false;   // set the F9 selection to `roomId`
    std::string roomId;
};

// The tree body (call inside a window; draws nothing without a loaded level).
ProjectTreeActions drawProjectTree(const Level& level);
