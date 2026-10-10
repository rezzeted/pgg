#pragma once

// DungeonGeometryGeneratorViewer data layer (F9): owns the project -> layout -> IR -> fill
// pipeline state so the UI reads plain structs. The UnitCache (F8) outlives
// individual fills; refill/relayout reuse it and expose the reuse stats.

#include <string>

#include "catalog.h"
#include "fill.h"
#include "ir.h"
#include "layout.h"
#include "project.h"

struct Level {
    dungeon_geometry_generator::Project project;
    dungeon_geometry_generator::layout::Catalog catalog;  // F2 catalog of the loaded layout tier
    dungeon_geometry_generator::LayoutData layoutData;
    dungeon_geometry_generator::IrV2 ir;
    dungeon_geometry_generator::FillResult fill;
    dungeon_geometry_generator::UnitCache unitCache;  // F8: position-independent unit outputs

    std::string projectPath;
    std::string dungeon_geometry_generatorAssets;  // resolved assets dir (FillOpts::dungeon_geometry_generator_assets)
    double layoutMs = 0.0;    // last successful generate
    double fillMs = 0.0;      // last fill_level
    bool loaded = false;

    // Full pipeline: load the project, generate a layout (attempts=4), build
    // the IR, then fill with the live cache.
    bool load(const std::string& project, const std::string& assets, std::string& err);
    // Re-read the project file, rebuild the IR from the stored layout and
    // refill through the same cache.
    bool refill(std::string& err);
    // Regenerate the layout from the same project, rebuild the IR, refill.
    bool relayout(std::string& err);

  private:
    bool buildIrFromGenerate(std::string& err);
    bool runFill(std::string& err);
};

// Locate the dungeon_geometry_generator assets library (the dir holding codes.pgg): ./assets first,
// then walk up from the executable's directory, then from the project file's
// directory. Empty when not found.
std::string resolve_dungeon_geometry_generator_assets(const std::string& argv0, const std::string& projectPath);

// Default dir of the project Browse dialog: <root>/projects next to the
// located assets library. Empty when no assets root matched.
std::string resolve_dungeon_geometry_generator_projects(const std::string& argv0);
