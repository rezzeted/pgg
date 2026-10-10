// DungeonGeometryGeneratorViewer (F9): 3D + top preview of a filled dungeon_geometry_generator level with an IR
// overlay (room ids, doors, wall/node owners, anchors), top-view picking with
// a provenance info panel, unit highlight/solo, refill/re-layout over the
// F8 unit cache, and a read-only Topo view of the level-synth topology
// (2D layout plan; the passage graph pane follows in the same tab).
// The side panel's Project tab shows the project tree (rooms, passages, room
// templates with their parametric generators, fill, slots); clicking a
// template or a generator opens its card in a View tab next to 3D/Top/Topo
// (contour canvas, doors, transforms, fill overrides, usage). Explicit
// templates and the generators edit level.project in place (dirty flag);
// Ctrl+S / Files > Save writes project.json (save_project) and applies via
// re-layout.
//   App frame: a main menu bar (Files: New / Open / Save / Close / Exit), the
// start screen while no project is loaded (big New/Open buttons, the
// cross-session recent list — recent.h — and the last load error) and the
// project UI (side panel + View tabs) once loaded. The OS window title shows
// "DGG Viewer - <project name>" (parent dir of a project.json, else the file
// stem) with a "*" while there are unsaved edits; New/Open/Close/Exit with
// unsaved edits go through an "unsaved changes" confirmation.
//   DungeonGeometryGeneratorViewer [project.json] [--smoke]
// No arguments: the start screen; a project is opened from it (New/Open or a
// recent entry), from Files > Open, or by dragging the project .json onto the
// window. The layout is always generated from the project's layout tier
// (attempts=4); frozen IR files are a DungeonGeometryGeneratorCli input, not the viewer's.
// --smoke runs the same data path without a window (ctest), prints one stats
// line and exits 0. Exit codes: 0 ok, 1 data error, 2 usage error.
// v1 limits: picking only in the top view, anchors only in the overlay.

#include "pch.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <pgg/eval.h>
#include <pgg/src/eval/topology_util.h>

#include "GeometryPreview.h"
#include "filedialog.h"
#include "level.h"
#include "panel.h"
#include "project_tree.h"
#include "recent.h"
#include "template_view.h"
#include "topo_view.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <util/sokol_imgui.h>

// Xlib.h (via sokol_app.h on Linux) defines None as a macro (0L); it collides
// with Selection::Kind::None. This TU never calls Xlib directly.
#if defined(None)
    #undef None
#endif

