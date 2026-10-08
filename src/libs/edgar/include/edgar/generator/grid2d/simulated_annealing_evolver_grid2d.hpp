#pragma once

#include "edgar/generator/common/basic_energy_updater.hpp"
#include "edgar/generator/common/energy_data.hpp"
#include "edgar/generator/common/simulated_annealing_configuration.hpp"
#include "edgar/generator/grid2d/constraints_evaluator_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace edgar::generator::grid2d {

/// Legacy random-walk SA evolver (dx/dy perturbations only).
/// Main Grid2D pipeline uses `LayoutControllerGrid2D` with configuration-space-first perturbation.
class SimulatedAnnealingEvolverGrid2D {
public:
    explicit SimulatedAnnealingEvolverGrid2D(common::SimulatedAnnealingConfiguration config = {})
        : config_(config) {}

    void evolve(std::vector<geometry::PolygonGrid2D>& outlines, std::vector<geometry::Vector2Int>& positions,
                std::mt19937& rng, int* iterations_out) const;

private:
    common::SimulatedAnnealingConfiguration config_;
};

} // namespace edgar::generator::grid2d
