#pragma once

// Per-run diagnostics. init() opens a fresh log file for this process in
// <repo>/logs/DungeonGeometryGeneratorViewer/ (the repo root is the parent of the
// located dungeon_geometry_generator assets dir; the system temp dir is the fallback),
// adds it to spdlog's default logger (the UI Log dock lines land in the file
// too) and installs a crash handler (POSIX: SIGSEGV/SIGABRT/SIGILL/SIGFPE/
// SIGBUS append a signal header + backtrace to the file before re-raising).
// Old runs are pruned so the dir stays bounded.
//   runlogSnapshotProject writes the in-memory project (unsaved editor edits
// included) next to the run log before a generate/refill: the repro case when
// the pipeline dies mid-run — the snapshot plus the assets dir regenerate the
// exact level (determinism, N1).

#include <string>

namespace dungeon_geometry_generator {
struct Project;
}

// Create the log dir, open this run's file, attach it to spdlog, install the
// crash handler. A second call does nothing. All failures are silent (the
// stdout-only logging remains).
void runlogInit(const std::string& argv0);

// This run's log file and its dir ("" when init failed).
const std::string& runlogPath();
const std::string& runlogDir();

// <run stamp>.gen<NN>.project.json next to the run log. Logs the path.
void runlogSnapshotProject(const dungeon_geometry_generator::Project& project, int genIndex);