namespace {

constexpr float kPanelWidth = 380.0f;
// Points group baked onto a copy of the level mesh for the 3D unit highlight
// (the merged mesh carries no groups of its own).
constexpr const char* kUnitGroup = "unit";

bool g_gfxOk = false;
bool g_imguiOk = false;

std::string g_argv0;
std::string g_projectArg;
std::string g_loadError;

Level g_level;

FileDialog g_projectDlg;              // Files > Open (session dir memory)
FileDialog g_newDlg;                  // Files > New location picker (saveMode)
std::vector<std::string> g_recent;    // projects, newest first (persisted via recent.h)
std::string g_lastOpenDir;            // parent of the last opened project (relative-path fallback)
std::string g_projectsDir;            // first-open dir of the Open dialog (<root>/projects)
std::string g_windowTitle;            // last title pushed to the OS window

GeometryPreview g_preview3d;
GeometryPreview g_previewTop;
PreviewPaneRect g_rect3d, g_rectTop;
OverlayLayers g_layers;
int g_activeView = 0;  // 0 = 3D (default tab), 1 = Top, 2 = Topo, >=3 = template tab index + 3

// Open template/generator tabs of the View window (read-only cards of the
// project tree). Opens are queued while panels draw and applied at the top of
// drawPanes so the tab bar never mutates under its own iteration.
struct OpenTemplateTab {
    ProjectTreeSelection sel;
    TemplateViewState st;
};
std::vector<OpenTemplateTab> g_tmplTabs;
std::vector<ProjectTreeSelection> g_pendingTabs;
int g_raiseTab = -1;  // g_tmplTabs index to select once in drawPanes (-1 = none)

void openTemplateTab(const ProjectTreeSelection& sel) { g_pendingTabs.push_back(sel); }

void applyPendingTabs() {
    for (const ProjectTreeSelection& sel : g_pendingTabs) {
        size_t i = 0;
        while (i < g_tmplTabs.size() && !(g_tmplTabs[i].sel == sel)) ++i;
        if (i == g_tmplTabs.size()) g_tmplTabs.push_back({sel, TemplateViewState{}});
        g_raiseTab = static_cast<int>(i);
    }
    g_pendingTabs.clear();
}

void closeAllTemplateTabs() {
    g_tmplTabs.clear();
    g_pendingTabs.clear();
    g_raiseTab = -1;
}

// Topo tab state: the model is a pure projection of the project graph and
// the generated layout (rebuilt on load/refill/relayout), the pane state is
// the camera + layer toggles.
dungeon_geometry_generator::TopoModel g_topoModel;
TopoPlanState g_topoPlan;
TopoGraphState g_topoGraph;

void rebuildTopoModel(bool resetCamera) {
    g_topoModel = g_level.project.layout
                      ? dungeon_geometry_generator::build_topo(g_level.project.layout.value(), g_level.layoutData)
                      : dungeon_geometry_generator::TopoModel{};
    if (resetCamera) {
        g_topoPlan = TopoPlanState{};
        g_topoGraph = TopoGraphState{};
    }
}

Selection g_selection;
std::string g_selectedUnit;     // row selected in the units panel
std::string g_highlightedUnit;  // unit with the 3D highlight group ("" = none)
std::string g_soloUnit;         // unit shown solo in the 3D pane ("" = level view)

std::vector<std::pair<std::string, bool>> g_log;  // (line, isError)
bool g_projectDirty = false;  // in-memory project edits not yet saved (Ctrl+S / Files > Save)

void logLine(const std::string& line, bool isError = false) {
    g_log.push_back({line, isError});
    if (g_log.size() > 12) g_log.erase(g_log.begin());
    if (isError) {
        spdlog::error("DungeonGeometryGeneratorViewer: {}", line);
    } else {
        spdlog::info("DungeonGeometryGeneratorViewer: {}", line);
    }
}

const dungeon_geometry_generator::FillResult::UnitSpan* findUnit(const Level& level, const std::string& id) {
    if (id.empty()) return nullptr;
    for (const dungeon_geometry_generator::FillResult::UnitSpan& u : level.fill.units)
        if (u.id == id) return &u;
    return nullptr;
}

// Fill unit of an IR selection: rooms fill as "room:<id>", walls/nodes/doors
// keep their IR ids.
std::string unitIdForSelection(const Selection& sel) {
    switch (sel.kind) {
        case Selection::Kind::Room: return "room:" + sel.id;
        case Selection::Kind::Wall:
        case Selection::Kind::Node:
        case Selection::Kind::Door: return sel.id;
        default: return {};
    }
}

// Copy of the mesh with a points group flagging [begin, end) — the 3D
// highlight renders it brighter via highlightGroup = "points:unit".
pgg::GeoPtr meshWithUnitGroup(const pgg::Geo& mesh, size_t begin, size_t end) {
    auto col = std::make_shared<pgg::BoolColumn>(mesh.pointCount(), 0);
    for (size_t i = begin; i < end && i < mesh.pointCount(); ++i) (*col)[i] = 1;
    auto set = std::make_shared<pgg::GroupSet>(mesh.pointGroups ? *mesh.pointGroups
                                                                : pgg::GroupSet{});
    set->columns[kUnitGroup] = std::move(col);
    return pgg::withGroups(mesh, pgg::Domain::Points, std::move(set));
}

// Sub-mesh of one unit span: points outside [begin, end) are dropped; the
// §8.3 cascade kills their faces and gathers the attribute columns.
pgg::GeoPtr soloSubmesh(const pgg::Geo& mesh, size_t begin, size_t end) {
    std::vector<uint8_t> drop(mesh.pointCount(), 1);
    for (size_t i = begin; i < end && i < mesh.pointCount(); ++i) drop[i] = 0;
    return pgg::topo::deleteByMask(mesh, pgg::Domain::Points, drop);
}

// 3D pane geometry: the level mesh (optionally with the highlight group) or
// the solo sub-mesh. The top pane always shows the plain level mesh.
void rebuild3d(bool refit) {
    if (!g_level.loaded || !g_level.fill.mesh) return;
    PreviewBuildOptions opts;
    pgg::GeoPtr mesh = g_level.fill.mesh;
    std::string mode;
    if (const dungeon_geometry_generator::FillResult::UnitSpan* u = findUnit(g_level, g_soloUnit)) {
        mesh = soloSubmesh(*mesh, u->meshBegin, u->meshEnd);
        mode = "solo: " + g_soloUnit;
    } else if (const dungeon_geometry_generator::FillResult::UnitSpan* u = findUnit(g_level, g_highlightedUnit)) {
        mesh = meshWithUnitGroup(*mesh, u->meshBegin, u->meshEnd);
        opts.highlightGroup = std::string("points:") + kUnitGroup;
        mode = "highlight: " + g_highlightedUnit;
    }
    const PreviewGeometry geo = buildPreviewGeometry(pgg::Value(std::move(mesh)), opts);
    g_preview3d.setGeometry(geo, refit);
    g_preview3d.setSummary(geo.summary + (mode.empty() ? "" : "  [" + mode + "]"));
}

void rebuildPreviews(bool refit) {
    if (!g_level.loaded || !g_level.fill.mesh) return;
    rebuild3d(refit);
    const PreviewGeometry geo =
        buildPreviewGeometry(pgg::Value(g_level.fill.mesh), PreviewBuildOptions{});
    g_previewTop.setGeometry(geo, refit);
    g_previewTop.setSummary(geo.summary + "  [fill " +
                            std::to_string(static_cast<long long>(g_level.fillMs)) + " ms]");
}

std::string fillSummary(const char* what) {
    const dungeon_geometry_generator::FillStats& s = g_level.fill.stats;
    return std::string(what) + ": " + std::to_string(g_level.fill.units.size()) + " units (" +
           std::to_string(s.reused.size()) + " reused, " + std::to_string(s.reran.size()) +
           " reran), mesh " + std::to_string(g_level.fill.mesh ? g_level.fill.mesh->pointCount() : 0) +
           " pts, " + std::to_string(static_cast<long long>(g_level.fillMs)) + " ms";
}

// Selection/unit ids can move on a re-layout; reset conservatively (the ids
// survive a refill, but the spans are rebuilt anyway).
void resetViewState() {
    g_selection = Selection{};
    g_selectedUnit.clear();
    g_highlightedUnit.clear();
    g_soloUnit.clear();
}

// MRU of successfully opened projects, newest first, persisted across
// sessions (recent.h).
void rememberRecent(const std::string& proj) {
    g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), proj), g_recent.end());
    g_recent.insert(g_recent.begin(), proj);
    if (g_recent.size() > 8) g_recent.resize(8);
    saveRecentProjects(g_recent);
}

