#pragma once

// Locating the dungeon_geometry_generator asset library (the dir carrying codes.pgg) for hosts that
// run the pipeline (DungeonGeometryGeneratorCli, DungeonGeometryGeneratorServe). Search order, same as DungeonGeometryGeneratorViewer:
// ./assets from the cwd, then up from the executable, then up from the
// project dir. Empty string when nothing matched — the caller decides the
// error (DungeonGeometryGeneratorCli reports D100 with a --assets hint).

#include <string>

namespace dungeon_geometry_generator {

std::string find_dungeon_geometry_generator_assets(const std::string& argv0, const std::string& projectPath);

}  // namespace dungeon_geometry_generator
