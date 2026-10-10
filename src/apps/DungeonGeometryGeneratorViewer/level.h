#pragma once

// DungeonGeometryGeneratorViewer data layer (F9): owns the project -> layout -> IR -> fill
// pipeline state so the UI reads plain structs. The UnitCache (F8) outlives
// individual fills; refill/generate reuse it and expose the reuse stats.
// Async hosts (main.cpp's GenTask) drive the stage/cancel/progress hooks:
// onStage fires at each stage start (kStage*), shouldCancel is polled between
// stages and between fill units (the F3 layout generator cancels through
// layoutGen->request_cancel()), onUnit reports fill progress.

#include <functional>
#include <memory>
#include <string>

#include "catalog.h"
#include "fill.h"
#include "ir.h"
#include "layout.h"
#include "project.h"

namespace dungeon_geometry_generator::layout {
class LayoutGenerator;
}

// Pipeline stages reported through Level::onStage (generate order; refill
// skips kStageLayout).
enum GenStage { kStageCatalog = 0, kStageLayout = 1, kStageIR = 2, kStageFill = 3 };

struct Level {
    dungeon_geometry_generator::Project project;
    dungeon_geometry_generator::layout::Catalog catalog;  // F2 catalog of the loaded layout tier
    dungeon_geometry_generator::LayoutData layoutData;
    dungeon_geometry_generator::IrV2 ir;
    dungeon_geometry_generator::FillResult fill;
    dungeon_geometry_generator::UnitCache unitCache;  // F8: position-independent unit outputs

    std::string projectPath;
    std::string dungeon_geometry_generatorAssets;  // resolved assets dir (FillOpts::dungeon_geometry_generator_assets)
    double catalogMs = 0.0;   // last F2 catalog build (generate/refill)
    double layoutMs = 0.0;    // last successful generate
    double irMs = 0.0;        // last IR build (generate/refill)
    double fillMs = 0.0;      // last fill_level
    bool loaded = false;
    bool generated = false;   // layout/IR/fill exist (false = parse-only state)

    // Async-host hooks (empty = synchronous callers: --smoke, tests).
    std::function<void(int stage)> onStage;       // kStage* at each stage start
    std::function<bool()> shouldCancel;           // polled between stages/units
    std::function<void(size_t unitsDone)> onUnit;  // after each fill unit

    // The F3 generator lives here so the UI thread can request_cancel() a
    // running generate (thread-safe, sticky until the next generate resets).
    std::unique_ptr<dungeon_geometry_generator::layout::LayoutGenerator> layoutGen;

    Level();
    ~Level();
    Level(Level&&) noexcept;
    Level& operator=(Level&&) noexcept;
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;

    // Parse-only open: read the project file and build the F2 catalog. The
    // layout/fill pipeline does NOT run — opening a project stays fast,
    // generation is explicit (generate).
    bool load(const std::string& project, const std::string& assets, std::string& err);
    // The slow stage: generate a layout (attempts=4), build the IR, then
    // fill with the live cache. Uses the in-memory project (unsaved editor
    // edits included).
    bool generate(std::string& err);
    // Re-read the project file, rebuild the IR from the stored layout and
    // refill through the same cache. Never generated yet: becomes generate.
    bool refill(std::string& err);

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