// Relative input paths resolve against the cwd first, then against the last
// opened project's directory.
std::string resolveInputPath(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty() || fs::path(path).is_absolute()) return path;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return path;
    if (!g_lastOpenDir.empty()) {
        const std::string cand = (fs::path(g_lastOpenDir) / path).string();
        if (fs::is_regular_file(cand, ec)) return cand;
    }
    return path;  // load_project will report it missing
}

void openLevel(const std::string& projRaw) {
    const std::string proj = resolveInputPath(projRaw);
    std::string err;
    if (!g_level.load(proj, resolve_dungeon_geometry_generator_assets(g_argv0, proj), err)) {
        g_loadError = err;
        logLine("load failed: " + err, true);
        return;
    }
    g_loadError.clear();
    resetViewState();
    closeAllTemplateTabs();
    g_projectDirty = false;
    rebuildTopoModel(true);  // fresh layout: fresh camera (auto-fit)
    rebuildPreviews(true);
    logLine(fillSummary("load"));
    // Recents survive the session: store the canonical absolute path (a
    // relative one would break when the next run has another cwd).
    std::error_code ec;
    const std::string abs = std::filesystem::weakly_canonical(proj, ec);
    rememberRecent(ec ? proj : abs);
    g_lastOpenDir = std::filesystem::path(ec ? proj : abs).parent_path().string();
}

// Back to the start screen: the level (and its unit cache) goes away, the
// previews are cleared, the recent list and dialog dirs stay as they were.
void closeLevel() {
    g_level = Level{};
    g_topoModel = dungeon_geometry_generator::TopoModel{};
    g_topoPlan = TopoPlanState{};
    g_topoGraph = TopoGraphState{};
    resetViewState();
    closeAllTemplateTabs();
    g_projectDirty = false;
    g_preview3d.clear();
    g_previewTop.clear();
    g_preview3d.setSummary({});
    g_previewTop.setSummary({});
    g_loadError.clear();
    logLine("level closed");
}

void doRefill() {
    std::string err;
    if (!g_level.refill(err)) {
        logLine("refill failed: " + err, true);
        return;
    }
    resetViewState();
    rebuildTopoModel(false);  // same layout: keep the camera
    rebuildPreviews(false);  // same layout: keep the camera
    logLine(fillSummary("refill"));
}

void doRelayout() {
    std::string err;
    if (!g_level.relayout(err)) {
        logLine("re-layout failed: " + err, true);
        return;
    }
    resetViewState();
    rebuildTopoModel(true);  // new layout: fresh camera (auto-fit)
    rebuildPreviews(true);  // new layout: refit
    logLine(fillSummary("re-layout"));
}

// Save the in-memory project (template cards edit it in place), then apply
// via the usual re-layout path — the reload re-validates what was written
// and rebuilds the catalog the cards read.
void doSaveApply() {
    std::string err;
    if (!dungeon_geometry_generator::save_project(g_level.projectPath, g_level.project, err)) {
        logLine("save failed: " + err, true);
        return;
    }
    g_projectDirty = false;
    logLine("saved " + g_level.projectPath);
    doRelayout();
}

// --- window title ------------------------------------------------------------

// Display name of a project file: the parent dir of a project.json ("demo"),
// else the file stem.
std::string projectDisplayName(const std::string& path) {
    namespace fs = std::filesystem;
    const fs::path p(path);
    if (p.filename() == "project.json" && p.has_parent_path()) {
        const std::string dir = p.parent_path().filename().string();
        if (!dir.empty()) return dir;
    }
    return p.stem().string();
}

// "DGG Viewer" on the start screen, "DGG Viewer - <name>[*]" with a project.
// Pushed to the OS window only on change.
void updateWindowTitle() {
    std::string title = "DGG Viewer";
    if (g_level.loaded)
        title += " - " + projectDisplayName(g_level.projectPath) + (g_projectDirty ? "*" : "");
    if (title != g_windowTitle) {
        g_windowTitle = title;
        sapp_set_window_title(title.c_str());
    }
}

// --- file actions (menu / start screen / shortcuts) ---------------------------

bool g_newOpen = false;  // request flag: the New project dialog opens on the next draw
char g_newPath[1024] = {};
std::string g_newError;

void openNewProjectDialog() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = !g_projectsDir.empty() ? fs::path(g_projectsDir) : fs::current_path(ec);
    const std::string def = (dir / "untitled" / "project.json").string();
    std::snprintf(g_newPath, sizeof(g_newPath), "%s", def.c_str());
    g_newError.clear();
    g_newOpen = true;
}

// Actions that lose the in-memory edits of a dirty project; those go through
// the unsaved-changes confirmation (g_pendingAction).
enum class PendingAction { None, New, Open, Close, Exit };
PendingAction g_pendingAction = PendingAction::None;

void runAction(PendingAction action) {
    switch (action) {
        case PendingAction::New: openNewProjectDialog(); break;
        case PendingAction::Open:
            fileDialogOpen(g_projectDlg, g_level.loaded ? g_level.projectPath : "", g_projectsDir);
            break;
        case PendingAction::Close: closeLevel(); break;
        case PendingAction::Exit: sapp_quit(); break;
        default: break;
    }
}

