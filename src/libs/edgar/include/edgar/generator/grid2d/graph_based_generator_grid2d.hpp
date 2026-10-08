#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <random>

#include "edgar/generator/grid2d/graph_based_generator_configuration.hpp"
#include "edgar/generator/grid2d/layout_grid2d.hpp"
#include "edgar/generator/grid2d/layout_orchestration.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"

#include <functional>

namespace edgar::generator::grid2d {

/// Graph-based layout generator (2D grid). Default backend follows the C# pipeline at a high level
/// (chain decomposition + simulated annealing). `GraphBasedGeneratorBackend::strip_packing` keeps the
/// earlier deterministic strip packer (see docs/port_vs_original_gap.md).
template <typename TRoom>
class GraphBasedGeneratorGrid2D {
public:
    GraphBasedGeneratorGrid2D(const LevelDescriptionGrid2D<TRoom>& level_description,
                              GraphBasedGeneratorConfiguration configuration = {});

    LayoutGrid2D<TRoom> generate_layout();

    void inject_random_generator(std::mt19937 rng);

    void set_layout_yield_callback(
        std::function<void(const LayoutYieldInfo&, const LayoutGrid2D<TRoom>&)> callback) {
        layout_yield_callback_ = std::move(callback);
    }

    /// Request cooperative cancellation (C# `CancellationToken`). Mutually exclusive with early-stop fields
    /// in configuration — throws `std::logic_error` if both are used.
    void request_cancel();

    /// Clears cancellation flag (for repeated runs / tests). No-op when early-stop limits are configured.
    void reset_cancellation();

    void set_on_valid(std::function<void(const LayoutGrid2D<TRoom>&)> callback) { on_valid_ = std::move(callback); }
    void set_on_partial_valid(std::function<void(const LayoutGrid2D<TRoom>&)> callback) {
        on_partial_valid_ = std::move(callback);
    }
    void set_on_perturbed(std::function<void(const LayoutGrid2D<TRoom>&)> callback) {
        on_perturbed_ = std::move(callback);
    }
    void set_on_simulated_annealing_event(std::function<void(const LayoutYieldInfo&)> callback) {
        on_simulated_annealing_event_ = std::move(callback);
    }

    double time_total_ms() const { return time_total_ms_; }
    int iterations_count() const { return iterations_count_; }
    const LayoutOrchestrationStats& orchestration_stats() const { return orchestration_stats_; }

private:
    bool early_stop_configured() const {
        return configuration_.early_stop_max_total_iterations.has_value() ||
               configuration_.early_stop_max_elapsed.has_value();
    }

    LevelDescriptionGrid2D<TRoom> level_;
    GraphBasedGeneratorConfiguration configuration_{};
    std::optional<std::mt19937> rng_;
    double time_total_ms_{};
    int iterations_count_{};
    std::function<void(const LayoutYieldInfo&, const LayoutGrid2D<TRoom>&)> layout_yield_callback_{};
    std::function<void(const LayoutYieldInfo&)> on_simulated_annealing_event_{};
    std::function<void(const LayoutGrid2D<TRoom>&)> on_valid_{};
    std::function<void(const LayoutGrid2D<TRoom>&)> on_partial_valid_{};
    std::function<void(const LayoutGrid2D<TRoom>&)> on_perturbed_{};
    LayoutOrchestrationStats orchestration_stats_{};
    std::shared_ptr<std::atomic<bool>> cancellation_flag_{std::make_shared<std::atomic<bool>>(false)};
};

} // namespace edgar::generator::grid2d

#include "edgar/generator/grid2d/graph_based_generator_grid2d.inl"
