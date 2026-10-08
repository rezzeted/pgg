#pragma once

#include "edgar/generator/grid2d/layout_grid2d.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace edgar::generator::grid2d {

/// Mirrors C# `SimulatedAnnealingEventType` (`Edgar.Legacy.Core.LayoutEvolvers.SimulatedAnnealing`).
enum class LayoutYieldEvent {
    LayoutGenerated = 0,
    RandomRestart = 1,
    OutOfIterations = 2,
    StageTwoFailure = 3,
};

/// When to emit intermediate layouts (C# `SimulatedAnnealingEvolver.Evolve` yield stream subset).
enum class LayoutStreamMode {
    /// Single final layout only (default); same as disabling the callback.
    Single = 0,
    /// Emit `LayoutYieldEvent::LayoutGenerated` for each successful chain completion (penalty <= 0), up to `max_layout_yields`.
    OnEachLayoutGenerated = 1,
    /// After each accepted SA perturb, run `TryCompleteChain` on a clone and emit like C# `SimulatedAnnealingEvolver` inner loop.
    OnEachSaTryCompleteChain = 2,
};

/// C# `SimulatedAnnealingEventArgs` subset: iteration bookkeeping around yields and stage-two failures.
struct LayoutOrchestrationStats {
    int iterations_total = 0;
    int iterations_since_last_event = 0;
    int layouts_generated = 0;
    /// Count of full layout restarts after a failed attempt (C# `numberOfFailures` usage in outer loops).
    int number_of_failures = 0;
    /// Incremented when `try_complete_chain` does not reach zero penalty (C# `stageTwoFailures`).
    int stage_two_failures = 0;
    /// Reserved for multi-chain C# pipelines; Grid2D chain backend uses 0.
    int chain_number = 0;
};

struct LayoutYieldInfo {
    LayoutYieldEvent event_type = LayoutYieldEvent::LayoutGenerated;
    int iterations_total = 0;
    int iterations_since_last_event = 0;
    int layouts_generated = 0;
    int chain_number = 0;
    double energy = 0.0;
};

template <typename TRoom>
struct ChainGenerateContext {
    LayoutStreamMode layout_stream = LayoutStreamMode::Single;
    /// Upper bound on `LayoutGenerated` callbacks (C# `count` in `Evolve`).
    int max_layout_yields = 10000;
    std::function<void(const LayoutYieldInfo&, const LayoutGrid2D<TRoom>&)> on_layout{};
    LayoutOrchestrationStats* stats_out = nullptr;

    /// C# `OnSimulatedAnnealingEvent`: receives `LayoutYieldInfo` without requiring layout stream mode.
    std::function<void(const LayoutYieldInfo&)> on_simulated_annealing_event{};
    /// C# `OnValid`: full valid layout (penalty resolved after TCC) — called once per successful outcome.
    std::function<void(const LayoutGrid2D<TRoom>&)> on_valid{};
    /// C# `OnPartialValid`: overlap-free snapshot during SA (semantic intermediate layout).
    std::function<void(const LayoutGrid2D<TRoom>&)> on_partial_valid{};
    /// C# `OnPerturbed`: after a Metropolis-accepted perturb step.
    std::function<void(const LayoutGrid2D<TRoom>&)> on_perturbed{};

    /// When set, cooperative cancel (mutually exclusive with early-stop limits in C#; same here).
    std::shared_ptr<std::atomic<bool>> cancellation_requested{};
    std::optional<int> early_stop_max_total_iterations{};
    std::optional<std::chrono::milliseconds> early_stop_max_elapsed{};
    std::chrono::steady_clock::time_point wall_start{};
    std::function<std::chrono::steady_clock::time_point()> now_fn = [] { return std::chrono::steady_clock::now(); };

    /// Primary iteration counter for early-stop (chain updates; SA inner steps use `chain_base_iterations` offset).
    int* iter_budget_sink = nullptr;

    bool poll_abort(int current_total_iterations) const {
        if (cancellation_requested && cancellation_requested->load(std::memory_order_acquire)) {
            return true;
        }
        if (early_stop_max_total_iterations.has_value() &&
            current_total_iterations >= early_stop_max_total_iterations.value()) {
            return true;
        }
        if (early_stop_max_elapsed.has_value()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_fn() - wall_start);
            if (elapsed >= early_stop_max_elapsed.value()) {
                return true;
            }
        }
        return false;
    }

    void publish_iterations(int n) const {
        if (iter_budget_sink) {
            *iter_budget_sink = n;
        }
    }
};

} // namespace edgar::generator::grid2d