void requestAction(PendingAction action) {
    if (g_level.loaded && g_projectDirty) {
        g_pendingAction = action;  // confirmed by the unsaved-changes modal
    } else {
        runAction(action);
    }
}

// --- menu bar -------------------------------------------------------------------

float drawMainMenu() {
    float h = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        h = ImGui::GetWindowHeight();
        if (ImGui::BeginMenu("Files")) {
            if (ImGui::MenuItem("New...", "Ctrl+N")) requestAction(PendingAction::New);
            if (ImGui::MenuItem("Open...", "Ctrl+O")) requestAction(PendingAction::Open);
            if (ImGui::MenuItem("Save", "Ctrl+S", false, g_level.loaded && g_projectDirty))
                doSaveApply();
            ImGui::Separator();
            if (ImGui::MenuItem("Close", nullptr, false, g_level.loaded))
                requestAction(PendingAction::Close);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) requestAction(PendingAction::Exit);
            ImGui::EndMenu();
        }
        if (g_level.loaded) {
            const std::string right =
                projectDisplayName(g_level.projectPath) + (g_projectDirty ? "*" : "");
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x -
                                 ImGui::GetStyle().ItemSpacing.x);
            ImGui::TextDisabled("%s", right.c_str());
        }
        ImGui::EndMainMenuBar();
    }
    return h;
}

// --- start screen ---------------------------------------------------------------

// No project loaded: the app title, big New/Open buttons, the last load
// error and the cross-session recent list. Drag & drop works in this state
// too (handleDrop calls openLevel directly).
void drawStartScreen(float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##start", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float winW = ImGui::GetWindowWidth();
    // One centered column holds everything: the title, the buttons and the
    // recent list; texts align to the column's left edge.
    const float colW = std::min(winW - 80.0f, 560.0f);
    const float colX = std::max((winW - colW) * 0.5f, ImGui::GetStyle().WindowPadding.x);
    const auto centerColX = [colX, colW](float itemW) {  // centered within the column
        ImGui::SetCursorPosX(colX + std::max((colW - itemW) * 0.5f, 0.0f));
    };

    ImGui::SetCursorPosY(h * 0.22f);
    const char* title = "DGG Viewer";
    ImGui::SetWindowFontScale(2.0f);
    ImGui::SetCursorPosX(colX);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetCursorPosX(colX);
    ImGui::TextDisabled("Dungeon Geometry Generator Viewer");

    ImGui::Spacing();
    ImGui::Spacing();
    // The buttons together span the column width.
    const float btnGap = 24.0f;
    const ImVec2 btnSize((colW - btnGap) * 0.5f, 64.0f);
    ImGui::SetCursorPosX(colX);
    if (ImGui::Button("New", btnSize)) requestAction(PendingAction::New);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create a starter project.json (Ctrl+N)");
    ImGui::SameLine(0.0f, btnGap);
    if (ImGui::Button("Open", btnSize)) requestAction(PendingAction::Open);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open an existing project.json (Ctrl+O)");
    const char* dropHint = "or drop a project .json onto the window";
    centerColX(ImGui::CalcTextSize(dropHint).x);
    ImGui::TextDisabled("%s", dropHint);

    if (!g_loadError.empty()) {
        ImGui::Spacing();
        ImGui::SetCursorPosX(colX);
        ImGui::PushTextWrapPos(colX + colW);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
        ImGui::TextUnformatted(g_loadError.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    // Column-local SeparatorText: ImGui's own SeparatorText rules the whole
    // window width regardless of the cursor, so draw the rule by hand,
    // clipped to the column (line, bg over it, then the label on top).
    const char* recentLabel = "Recent Projects";
    ImGui::SetCursorPosX(colX);
    {
        const ImVec2 labelSize = ImGui::CalcTextSize(recentLabel);
        const ImVec2 textPos = ImGui::GetCursorScreenPos();
        const float lineY = textPos.y + labelSize.y * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(textPos.x, lineY), ImVec2(textPos.x + colW, lineY),
                    ImGui::GetColorU32(ImGuiCol_Separator));
        dl->AddRectFilled(textPos, ImVec2(textPos.x + labelSize.x, textPos.y + labelSize.y),
                          ImGui::GetColorU32(ImGuiCol_WindowBg));
        ImGui::TextUnformatted(recentLabel);
    }
    if (g_recent.empty()) {
        ImGui::SetCursorPosX(colX);
        ImGui::TextDisabled("(empty — opened projects appear here)");
    }
    int removeAt = -1;
    for (size_t i = 0; i < g_recent.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetCursorPosX(colX);
        if (ImGui::Selectable(g_recent[i].c_str(), false, 0, ImVec2(colW - 28.0f, 0.0f)))
            openLevel(g_recent[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = static_cast<int>(i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from the list");
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        g_recent.erase(g_recent.begin() + removeAt);
        saveRecentProjects(g_recent);
    }
    ImGui::End();
}

// --- modals -------------------------------------------------------------------

// Starter content of Files > New: a minimal valid v1 project (entry —
// corridor — hall, parametric rooms, the default slots) written to the
// chosen path and then opened via the usual openLevel path.
const char* kNewProjectJson = R"JSON({
  "format": "dungeon-geometry-generator-project/1",
  "seed": 1,
  "layout": {
    "corridors": {"width": 2, "length": [3, 4]},
    "rooms_rect": {"w": [4, 5], "h": [4, 5]},
    "door_length": 1,
    "door_corner_distance": 1,
    "min_room_distance": 1,
    "catalog_budget": 64,
    "rooms": [
      {"id": "entry", "role": "entry"},
      {"id": "hall", "role": "hall"},
      {"id": "c1", "role": "corridor"}
    ],
    "passages": [
      {"a": "entry", "b": "c1", "door": "open"},
      {"a": "c1", "b": "hall", "door": "open"}
    ]
  },
  "fill": {
    "cell": 2.0,
    "wall_t": 0.6,
    "min_passage": 1.2,
    "min_opening": 0.8,
    "room_h": 3.0,
    "door_h": 2.2,
    "frame": 0.15,
    "lamp_step": 4.0,
    "row_module": 0.25,
    "roles": {
      "*": {"h": 3.0, "style": "stone", "floor": "stone", "ceil": "plain"},
      "corridor": {"h": 2.6, "style": "brick", "floor": "brick", "ceil": "plain"}
    },
    "transitions": {"pattern": "butt", "width": 1.0, "place": "corner"},
    "side_rules": [
      {"match": {"adjacent_role": "corridor"}, "style": "brick"},
      {"match": {"side": "outer"}, "style": "stone"}
    ]
  },
  "slots": {
    "room_fill": "rooms/fill_v1.pgg",
    "wall_body": "walls/body_v1.pgg",
    "facing": "walls/facing_v1.pgg",
    "node": "walls/node_v1.pgg",
    "door": "doors/opening_v1.pgg",
    "decor:lamp": "decor/lamp_v1.pgg"
  },
  "asset_roots": ["assets"]
}
)JSON";

void createNewProject() {
    namespace fs = std::filesystem;
    const std::string path(g_newPath);
    if (path.empty() || path.find_first_not_of(" \t") == std::string::npos) {
        g_newError = "empty path";
        return;
    }
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty() && !fs::create_directories(parent, ec) && ec) {
        g_newError = "cannot create " + parent.string() + ": " + ec.message();
        return;
    }
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            g_newError = "cannot write " + path;
            return;
        }
        out << kNewProjectJson;
    }
    openLevel(path);
    if (g_level.loaded) {
        ImGui::CloseCurrentPopup();
    } else {
        g_newError = g_loadError;  // written but rejected — keep the modal open
    }
}

