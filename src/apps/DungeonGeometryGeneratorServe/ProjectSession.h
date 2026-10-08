#pragma once

// One DungeonGeometryGeneratorServe slot (docs/dungeon_geometry_generator/mcp_v1.md): a loaded dungeon_geometry_generator project keyed by its
// canonical path, the layout/IR/fill pipeline state and the warm F8 unit
// cache that survives the slot's refills. Work on a slot is serialized by mu
// (recursive, so the op helpers can nest the ensure-steps).

#include <cstdint>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "fill.h"
#include "layout.h"

class ProjectSession {
public:
    explicit ProjectSession(std::string canonicalPath) : m_canonical(std::move(canonicalPath)) {}

    std::recursive_mutex mu;

    const std::string& canonicalPath() const { return m_canonical; }
    nlohmann::json sessionEcho() const { return {{"file", m_canonical}}; }

    dungeon_geometry_generator::Project project;
    bool has_project = false;
    dungeon_geometry_generator::LayoutData layoutData;
    bool has_layout = false;
    dungeon_geometry_generator::IrV2 ir;
    bool has_ir = false;
    dungeon_geometry_generator::FillResult fill;
    bool has_fill = false;
    dungeon_geometry_generator::UnitCache cache;  // F8: warm across the slot's fills (content keys)
    std::int64_t projectMtimeNs = 0;  // last loaded project file mtime

    int seedUsed = 0, attemptUsed = 0;  // last layout
    double layoutMs = 0.0, fillMs = 0.0;

private:
    std::string m_canonical;
};

std::string canonicalServePath(const std::string& path);
std::int64_t fileMtimeNs(const std::string& path);
double wallNowSec();
