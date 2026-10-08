#pragma once

#include "edgar/generator/common/energy_data.hpp"
#include "edgar/geometry/overlap.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/rectangle_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <cstddef>
#include <cstdlib>
#include <vector>

namespace edgar::generator::grid2d::constraints {

/// C# `BasicConstraint`: overlap + door-distance for graph-neighbor pairs.
///
/// The `valid_on_cs` matrix must be precomputed by the caller using
/// `ConstraintsEvaluatorGrid2D::precompute_cs_validity()`.
struct BasicConstraintGrid2D {
    /// Legacy overload (overlap only, no graph info).
    static common::EnergyData evaluate_pair(std::size_t i, std::size_t j,
                                            const std::vector<geometry::PolygonGrid2D>& outlines,
                                            const std::vector<geometry::Vector2Int>& positions) {
        common::EnergyData out;
        if (geometry::polygons_overlap_area(outlines[i], positions[i], outlines[j], positions[j])) {
            out.overlap_penalty = 1.0;
        }
        return out;
    }

    /// Full overload with precomputed `valid_on_cs` matrix (C# parity).
    /// `valid_on_cs[i][j]` is true iff rooms i,j are graph neighbors with a valid door position.
    static common::EnergyData evaluate_pair(std::size_t i, std::size_t j,
                                            const std::vector<geometry::PolygonGrid2D>& outlines,
                                            const std::vector<geometry::Vector2Int>& positions,
                                            const std::vector<std::vector<bool>>& valid_on_cs) {
        common::EnergyData out;
        if (geometry::polygons_overlap_area(outlines[i], positions[i], outlines[j], positions[j])) {
            out.overlap_penalty = 1.0;
        } else if (!valid_on_cs.empty() && !valid_on_cs[i][j]) {
            const auto ri = outlines[i].bounding_rectangle() + positions[i];
            const auto rj = outlines[j].bounding_rectangle() + positions[j];
            const geometry::Vector2Int ci{(ri.a.x + ri.b.x) / 2, (ri.a.y + ri.b.y) / 2};
            const geometry::Vector2Int cj{(rj.a.x + rj.b.x) / 2, (rj.a.y + rj.b.y) / 2};
            out.move_distance_penalty = static_cast<double>(
                std::abs(ci.x - cj.x) + std::abs(ci.y - cj.y));
        }
        return out;
    }
};

} // namespace edgar::generator::grid2d::constraints