void drawNewProjectModal() {
    if (g_newOpen) {
        ImGui::OpenPopup("New project");
        g_newOpen = false;
    }
    if (!ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("project file:");
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText("##newpath", g_newPath, sizeof(g_newPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##new")) fileDialogOpen(g_newDlg, g_newPath, g_projectsDir);
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(g_newPath), ec))
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "exists — will be overwritten");
    if (!g_newError.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "%s", g_newError.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Create", ImVec2(90.0f, 0.0f))) createNewProject();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawUnsavedModal() {
    if (g_pendingAction == PendingAction::None) return;
    if (!ImGui::IsPopupOpen("Unsaved changes")) ImGui::OpenPopup("Unsaved changes");
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("%s has unsaved changes.", projectDisplayName(g_level.projectPath).c_str());
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(90.0f, 0.0f))) {
        doSaveApply();
        if (!g_projectDirty) {  // saved: run the pending action (on failure stay)
            const PendingAction action = g_pendingAction;
            g_pendingAction = PendingAction::None;
            ImGui::CloseCurrentPopup();
            runAction(action);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(90.0f, 0.0f))) {
        const PendingAction action = g_pendingAction;
        g_pendingAction = PendingAction::None;
        ImGui::CloseCurrentPopup();
        runAction(action);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
        g_pendingAction = PendingAction::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Modals live at frame scope (they must draw in every layout state): the two
// file dialogs (Open / New location), the New project dialog and the
// unsaved-changes confirmation.
void drawModals() {
    std::string picked;
    if (fileDialogDraw(g_projectDlg, "Open project", picked)) openLevel(picked);
    if (fileDialogDraw(g_newDlg, "New project location", picked))
        std::snprintf(g_newPath, sizeof(g_newPath), "%s", picked.c_str());
    drawNewProjectModal();
    drawUnsavedModal();
}

// --- camera focus ------------------------------------------------------------

bool unitSpanBBox(const Level& level, const dungeon_geometry_generator::FillResult::UnitSpan& u, glm::vec3& mn,
                  glm::vec3& mx) {
    if (!level.fill.mesh || !level.fill.mesh->positions) return false;
    const std::vector<glm::vec3>& P = *level.fill.mesh->positions;
    if (u.meshBegin >= u.meshEnd || u.meshBegin >= P.size()) return false;
    const size_t end = std::min(u.meshEnd, P.size());
    mn = glm::vec3(FLT_MAX);
    mx = glm::vec3(-FLT_MAX);
    for (size_t i = u.meshBegin; i < end; ++i) {
        mn = glm::min(mn, P[i]);
        mx = glm::max(mx, P[i]);
    }
    return true;
}

bool selectionBBox(const Level& level, const Selection& sel, glm::vec3& mn, glm::vec3& mx) {
    const double cell = level.project.fill.cell;
    mn = glm::vec3(FLT_MAX);
    mx = glm::vec3(-FLT_MAX);
    auto extend = [&](double x, double y, double z) {
        const glm::vec3 p(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    };
    switch (sel.kind) {
        case Selection::Kind::Room:
            for (const dungeon_geometry_generator::IrRoom& r : level.ir.rooms)
                if (r.id == sel.id) {
                    for (const dungeon_geometry_generator::GridPt& g : r.grid) {
                        extend(g.first * cell, 0.0, g.second * cell);
                        extend(g.first * cell, r.h, g.second * cell);
                    }
                    return true;
                }
            return false;
        case Selection::Kind::Wall:
            for (const dungeon_geometry_generator::IrWall& w : level.ir.walls)
                if (w.id == sel.id) {
                    const double h = std::max(w.h_left, w.h_right);
                    extend(w.g0.first * cell, 0.0, w.g0.second * cell);
                    extend(w.g1.first * cell, h, w.g1.second * cell);
                    return true;
                }
            return false;
        case Selection::Kind::Door:
            for (const dungeon_geometry_generator::IrDoor& d : level.ir.doors)
                if (d.id == sel.id) {
                    extend(d.from.first, 0.0, d.from.second);
                    extend(d.to.first, d.h, d.to.second);
                    return true;
                }
            return false;
        case Selection::Kind::Node:
            for (const dungeon_geometry_generator::IrNode& n : level.ir.nodes)
                if (n.id == sel.id) {
                    const double r = std::max(n.thick, 0.5) * 0.5;
                    extend(n.at.first * cell - r, 0.0, n.at.second * cell - r);
                    extend(n.at.first * cell + r, n.h_pillar, n.at.second * cell + r);
                    return true;
                }
            return false;
        default:
            return false;
    }
}

void focusBBox(const glm::vec3& mn, const glm::vec3& mx) {
    const glm::vec3 center = (mn + mx) * 0.5f;
    const float radius = std::max(glm::length(mx - mn) * 0.5f, 0.5f);
    g_preview3d.setTarget(center, radius);
    g_previewTop.setTarget(center, radius);
}

// --- panels ------------------------------------------------------------------

void drawSidePanel(float y, float h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, h), ImGuiCond_Always);
    ImGui::Begin("##side", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse);

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Stats")) {
        ImGui::TextWrapped("project: %s", g_level.projectPath.c_str());
        const dungeon_geometry_generator::FillStats& s = g_level.fill.stats;
        ImGui::Text("rooms %zu  walls %zu  nodes %zu", s.rooms, s.bodies, s.nodes);
        ImGui::Text("facings %zu  doors %zu  lamps %zu", s.facings, s.doors, s.lamps);
        ImGui::Text("fill %.0f ms (%zu units: %zu reused, %zu reran)", g_level.fillMs,
                    g_level.fill.units.size(), s.reused.size(), s.reran.size());
        ImGui::Text("layout %.0f ms", g_level.layoutMs);
        ImGui::TextDisabled("unit cache: %zu outputs", g_level.unitCache.size());

        if (ImGui::Button("Refill (reload project)")) doRefill();
        ImGui::SameLine();
        if (ImGui::Button("Re-layout")) doRelayout();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Generate a fresh layout (same seed, attempts=4), rebuild the IR, refill");
        if (g_projectDirty) ImGui::TextDisabled("modified — Files > Save (Ctrl+S)");
    }

    if (ImGui::BeginTabBar("##side_tabs")) {
        if (ImGui::BeginTabItem("Project")) {
            const ProjectTreeActions ta = drawProjectTree(g_level);
            if (ta.openTab) openTemplateTab(ta.tab);
            if (ta.selectRoom) g_selection = Selection{Selection::Kind::Room, ta.roomId};
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inspect")) {
            if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
                const InfoActions ia = drawInfoPanel(g_level, g_selection);
                const std::string selUnit = unitIdForSelection(g_selection);
                if (ia.focus) {
                    glm::vec3 mn, mx;
                    if (selectionBBox(g_level, g_selection, mn, mx)) focusBBox(mn, mx);
                }
                if ((ia.highlightUnit || ia.soloUnit) && !findUnit(g_level, selUnit)) {
                    if (!selUnit.empty()) logLine("no fill unit for " + selUnit, true);
                } else if (ia.highlightUnit) {
                    g_selectedUnit = selUnit;
                    g_highlightedUnit = g_highlightedUnit == selUnit ? "" : selUnit;
                    rebuild3d(false);
                } else if (ia.soloUnit) {
                    g_selectedUnit = selUnit;
                    g_soloUnit = selUnit;
                    rebuild3d(true);  // frame the isolated piece
                }
            }

            if (ImGui::CollapsingHeader("Units", ImGuiTreeNodeFlags_DefaultOpen)) {
                const UnitActions ua =
                    drawUnitsPanel(g_level, g_selectedUnit, g_highlightedUnit, !g_soloUnit.empty());
                if (ua.focus) {
                    glm::vec3 mn, mx;
                    if (const dungeon_geometry_generator::FillResult::UnitSpan* u = findUnit(g_level, g_selectedUnit);
                        u && unitSpanBBox(g_level, *u, mn, mx))
                        focusBBox(mn, mx);
                }
                if (ua.toggleHighlight) {
                    g_highlightedUnit = g_highlightedUnit == g_selectedUnit ? "" : g_selectedUnit;
                    rebuild3d(false);
                }
                if (ua.toggleSolo) {
                    const bool entering = g_soloUnit.empty();
                    g_soloUnit = entering ? g_selectedUnit : "";
                    rebuild3d(entering);  // entering solo: frame the piece; back: keep camera
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##log", ImVec2(0.0f, 110.0f), true);
        for (const auto& [text, isError] : g_log) {
            if (isError) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextWrapped("%s", text.c_str());
            }
        }
        if (g_log.empty()) ImGui::TextDisabled("(empty)");
        ImGui::EndChild();
    }
    ImGui::End();
}

