#pragma once

// Cross-session "recent projects" list of the start screen and the Files
// menu: one path per line, newest first, in the platform config dir
// (%APPDATA%\DGGViewer, ~/Library/Application Support/DGGViewer,
// ${XDG_CONFIG_HOME:-~/.config}/dgg-viewer) -> recent_projects.txt.
// All errors are silent: a missing/unwritable config dir just means no
// recents.

#include <string>
#include <vector>

std::vector<std::string> loadRecentProjects();
void saveRecentProjects(const std::vector<std::string>& recent);
