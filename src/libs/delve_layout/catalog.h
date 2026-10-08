#pragma once

// Delve F2 catalog (docs/delve/catalog_v1.md): project v1 declarations -> dungeon_topology_generator
// templates + per-room descriptions. Deterministic order: parametric
// corridors, parametric rects, explicit templates (declaration order).

#include <map>
#include <string>
#include <vector>

#include "dungeon_topology_generator/generator/grid2d/room_description_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/room_template_grid2d.hpp"

#include "project.h"

namespace delve::layout {

struct CatalogEntry {
    std::string name;
    std::vector<std::string> roles;
    dungeon_topology_generator::generator::grid2d::RoomTemplateGrid2D dungeon_topology_generator;
    FillOverride fill;
    bool parametric = false;
};

struct CatalogStats {
    int templates = 0;
    int instances = 0;  // templates x transforms after symmetry merging
    int corridors = 0;
    int rects = 0;
    int explicit_count = 0;
};

struct Catalog {
    std::vector<CatalogEntry> entries;
    CatalogStats stats;
};

// False + err when the project has no layout tier, an explicit name collides
// with a parametric one, or an dungeon_topology_generator constructor rejects a declaration.
bool build_catalog(const Project& project, Catalog& out, std::string& err);

// Per-graph-room descriptions (corridor flag + template copies). Empty sets
// are impossible after R-G3; one is reported as an internal error.
bool build_room_descriptions(
    const Project& project, const Catalog& catalog,
    std::map<std::string, dungeon_topology_generator::generator::grid2d::RoomDescriptionGrid2D>& out, std::string& err);

}  // namespace delve::layout
