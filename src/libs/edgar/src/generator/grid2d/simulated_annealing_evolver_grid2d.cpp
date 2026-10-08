#include "edgar/generator/grid2d/simulated_annealing_evolver_grid2d.hpp"

namespace edgar::generator::grid2d {

void SimulatedAnnealingEvolverGrid2D::evolve(std::vector<geometry::PolygonGrid2D>& outlines,
                                             std::vector<geometry::Vector2Int>& positions, std::mt19937& rng,
                                             int* iterations_out) const {
    if (positions.empty()) {
        if (iterations_out) {
            *iterations_out = 0;
        }
        return;
    }
    if (!config_.enable_random_walk_fallback || config_.max_perturbation_radius <= 0) {
        if (iterations_out) {
            *iterations_out = 0;
        }
        return;
    }

    int iterations = 0;
    int last_success_iteration = 0;

    auto energy = [&]() {
        return common::BasicEnergyUpdater::total_penalty(ConstraintsEvaluatorGrid2D::evaluate(outlines, positions));
    };
    double e = energy();

    constexpr double p0 = 0.2;
    constexpr double p1 = 0.01;
    double t0 = -1.0 / std::log(p0);
    const double t1 = -1.0 / std::log(p1);
    const int cycles = std::max(1, config_.cycles);
    const double ratio =
        (cycles > 1) ? std::pow(t1 / t0, 1.0 / static_cast<double>(cycles - 1)) : 0.9995;
    double delta_e_avg = 0.0;
    int accepted_solutions = 1;
    double t = t0;

    std::uniform_int_distribution<int> pick_room(0, static_cast<int>(positions.size()) - 1);
    std::uniform_int_distribution<int> pick_dx(-config_.max_perturbation_radius, config_.max_perturbation_radius);
    std::uniform_int_distribution<int> pick_dy(-config_.max_perturbation_radius, config_.max_perturbation_radius);
    std::uniform_real_distribution<double> uni01(0.0, 1.0);

    for (int i = 0; i < cycles; ++i) {
        if (iterations - last_success_iteration > config_.max_iterations_without_success) {
            break;
        }

        bool was_accepted = false;

        for (int j = 0; j < config_.trials_per_cycle; ++j) {
            ++iterations;
            // C# `MaxStageTwoFailures` also feeds outer layout restarts in `ChainBasedGeneratorGrid2D`; this evolver
            // does not use it (random-walk SA only).

            const int r = pick_room(rng);
            const geometry::Vector2Int old = positions[static_cast<std::size_t>(r)];
            const int dx = pick_dx(rng);
            const int dy = pick_dy(rng);
            positions[static_cast<std::size_t>(r)] = {old.x + dx, old.y + dy};

            const double new_e = energy();
            const double energy_delta = new_e - e;

            const double delta_abs = std::abs(energy_delta);
            bool accept = false;
            if (energy_delta > 0.0) {
                if (i == 0 && j == 0) {
                    delta_e_avg = delta_abs * 15.0;
                }
                const double p = std::exp(-delta_abs / (delta_e_avg * t));
                if (uni01(rng) < p) {
                    accept = true;
                }
            } else {
                accept = true;
            }

            if (accept) {
                ++accepted_solutions;
                e = new_e;
                delta_e_avg = (delta_e_avg * static_cast<double>(accepted_solutions - 1) + delta_abs) /
                              static_cast<double>(accepted_solutions);
                was_accepted = true;
                if (e <= 0.0) {
                    last_success_iteration = iterations;
                }
            } else {
                positions[static_cast<std::size_t>(r)] = old;
            }

            if (e <= 0.0) {
                if (iterations_out) {
                    *iterations_out = iterations;
                }
                return;
            }
        }

        if (!was_accepted) {
            // C# increments numberOfFailures (handled only in the chain+layout-controller pipeline).
        }
        t *= ratio;
    }

    if (iterations_out) {
        *iterations_out = iterations;
    }
}

} // namespace edgar::generator::grid2d
