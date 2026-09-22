#pragma once

// Weighted straight skeleton of a simple polygon (architecture plan A4, spec
// §8 L1): every outline edge sweeps inward at plan speed w = 1/tan(pitch)
// (w = 0 -> a vertical gable wall that never moves). Event queue per Felkel &
// Obdrzalek: edge events (a wavefront edge collapses), split events (a reflex
// vertex runs into an opposite edge). Deterministic: events fire ordered by
// (t, seq) with a monotonic sequence counter, and events sharing (t, pos)
// within eps collapse into a single skeleton node (pyramid peaks, symmetry).

#include <vector>

#include <glm/glm.hpp>

namespace pgg {

struct SkeletonInput {
    // Outline vertices (x, z), CCW seen from +Y (signed area in (x, z) < 0 —
    // the same convention as lib/arch/plan). speed[i] is the plan speed of the
    // edge outline[i] -> outline[i+1]: 1/tan(pitch); 0 = vertical (gable).
    std::vector<glm::vec2> outline;
    std::vector<float> speed;
};

struct SkeletonNode {
    glm::vec2 p;     // plan position
    float t = 0.0f;  // event time == height above y0 (plan speed convention)
};

struct SkeletonArc {
    int32_t a = -1;
    int32_t b = -1;
    int32_t leftEdge = -1;   // source outline edge id on the left of a -> b
    int32_t rightEdge = -1;  // on the right
};

struct SkeletonFace {
    int32_t edge = -1;            // source outline edge id
    std::vector<int32_t> nodes;   // polygon: [outline v0 (t=0), outline v1 (t=0), skeleton chain...]
};

struct StraightSkeleton {
    std::vector<SkeletonNode> nodes;
    std::vector<SkeletonArc> arcs;
    std::vector<SkeletonFace> faces;
    // Cut contours at tMax (rise_max > 0): one ring of node indices per
    // wavefront ring alive at the cut. Empty when the skeleton ran to the end.
    std::vector<std::vector<int32_t>> topRings;
};

// Builds the skeleton. Requires: simple polygon (no self-intersections),
// >= 3 vertices, CCW-from-above winding, no duplicate consecutive vertices,
// no exactly-degenerate (180 deg) vertices, speeds >= 0 with at least one > 0.
// tMax > 0 cuts the sweep at that time: events past the cut are dropped and
// the live wavefront rings are sealed into cut nodes/arcs (mansard decks).
StraightSkeleton buildStraightSkeleton(const SkeletonInput& in, float tMax = 0.0f);

// Offsets every edge line outward by d and re-intersects neighbours (roof
// overhang): the polygon structure (vertex count, winding) is preserved.
std::vector<glm::vec2> offsetOutline(const std::vector<glm::vec2>& outline, float d);

}  // namespace pgg