// Right region: one window holding the views as tabs (3D is the default)
// instead of splitting the area; only the active tab's pane is drawn.
void drawPanes(float x, float y, float w, float h) {
    applyPendingTabs();
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##view", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse);
    int closeTab = -1;
    if (ImGui::BeginTabBar("##views")) {
        if (ImGui::BeginTabItem("3D")) {
            g_activeView = 0;
            drawPreviewPane(g_preview3d, g_rect3d, "no level loaded");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Top")) {
            g_activeView = 1;
            const PreviewPaneResult topRes =
                drawPreviewPane(g_previewTop, g_rectTop, "no level loaded", &g_layers);
            if (g_level.loaded) {
                if (topRes.clicked)
                    g_selection = pickAtSelection(g_level, g_previewTop, g_rectTop, topRes.clickPos);
                drawIrOverlay(ImGui::GetWindowDrawList(), g_previewTop, g_rectTop, g_level, g_layers,
                              g_selection);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Topo")) {
            g_activeView = 2;
            if (g_level.loaded) {
                // Two side-by-side panes over the same TopoModel: the 2D plan
                // and the passage graph (independent cameras, half the tab
                // width each).
                const ImVec2 tabAvail = ImGui::GetContentRegionAvail();
                const float paneW = std::max((tabAvail.x - 12.0f) * 0.5f, 160.0f);
                const float paneH = std::max(tabAvail.y, 160.0f);

                TopoPlanResult planRes;
                if (ImGui::BeginChild("##topo_plan", ImVec2(paneW, paneH)))
                    planRes = drawTopoPlan(g_topoModel, g_selection, g_topoPlan);
                ImGui::EndChild();
                if (planRes.focus) {
                    for (const auto& n : g_topoModel.nodes) {
                        if (n.id == g_selection.id && n.hasLayout) {
                            fitTopoPlanCam(g_topoPlan, n.minx, n.miny, n.maxx, n.maxy);
                            break;
                        }
                    }
                }
                ImGui::SameLine();
                TopoGraphResult graphRes;
                if (ImGui::BeginChild("##topo_graph", ImVec2(paneW, paneH)))
                    graphRes = drawTopoGraph(g_topoModel, g_selection, g_topoGraph);
                ImGui::EndChild();
                if (graphRes.focus) {
                    for (const auto& n : g_topoModel.nodes) {
                        if (n.id == g_selection.id && n.hasLayout) {
                            fitTopoGraphCam(g_topoGraph, n.minx, n.miny, n.maxx, n.maxy);
                            break;
                        }
                    }
                }
            } else {
                ImGui::TextDisabled("no level loaded");
            }
            ImGui::EndTabItem();
        }
        // Dynamic template/generator tabs opened from the project tree.
        for (size_t i = 0; i < g_tmplTabs.size(); ++i) {
            OpenTemplateTab& tab = g_tmplTabs[i];
            bool open = true;
            const ImGuiTabItemFlags flags =
                static_cast<int>(i) == g_raiseTab ? ImGuiTabItemFlags_SetSelected : 0;
            const std::string label = templateTabLabel(tab.sel);
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                g_activeView = 3 + static_cast<int>(i);
                if (g_level.loaded) {
                    const TemplateViewActions ta =
                        drawTemplateView(g_level, tab.sel, tab.st, g_projectDirty);
                    if (ta.markDirty) g_projectDirty = true;
                    if (ta.openTab) openTemplateTab(ta.tab);
                    if (ta.selectRoom) g_selection = Selection{Selection::Kind::Room, ta.roomId};
                    if (ta.saveApply) doSaveApply();
                } else {
                    ImGui::TextDisabled("no level loaded");
                }
                ImGui::EndTabItem();
            }
            if (!open) closeTab = static_cast<int>(i);
        }
        ImGui::EndTabBar();
    }
    if (closeTab >= 0) g_tmplTabs.erase(g_tmplTabs.begin() + closeTab);
    g_raiseTab = -1;  // consumed
    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("DungeonGeometryGeneratorViewer: init()");

    sg_desc gfx = {};
    gfx.environment = sglue_environment();
    gfx.logger.func = slog_func;
    sg_setup(&gfx);
    g_gfxOk = sg_isvalid();
    if (!g_gfxOk) {
        spdlog::error("DungeonGeometryGeneratorViewer: sg_setup FAILED");
        return;
    }

    simgui_desc_t imguiDesc = {};
    imguiDesc.logger.func = slog_func;
    simgui_setup(&imguiDesc);
    g_imguiOk = true;

    g_preview3d.init();
    g_previewTop.init();
    g_previewTop.setProjection(PreviewProjection::OrthoTop);

    g_projectsDir = resolve_dungeon_geometry_generator_projects(g_argv0);
    g_recent = loadRecentProjects();
    g_newDlg.saveMode = true;  // the New location picker accepts a not-yet-existing path

    if (!g_projectArg.empty()) openLevel(g_projectArg);
}

