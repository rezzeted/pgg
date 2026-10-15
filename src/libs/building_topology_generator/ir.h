#pragma once

// Building IR (requirements §10): the versioned JSON contract between
// building_topology_generator (BTG) and building_geometry_generator (BGG).
// Format: "building-topology-generator-ir/0". All numbers are meters in the
// building frame (§5.1: +Y up, plan in XZ, main facade +Z, origin at the
// footprint center at ground floor level), quantized to 1e-4 on write.
// String fields are duplicated by int codes from codes.h.

#include <glm/glm.hpp>

namespace building_topology_generator {

inline constexpr const char* kIrFormat = "building-topology-generator-ir/0";
inline constexpr double kIrQuant = 1e-4;  // float quantization on write (N6)

struct IrParams {
    double cell = 0.5;
    double h_ground = 2.8;
    double h_upper = 2.6;
    double slab_t = 0.25;
    double t_exterior = 0.45;
    double t_bearing = 0.30;
    double t_partition = 0.12;
    double t_knee = 0.10;
    double player_height = 1.8;
    double player_radius = 0.3;
    double headroom_margin = 0.2;
    double glass_ratio = 0.125;   // glazing >= 1/8 of the floor area (§5.4)
    double h_knee = 1.2;          // knee wall line height (§5.6)
    double roof_t = 0.25;         // roof pie thickness along the slope normal
    double h_above_roof = 0.5;    // min chimney above the slope (§5.7)
    double valley_margin = 0.6;   // chimney distance to valleys/hips (§5.7)

