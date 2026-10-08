#pragma once

namespace edgar::generator::common {

/// C# `Edgar.Legacy.Core.LayoutEvolvers.SimulatedAnnealing.SimulatedAnnealingConfiguration` (GraphBasedGenerator).
struct SimulatedAnnealingConfiguration {
    int cycles = 50;
    int trials_per_cycle = 100;
    int max_iterations_without_success = 100;
    int max_stage_two_failures = 10000;
    bool handle_trees_greedily = true;

    /// Fallback tuning only: max |dx|,|dy| for random-walk perturbation when CS sampling cannot provide candidates.
    int max_perturbation_radius = 6;

    /// Primary perturbation budget for configuration-space candidate search (`sample_maximum_intersection_position`).
    int max_cs_perturbation_checks = 160;

    /// If false, SA keeps old position when CS perturbation has no candidate (no random-walk fallback).
    bool enable_random_walk_fallback = true;

    static SimulatedAnnealingConfiguration csharp_default() {
        return SimulatedAnnealingConfiguration{};
    }
};

} // namespace edgar::generator::common