void frame() {
    if (!g_gfxOk) return;

    if (g_imguiOk) {
        const float dpi = std::max(sapp_dpi_scale(), 0.01f);
        simgui_frame_desc_t fd = {};
        fd.width = sapp_width();
        fd.height = sapp_height();
        fd.delta_time = static_cast<float>(sapp_frame_duration());
        fd.dpi_scale = dpi;
        simgui_new_frame(&fd);

        // ImGui works in logical points; the framebuffer size needs the dpi.
        const int w = static_cast<int>(std::lround(sapp_widthf() / dpi));
        const int h = static_cast<int>(std::lround(sapp_heightf() / dpi));

        const float menuH = drawMainMenu();
        const float contentH = static_cast<float>(h) - menuH;
        if (g_level.loaded) {
            drawSidePanel(menuH, contentH);
            drawPanes(kPanelWidth, menuH, static_cast<float>(w) - kPanelWidth, contentH);
        } else {
            drawStartScreen(menuH, static_cast<float>(w), contentH);
        }
        drawModals();
        updateWindowTitle();

        // Global editor shortcuts.
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N)) requestAction(PendingAction::New);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) requestAction(PendingAction::Open);
        if (g_projectDirty && g_level.loaded &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))
            doSaveApply();

        // Offscreen pass of the active view only: outside (before) the
        // swapchain pass that draws the ImGui image referencing its target.
        // The Topo tab draws into ImGui draw lists only — no preview pass.
        if (g_activeView == 0) {
            g_preview3d.render();
        } else if (g_activeView == 1) {
            g_previewTop.render();
        }
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.10f, 0.11f, 0.13f, 1.0f};
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    if (g_imguiOk) simgui_render();
    sg_end_pass();
    sg_commit();
}