    double h_walk() const { return player_height + headroom_margin; }
};

struct IrStorey {
    std::string id;       // "ground", "upper", "attic", ...
    int kind_code = 0;    // kStorey*
    double y = 0.0;       // floor level
    double h = 0.0;       // clear height
    double slab_t = 0.0;  // slab thickness above (0 for the topmost full storey under the attic floor... see §5.3)
};

struct IrRoofSpec {
    int kind_code = 0;                 // kRoof*
    std::vector<double> pitch_by_edge; // degrees per outline edge; 90 = gable end
    double overhang = 0.0;
    double eave_rise = 0.0;  // slope plane height over `outline` above the attic floor
    double eave_y = 0.0;     // absolute: attic floor y + eave_rise
    double roof_t = 0.0;     // roof pie thickness along the normal
};

struct IrVolume {
    std::string id;
    std::vector<glm::dvec2> outline;  // outer wall face polygon, meters, CCW from +Y (§5.2)
    int storey0 = 0;                  // index range into Ir::storeys, inclusive
    int storey1 = 0;
    IrRoofSpec roof;
};

// Derived roof data (from the straight skeleton; requirements §10 "roof").
struct IrRoofPlane {
    std::string volume;
    int edge = -1;          // source outline edge of the volume
    double pitch = 0.0;     // degrees
    glm::dvec3 o{0.0};      // a plane point: midpoint of the (offset) eave, top surface
    glm::dvec3 n{0.0, 1.0, 0.0};  // outward/up normal of the top surface
};
struct IrRoofEdge {
    glm::dvec3 a{0.0}, b{0.0};
};
struct IrRoofDerived {
    std::vector<IrRoofPlane> planes;
    std::vector<IrRoofEdge> ridges;
    std::vector<IrRoofEdge> valleys;
    std::vector<IrRoofEdge> hips;
};

struct IrHearthRef {
    std::string space;    // space id with the hearth
    int kind_code = 0;    // kHearth*
    std::string wall;     // wall id the hearth sits on
    double s = 0.0;       // position along that wall, meters from wall a
};

struct IrSpace {
    std::string id;
    std::string storey;
    int fn_code = 0;
    int zone_code = 0;
    std::vector<glm::ivec2> cells;      // plan grid cells (debug/topology)
    std::vector<glm::dvec2> outline;    // inner-face polygon, meters, CCW from +Y
    double area_net = 0.0;              // net area; attic: useful zone only (§5.6)
    bool habitable = false;
    int hearth_code = 0;                // kHearth* marker; details in chimneys[].hearths
};

struct IrOpening {
    std::string id;
    int kind_code = 0;  // kOpeningDoor / kOpeningWindow
    std::string type;   // terminal id, e.g. "door_plain", "win_flat"
    double s0 = 0.0;    // along the wall axis, meters from wall a
    double s1 = 0.0;
    double y0 = 0.0;    // vertical span above the storey floor
    double y1 = 0.0;
};

struct IrPlane {
    glm::dvec3 o{0.0};
    glm::dvec3 n{0.0, 1.0, 0.0};  // kept half-space: dot(p - o, n) <= 0
};

struct IrWall {
    std::string id;
    std::string storey;
    glm::dvec2 a{0.0}, b{0.0};  // wall axis (grid line), meters
    int class_code = 0;         // kWall*
    double thickness = 0.0;
    std::string owner;                       // one owner per shared wall (I9)
    std::string side_l, side_r;              // space id or "outside"
    double h = 0.0;                          // height above the storey floor
    std::vector<IrPlane> top_profile;        // gable: clip planes of the roof slopes
    std::vector<IrOpening> openings;
};

struct IrNodeFace {
    glm::dvec2 dir{0.0, 1.0};  // outward face normal (plan)
    double h = 0.0;            // open-face height
};
struct IrNode {
    std::string id;
    std::string storey;
    glm::dvec2 p{0.0};
    std::string owner;
    std::vector<IrNodeFace> faces;
};

struct IrSlabRect {
    double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
};
struct IrSlab {
    std::string storey_below;
    std::string storey_above;
    std::vector<IrSlabRect> rects;  // floor area minus stair VOID and chimney cells (§7.8)
    double y = 0.0;                 // slab bottom level
    double t = 0.0;                 // thickness
};

struct IrRailing {
    std::string id;
    std::string storey;
    glm::dvec2 a{0.0}, b{0.0};
    double h = 0.9;
};

struct IrStair {
    std::string id;
    int type_code = 0;      // kStair*
    std::string from, to;   // storey ids
    glm::dvec2 start{0.0};  // plan center of the first riser line
    glm::dvec2 dir{1.0, 0.0};     // march direction (axis unit)
    double width = 0.0;
    int risers = 0;               // total rises (§5.5)
    double rise = 0.0, going = 0.0;
    // L-stair (§9.1): two flights + landing
    int risers_low = 0;                       // risers of the lower flight
    glm::dvec2 dir2{0.0, 1.0};                // upper flight direction
    glm::dvec2 landing_min{0.0}, landing_max{0.0};
    // Slab opening (well) on the upper storey, meters; an L-stair has an
    // L-shaped opening, hence a rect list (§9.1).
    std::vector<IrSlabRect> well;
};

struct IrChimney {
    std::string id;
    std::vector<IrHearthRef> hearths;
    glm::dvec2 section{0.5, 0.5};  // meters (1x1 or 1x2 cells, §5.7)
    std::map<std::string, std::vector<glm::ivec2>> cells_by_storey;
    glm::dvec3 top{0.0};
    int roof_plane = -1;  // index into Ir::roof.planes (exit plane)
};

struct IrDormer {
    std::string id;
    int plane = -1;  // index into Ir::roof.planes
    glm::dvec2 at{0.0};
    double w = 0.0, h = 0.0;
};

struct IrAccessEdge {
    std::string a, b;
    std::string via;   // "door" | "stair" | "open"
    std::string ref;   // opening or stair id
};
struct IrAccess {
    std::vector<std::string> nodes;
    std::vector<IrAccessEdge> edges;
};

struct IrAnchor {
    int kind_code = 0;  // kAnchor*
    glm::dvec3 p{0.0};
    glm::dvec3 dir{0.0, 0.0, 1.0};
    std::string space;
};

struct Ir {
    uint64_t seed = 0;
    std::string program;
    IrParams params;
    std::vector<IrStorey> storeys;
    std::vector<IrVolume> volumes;
    IrRoofDerived roof;
    std::vector<IrSpace> spaces;
    std::vector<IrWall> walls;
    std::vector<IrNode> nodes;
    std::vector<IrSlab> slabs;
    std::vector<IrRailing> railings;
    std::vector<IrStair> stairs;
    std::vector<IrChimney> chimneys;
    std::vector<IrDormer> dormers;
    IrAccess access;
    std::vector<IrAnchor> anchors;
    std::map<std::string, double> report_cost;
    std::vector<std::string> report_moves;
};

// N7: rejects a foreign format with a hint; validates required fields and the
// string/code consistency of every coded field.
bool read_ir_json(const std::string& text, Ir& out, std::string& err);
bool load_ir_file(const std::string& path, Ir& out, std::string& err);

// Deterministic: ordered keys, entities sorted by id, floats quantized to 1e-4.
std::string write_ir_json(const Ir& ir);
bool save_ir_file(const Ir& ir, const std::string& path, std::string& err);

// Canonical entity order (by id) for N1/N6.
void sort_ir(Ir& ir);

}  // namespace building_topology_generator
