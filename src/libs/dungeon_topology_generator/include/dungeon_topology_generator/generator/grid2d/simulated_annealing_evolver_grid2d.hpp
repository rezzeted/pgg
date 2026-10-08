#pragma once

#include "dungeon_topology_generator/generator/common/basic_energy_updater.hpp"
#include "dungeon_topology_generator/generator/common/energy_data.hpp"
#include "dungeon_topology_generator/generator/common/simulated_annealing_configuration.hpp"
#include "dungeon_topology_generator/generator/grid2d/constraints_evaluator_grid2d.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d {

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

} // namespace dungeon_topology_generator::generator::grid2d