void cleanup() {
    if (g_imguiOk) {
        g_preview3d.shutdown();
        g_previewTop.shutdown();
        simgui_shutdown();
        g_imguiOk = false;
    }
    if (sg_isvalid()) sg_shutdown();
}

// A dropped file is an IR when its text carries the dungeon-geometry-generator-ir format key
// (cheap sniff of the head; projects never mention it).
bool sniffIsIr(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string head(65536, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    return head.find("\"dungeon-geometry-generator-ir/") != std::string::npos;
}

// Drag & drop (enable_dragndrop, max 1): a project .json opens directly,
// replacing the current level. A frozen IR is not a viewer input (the layout
// is always generated from the project) — say so instead of failing later.
void handleDrop() {
    if (sapp_get_num_dropped_files() < 1) return;
    const std::string path = sapp_get_dropped_file_path(0);
    if (sniffIsIr(path)) {
        g_loadError = "frozen IR is not opened here — drop the project .json";
        logLine(g_loadError, true);
        return;
    }
    openLevel(path);
}

void event(const sapp_event* ev) {
    if (g_imguiOk) simgui_handle_event(ev);
    if (ev->type == SAPP_EVENTTYPE_FILES_DROPPED) handleDrop();
}

void printUsage() {
    std::fprintf(stderr,
                 "usage: DungeonGeometryGeneratorViewer [project.json] [--smoke]\n"
                 "  no arguments: the start screen (New / Open / recent projects); a project\n"
                 "  .json can also be dropped onto the window (the layout is always generated)\n"
                 "  --smoke      no window: generate the level, print one stats line, exit 0\n"
                 "               (needs a project; usage errors exit 2)\n"
                 "exit codes: 0 ok, 1 data error, 2 usage error\n");
}

// --smoke: the whole data path (layout -> IR -> fill with a unit cache)
// without sokol. stdout carries exactly one stats line.
int runSmoke() {
    spdlog::set_level(spdlog::level::err);
    Level level;
    std::string err;
    if (!level.load(g_projectArg, resolve_dungeon_geometry_generator_assets(g_argv0, g_projectArg), err)) {
        std::fprintf(stderr, "DungeonGeometryGeneratorViewer --smoke: %s\n", err.c_str());
        return 1;
    }
    const dungeon_geometry_generator::FillStats& s = level.fill.stats;
    std::printf("smoke ok: rooms %zu, walls %zu, facings %zu, nodes %zu, doors %zu, lamps %zu | "
                "mesh %zu pts, anchors %zu pts | units %zu (%zu reused, %zu reran) | "
                "layout %.0f ms, fill %.0f ms\n",
                s.rooms, s.bodies, s.facings, s.nodes, s.doors, s.lamps,
                level.fill.mesh ? level.fill.mesh->pointCount() : 0,
                level.fill.anchors ? level.fill.anchors->pointCount() : 0,
                level.fill.units.size(), s.reused.size(), s.reran.size(), level.layoutMs,
                level.fillMs);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    g_argv0 = argc > 0 ? argv[0] : "";
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--smoke") {
            smoke = true;
        } else if (arg.rfind("--", 0) != 0) {
            g_projectArg = arg;
        } else {
            std::fprintf(stderr, "DungeonGeometryGeneratorViewer: unknown option '%s'\n", arg.c_str());
            printUsage();
            return 2;
        }
    }
    if (smoke && g_projectArg.empty()) {
        std::fprintf(stderr, "DungeonGeometryGeneratorViewer: --smoke needs a project\n");
        printUsage();
        return 2;
    }
    if (smoke) return runSmoke();
    // GUI mode: the project is optional — no arguments opens the start screen.

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1440;
    desc.height = 900;
    // Swapchain stays 1x: the MSAA lives on the preview offscreen passes.
    desc.sample_count = 1;
    desc.window_title = "DGG Viewer";
    desc.high_dpi = true;
    desc.enable_dragndrop = true;
    desc.max_dropped_files = 1;  // one project .json per drop
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;
    sapp_run(&desc);
    return 0;
}
