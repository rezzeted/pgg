#pragma once

// Loading and saving of geo<points> payloads for host param binding
// (Delve requirements §9.2): artists iterate on real IR dumps without Delve
// by binding `--param pts=@rooms.points.json` in PggTool, PggServe and
// PggViewer. The format is neutral JSON, versioned, text-diffable:
//
//   {
//     "format": "pgg-points/1",
//     "positions": [[x, y, z], ...],      // required, may be empty
//     "normals": [[x, y, z], ...],        // optional, same count as positions
//     "attrs": {                          // optional, points domain
//       "room": {"type": "int", "values": [...]},
//       "Cd":   {"type": "vec3", "values": [[r, g, b], ...]}
//     },
//     "groups": {"sel": [1, 0, ...]}      // optional, points domain, 0/1 or bool
//   }
//
// Attribute types are the PGG scalar names: bool, int, f32, vec2, vec3,
// vec4, string. Every column must match the point count. Unknown top-level
// keys are ignored (forward compatibility); a missing or mismatched
// "format" is an error. All loaded columns carry AttrTypeInfo::None (plain
// data); use set(..., typeinfo = ...) after loading when a role is needed.
// A leading '@' on attribute/group names is stripped.

#include <string>

#include "geometry.h"

namespace pgg {

inline constexpr const char* kPointsFileFormat = "pgg-points/1";

// Loads a pgg-points/1 file into a geo<points> value. false + `err` when
// the file cannot be read or fails validation (err may be null).
bool loadPointsGeo(const std::string& path, GeoPtr& out, std::string* err = nullptr);

// Saves a geo<points> value in the pgg-points/1 format (attribute/group
// names sorted for stable diffs). Only kind Points is supported —
// mesh/instances are an error. false + `err` on IO failure (err may be null).
bool savePointsGeo(const std::string& path, const Geo& geo, std::string* err = nullptr);

}  // namespace pgg
