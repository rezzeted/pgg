#pragma once

#include "edgar/chain_decompositions/chain_decomposition_configuration.hpp"
#include "edgar/generator/common/simulated_annealing_configuration.hpp"
#include "edgar/generator/common/sa_configuration_provider.hpp"
#include "edgar/generator/grid2d/layout_orchestration.hpp"

#include <chrono>
#include <functional>
#include <optional>

namespace edgar::generator::grid2d {

/// Which chain decomposition feeds `ChainBasedGeneratorGrid2D` (C# `GraphBasedGenerator` strategies).
enum class ChainDecompositionStrategy {
    /// Historical tests / stable behaviour (`BreadthFirstChainDecompositionOld`).
    breadth_first_old = 0,
    /// Planar-face BFS decomposition (`BreadthFirstChainDecomposition`).
    breadth_first_new = 1,
    /// Run inner decomposition on `get_stage_one_graph()`, then merge stage-two rooms (`TwoStageChainDecomposition`).
    two_stage = 2,
};

enum class GraphBasedGeneratorBackend {
    /// Fast strip packer + Clipper2 overlap (original C++ port).
    strip_packing = 0,
    /// Chain decomposition (`BreadthFirstChainDecompositionOld`) + simulated annealing (C#-style pipeline subset).
    chain_simulated_annealing = 1,
};

/// Mirrors a subset of C# `GraphBasedGeneratorConfiguration` plus C++ backend selection.
struct GraphBasedGeneratorConfiguration {
    GraphBasedGeneratorBackend backend = GraphBasedGeneratorBackend::chain_simulated_annealing;

    /// Used when `backend == chain_simulated_annealing`.
    /// C# `GraphBasedGeneratorGrid2D` uses `TwoStageChainDecomposition(BreadthFirstChainDecomposition)`.
    ChainDecompositionStrategy chain_decomposition = ChainDecompositionStrategy::two_stage;

    /// Settings for `breadth_first_new` / `two_stage` inner BFS (`BreadthFirstChainDecomposition`).
    chain_decompositions::ChainDecompositionConfiguration chain_decomposition_configuration{};

    /// Extra spacing multiplier when strip-packing rooms (implementation detail).
    int strip_gap_cells = 0;

    common::SimulatedAnnealingConfiguration simulated_annealing{};

    /// C# `DungeonGeneratorConfiguration.SimulatedAnnealingMaxBranching` — how many valid layout
    /// variants the chain tree search (GeneratorPlanner) keeps per chain.
    int simulated_annealing_max_branching = 5;

    std::optional<common::SAConfigurationProvider> sa_config_provider{};

    /// C# `GraphBasedGeneratorConfiguration.OptimizeCorridorConstraints`.
    bool optimize_corridor_constraints = true;

    /// C# `SimulatedAnnealingEvolver.Evolve` yield stream (`OnEachLayoutGenerated` emits intermediate layouts).
    LayoutStreamMode layout_stream_mode = LayoutStreamMode::Single;
    int max_layout_yields = 10000;

    /// C# early stopping: max work units (same counter as chain `iter_count` + SA inner steps via context).
    std::optional<int> early_stop_max_total_iterations{};
    /// Wall-time budget from start of `generate_layout` (uses `steady_clock_now`).
    std::optional<std::chrono::milliseconds> early_stop_max_elapsed{};
    /// Injectable clock for tests and time budget (default: `std::chrono::steady_clock::now`).
    std::function<std::chrono::steady_clock::time_point()> steady_clock_now =
        [] { return std::chrono::steady_clock::now(); };
};

} // namespace edgar::generator::grid2d
