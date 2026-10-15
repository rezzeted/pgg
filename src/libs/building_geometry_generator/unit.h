#pragma once

// Internal: per-unit placement math shared by fill_building (runs the assets)
// and emit_scene_pgg (prints the same calls as a .pgg scene). One struct per
// unit kind carries everything the slot needs plus the world transform.

#include "building_geometry.h"

namespace building_geometry_generator {

// geo<points> under construction: positions + typed point attr columns.
struct PointsBuilder {
    std::vector<glm::vec3> positions;
    std::map<std::string, std::vector<float>> f32_attrs;
    std::map<std::string, std::pair<std::vector<glm::vec3>, pgg::AttrTypeInfo>> vec3_attrs;

    bool empty() const { return positions.empty(); }
    pgg::GeoPtr build() const;
    // Scene emission: emits `<prefix> = set_position(mesh_line...)` plus one
    // chained set(...) binding per attr into `out`; returns the final binding
    // name. Deterministic; needs <functional> via pch.
    std::string emit_to(const std::string& prefix, std::ostringstream& out) const;
};

struct Placement {
    double yaw_deg = 0.0;  // around +Y
    glm::dvec3 at{0.0};    // translate after rotation
};

// Plan helpers. Plan vectors are glm::dvec2 (x, z); the building frame is
// +Y up (requirements §5.1).
glm::dvec2 plan_dir(const glm::dvec2& a, const glm::dvec2& b);           // normalized b - a
glm::dvec2 plan_left(const glm::dvec2& d);                               // (-d.z, d.x)
double yaw_for_axis_x(const glm::dvec2& d);  // local +X -> d, degrees
double yaw_for_axis_z(const glm::dvec2& d);  // local +Z -> d, degrees

struct WallUnit {
    Placement pl;      // local frame: axis +X from `a`, base y = 0, z symmetric
    double len = 0.0, thick = 0.0, h = 0.0;
    int class_code = 0;
    PointsBuilder cuts;        // pairs (s, 0, 0) with @y0/@y1 on both points
    PointsBuilder top_planes;  // rows with @o/@n, local frame, IR half-space
};
WallUnit wall_unit(const building_topology_generator::Ir& ir,
                   const building_topology_generator::IrWall& w);

struct NodeUnit {
    Placement pl;
    glm::vec3 size{0.45f, 2.8f, 0.45f};
};
NodeUnit node_unit(const building_topology_generator::Ir& ir,
                   const building_topology_generator::IrNode& n);

struct SlabUnit {
    std::string id;
    PointsBuilder rects;  // pairs (min, max), y = slab bottom
    double t = 0.0;
};
SlabUnit slab_unit(const building_topology_generator::IrSlab& s);

struct StairUnit {
    Placement pl;  // march 1 from `start` along `dir`, base at the lower floor
    int risers = 0, risers_low = 0, turn = 1;
    double rise = 0.0, going = 0.0, w = 0.0;
};
StairUnit stair_unit(const building_topology_generator::Ir& ir,
                     const building_topology_generator::IrStair& s);

struct RailingUnit {
    Placement pl;  // from `a` along +Z
    double len = 0.0, h = 0.9;
};
RailingUnit railing_unit(const building_topology_generator::Ir& ir,
                         const building_topology_generator::IrRailing& r);

struct OpeningUnit {
    std::string id;
    std::string type;  // terminal id (door_plain, win_flat)
    Placement pl;      // bottom center at the opening, face into side_l/outside
    double w = 0.0, h = 0.0;
};
OpeningUnit opening_unit(const building_topology_generator::Ir& ir,
                         const building_topology_generator::IrWall& w,
                         const building_topology_generator::IrOpening& o);

struct ChimneyUnit {
    Placement pl;  // stack base center on the ground
    double w = 0.0, d = 0.0, h = 0.0;
};
ChimneyUnit chimney_unit(const building_topology_generator::IrChimney& c);

struct HearthUnit {
    std::string id;
    Placement pl;  // bottom center at the wall, face into the hearth space
    double w = 1.2;
    int kind_code = 0;
};
// False when the hearth wall id is unknown (caller reports a diagnostic).
bool hearth_unit(const building_topology_generator::Ir& ir,
                 const building_topology_generator::IrHearthRef& h, HearthUnit& out);

struct RoofUnit {
    std::string volume;
    PointsBuilder outline;  // vertices CCW from +Y with @pitch
    double pitch = 45.0;    // fallback when a vertex carries no @pitch
    double y0 = 0.0;        // skeleton base (eave_y - overhang * tan(pitch), §7.2)
    double overhang = 0.0;
    PointsBuilder holes;  // pairs (min, max) in plan
    double hole_margin = 0.06;
    // ceilings/attic only:
    double roof_t = 0.0;
    PointsBuilder inner;  // inner attic contour (outline - exterior thickness)
    double inner_hole_margin = 0.03;
};
RoofUnit roof_unit(const building_topology_generator::Ir& ir,
                   const building_topology_generator::IrVolume& v,
                   std::vector<std::string>& diagnostics);

// Storey lookup (nullptr when unknown).
const building_topology_generator::IrStorey* find_storey(
    const building_topology_generator::Ir& ir, const std::string& id);

}  // namespace building_geometry_generator
