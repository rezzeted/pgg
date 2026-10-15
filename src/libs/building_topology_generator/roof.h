#pragma once

// Roof planes for the topology side (requirements §7.2, §11.4): BTG computes
// the roof from the volume outline with the same geom2d straight skeleton the
// PGG builtin uses, so the planes the topology reasons about (attic mask,
// chimney exits, dormers, windows under annex roofs) are exactly the planes
// the geometry will build. The parity is pinned by building_roof_parity_test.

#include <glm/glm.hpp>

#include "ir.h"
#include "straight_skeleton.h"

namespace building_topology_generator {

struct RoofModel {
    std::string volume_id;
    geom2d::StraightSkeleton sk;
    std::vector<glm::dvec2> outline;        // volume outline (source, outer wall face)
    std::vector<glm::dvec2> sweep_outline;  // offset outward by overhang: the sweep base
    std::vector<double> pitch_deg;          // per outline edge
    double y0 = 0.0;                        // skeleton base height: eave_y - overhang * tan(pitch)
    double eave_y = 0.0;
    double overhang = 0.0;
    double roof_t = 0.0;

    std::vector<IrRoofPlane> planes;  // per sloped (non-gable) skeleton face
    std::vector<IrRoofEdge> ridges;
    std::vector<IrRoofEdge> valleys;
    std::vector<IrRoofEdge> hips;
};

// Requirements §7.2: the kernel sweeps from the overhang-offset outline, so
// the base is lowered by overhang * tan(pitch) to make the plane pass through
// `outline` at eave_y. v1: all non-gable edges must share one pitch.
bool build_roof_model(const IrVolume& vol, RoofModel& out, std::string& err);

// Top roof surface height above the plan point; false when (x, z) is outside
// every sloped face (e.g. beyond the overhang or under a gable-only edge).
bool roof_y_at(const RoofModel& m, double x, double z, double& y);

// Underside of the roof pie (requirements §5.6): roof_y - roof_t / cos(pitch).
bool ceiling_y_at(const RoofModel& m, double x, double z, double& y);

// Chimney height rule (requirements §5.7, SP 7.13130 practice): d = plan
// distance from the stack center to the nearest ridge, ridge_y its height,
// exit_y = roof_y at the exit point.
double chimney_top_required(double d, double ridge_y, double exit_y, double h_above_roof);

// Plan distance from p to the nearest ridge segment; false when no ridges.
bool nearest_ridge(const RoofModel& m, double x, double z, double& dist, double& ridge_y);

}  // namespace building_topology_generator
