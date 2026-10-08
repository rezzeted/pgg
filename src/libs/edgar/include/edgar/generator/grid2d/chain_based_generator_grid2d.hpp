#pragma once

#include "edgar/chain_decompositions/breadth_first_chain_decomposition.hpp"
#include "edgar/chain_decompositions/breadth_first_chain_decomposition_old.hpp"
#include "edgar/chain_decompositions/two_stage_chain_decomposition.hpp"
#include "edgar/generator/common/sa_configuration_provider.hpp"
#include "edgar/generator/common/simulated_annealing_configuration.hpp"
#include "edgar/generator/grid2d/graph_based_generator_configuration.hpp"
#include "edgar/generator/grid2d/configuration_spaces_grid2d.hpp"
#include "edgar/generator/grid2d/detail/room_index_map.hpp"
#include "edgar/generator/grid2d/grid2d_layout_state.hpp"
#include "edgar/generator/grid2d/layout_grid2d.hpp"
#include "edgar/generator/grid2d/layout_orchestration.hpp"
#include "edgar/generator/common/basic_energy_updater.hpp"
#include "edgar/generator/grid2d/constraints_evaluator_grid2d.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/generator/grid2d/layout_controller_grid2d.hpp"
#include "edgar/generator/grid2d/room_shapes_handler_grid2d.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <limits>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace edgar::generator::grid2d {

template <typename TRoom>
class ChainBasedGeneratorGrid2D {
public:
    struct Result {
        LayoutGrid2D<TRoom> layout;
        int iterations = 0;
    };

    static Result generate(const LevelDescriptionGrid2D<TRoom>& level, common::SimulatedAnnealingConfiguration sa_config,
                           std::mt19937& rng,
                           ChainDecompositionStrategy chain_strategy = ChainDecompositionStrategy::breadth_first_old,
                           chain_decompositions::ChainDecompositionConfiguration chain_cfg = {},
                           const ChainGenerateContext<TRoom>* ctx = nullptr,
                           const common::SAConfigurationProvider* sa_provider = nullptr,
                           int max_chain_branching = 5) {
        Grid2DLayoutState<TRoom> state(level);
        const detail::RoomIndexMap<TRoom>& rmap = state.rmap;
        const auto& ig = state.ig;

        std::vector<chain_decompositions::Chain<int>> chains;
        switch (chain_strategy) {
        case ChainDecompositionStrategy::breadth_first_old: {
            chain_decompositions::BreadthFirstChainDecompositionOld decomposer;
            chains = decomposer.get_chains(ig);
            break;
        }
        case ChainDecompositionStrategy::breadth_first_new: {
            chain_decompositions::BreadthFirstChainDecomposition decomposer(std::move(chain_cfg));
            chains = decomposer.get_chains(ig);
            break;
        }
        case ChainDecompositionStrategy::two_stage: {
            chain_decompositions::BreadthFirstChainDecomposition inner(std::move(chain_cfg));
            chain_decompositions::TwoStageChainDecomposition<TRoom> two_stage(level, rmap, inner);
            chains = two_stage.get_chains(ig);
            break;
        }
        default:
            throw std::invalid_argument("ChainBasedGeneratorGrid2D: unknown chain decomposition strategy");
        }

        const int n = static_cast<int>(rmap.index_to_room.size());
        std::vector<int> order;
        order.reserve(static_cast<std::size_t>(n));
        for (const auto& ch : chains) {
            for (int node : ch.nodes) {
                order.push_back(node);
            }
        }
        if (static_cast<int>(order.size()) != n) {
            throw std::runtime_error("ChainBasedGeneratorGrid2D: chain decomposition does not cover all rooms");
        }

        const bool is_tree_graph = edgar::graphs::is_tree(ig);
        const bool use_greedy_tree = sa_config.handle_trees_greedily && is_tree_graph;

        std::vector<bool> is_corridor_flags(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            is_corridor_flags[static_cast<std::size_t>(i)] =
                level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(i)]).is_corridor();
        }
        const RoomShapesHandlerGrid2D<TRoom> room_shapes_handler(level, rmap);

        auto build_doors_tab = [&](const std::vector<geometry::PolygonGrid2D>& ol,
                                    const std::vector<std::optional<RoomTemplateGrid2D>>& tpls) {
            std::vector<std::vector<DoorLineGrid2D>> dt(static_cast<std::size_t>(n));
            for (int j = 0; j < n; ++j) {
                if (tpls[static_cast<std::size_t>(j)].has_value()) {
                    dt[static_cast<std::size_t>(j)] =
                        tpls[static_cast<std::size_t>(j)]->doors().get_doors(ol[static_cast<std::size_t>(j)]);
                }
            }
            return dt;
        };
        auto penalty_total = [&](const std::vector<geometry::PolygonGrid2D>& ol,
                                 const std::vector<geometry::Vector2Int>& pos) {
            const auto dt = build_doors_tab(ol, state.templates);
            const auto vcs = ConstraintsEvaluatorGrid2D::precompute_cs_validity(ol, pos, dt, ig);
            return common::BasicEnergyUpdater::total_penalty(
                ConstraintsEvaluatorGrid2D::evaluate(ol, pos, vcs, level.minimum_room_distance,
                                                     &is_corridor_flags, level.optimize_corridor_constraints,
                                                     &ig));
        };

        const int max_layout_restarts =
            std::max(1, std::min(sa_config.max_stage_two_failures, 256));
        int iter_count = 0;
        int yields_emitted = 0;
        const int max_yields = ctx ? ctx->max_layout_yields : 0;

        auto sync_stats_iterations = [&]() {
            if (ctx && ctx->stats_out) {
                ctx->stats_out->iterations_total = iter_count;
            }
        };

        auto emit = [&](LayoutYieldEvent ev, Grid2DLayoutState<TRoom>& st, double pen) {
            if (!ctx) {
                return;
            }
            const bool outer_stream = ctx->layout_stream == LayoutStreamMode::OnEachLayoutGenerated;
            const bool sa_stream = ctx->layout_stream == LayoutStreamMode::OnEachSaTryCompleteChain;

            LayoutYieldInfo info;
            info.event_type = ev;
            info.iterations_total = iter_count;
            info.energy = pen;
            if (ctx->stats_out) {
                info.iterations_since_last_event = ctx->stats_out->iterations_since_last_event;
                info.layouts_generated = ctx->stats_out->layouts_generated;
                info.chain_number = ctx->stats_out->chain_number;
            }
            sync_stats_iterations();
            if (ctx->on_simulated_annealing_event) {
                ctx->on_simulated_annealing_event(info);
            }

            if (!ctx->on_layout) {
                return;
            }
            if (!outer_stream && !sa_stream) {
                return;
            }
            if (ev == LayoutYieldEvent::LayoutGenerated && !outer_stream) {
                return;
            }
            if (ev == LayoutYieldEvent::LayoutGenerated) {
                if (max_yields > 0 && yields_emitted >= max_yields) {
                    return;
                }
                ++yields_emitted;
            }
            ctx->on_layout(info, st.to_layout_grid());
            if (ctx->stats_out && ev == LayoutYieldEvent::LayoutGenerated) {
                ctx->stats_out->layouts_generated++;
                ctx->stats_out->iterations_since_last_event = 0;
            }
        };

        bool success = false;
        double last_penalty = 0.0;

        auto safe_to_layout = [&]() -> LayoutGrid2D<TRoom> {
            if (n <= 0 || static_cast<int>(state.outlines.size()) != n) {
                return {};
            }
            for (int i = 0; i < n; ++i) {
                if (!state.templates[static_cast<std::size_t>(i)].has_value()) {
                    return {};
                }
            }
            return state.to_layout_grid();
        };

        for (int restart = 0; restart < max_layout_restarts; ++restart) {
            if (ctx && ctx->poll_abort(iter_count)) {
                sync_stats_iterations();
                if (ctx->iter_budget_sink) {
                    ctx->publish_iterations(iter_count);
                }
                return Result{safe_to_layout(), iter_count};
            }
            if (restart > 0) {
                if (ctx && ctx->stats_out) {
                    ctx->stats_out->number_of_failures++;
                }
                emit(LayoutYieldEvent::RandomRestart, state, last_penalty);
            }

            state.resize_room_slots(n);
            auto& outlines = state.outlines;
            auto& positions = state.positions;
            auto& transforms = state.transforms;
            auto& templates = state.templates;
            std::vector<bool> placed(static_cast<std::size_t>(n), false);
            // Initial placement can fail on dense graphs; treat it as a failed restart attempt
            // instead of throwing across generate_layout (C# just counts the failure).
            bool initial_placement_failed = false;

            if (use_greedy_tree) {
                std::vector<int> pending(order.begin(), order.end());
                // Defer nodes whose neighbors are not placed yet (two-stage chain ordering).
                // `any_placed` must be re-evaluated per node: a stale per-pass value disables
                // every deferral on the first pass, scattering rooms at random spots instead
                // of anchoring them to already placed neighbours.
                while (!pending.empty() && !initial_placement_failed) {
                    bool pass_progress = false;
                    for (auto it = pending.begin(); it != pending.end();) {
                        bool any_placed = false;
                        for (const bool p : placed) {
                            any_placed |= p;
                        }
                        const int ri = *it;
                        bool has_placed_neighbor = false;
                        bool has_neighbors = false;
                        for (int nb : ig.neighbours(ri)) {
                            has_neighbors = true;
                            if (placed[static_cast<std::size_t>(nb)]) {
                                has_placed_neighbor = true;
                                break;
                            }
                        }
                        // Defer only when something is already placed to anchor on
                        if (any_placed && has_neighbors && !has_placed_neighbor) {
                            ++it;
                            continue;
                        }
                        if (!LayoutControllerGrid2D::add_node_greedily(level, rmap, ig, outlines, positions, templates,
                                                                        transforms, placed, ri, rng, &room_shapes_handler)) {
                            initial_placement_failed = true;
                            break;
                        }
                        it = pending.erase(it);
                        pass_progress = true;
                    }
                    if (!pass_progress && !pending.empty()) {
                        initial_placement_failed = true;
                    }
                }
            } else {
                std::uniform_int_distribution<int> jitter(-32, 32);
                auto pick_template = [&](int room_index) {
                    return room_shapes_handler.select_for_room(room_index, rng, &templates, &transforms);
                };

                {
                    const int r0 = order[0];
                    auto pick = pick_template(r0);
                    templates[static_cast<std::size_t>(r0)] = std::move(pick.room_template);
                    outlines[static_cast<std::size_t>(r0)] = std::move(pick.outline);
                    transforms[static_cast<std::size_t>(r0)] = pick.transformation;
                    positions[static_cast<std::size_t>(r0)] = {0, 0};
                    placed[static_cast<std::size_t>(r0)] = true;
                }

                std::vector<int> pending(order.begin() + 1, order.end());
                // Two-stage decomposition can order a node before its (stage-two) neighbors;
                // defer such nodes until some neighbor is placed instead of failing outright.
                while (!pending.empty() && !initial_placement_failed) {
                    bool pass_progress = false;
                    for (auto it = pending.begin(); it != pending.end();) {
                        const int ri = *it;
                        int pj = -1;
                        for (int nb : ig.neighbours(ri)) {
                            if (placed[static_cast<std::size_t>(nb)]) {
                                pj = nb;
                                break;
                            }
                        }
                        if (pj < 0) {
                            ++it; // no placed neighbor yet — defer to a later pass
                            continue;
                        }
                        auto pick = pick_template(ri);
                        templates[static_cast<std::size_t>(ri)] = std::move(pick.room_template);
                        outlines[static_cast<std::size_t>(ri)] = std::move(pick.outline);
                        transforms[static_cast<std::size_t>(ri)] = pick.transformation;

                        bool ok = false;
                        std::vector<std::vector<DoorLineGrid2D>> doors_tab(static_cast<std::size_t>(n));
                        for (int j = 0; j < n; ++j) {
                            if (!templates[static_cast<std::size_t>(j)].has_value()) continue;
                            if (j == ri || placed[static_cast<std::size_t>(j)]) {
                                doors_tab[static_cast<std::size_t>(j)] =
                                    templates[static_cast<std::size_t>(j)]->doors().get_doors(
                                        outlines[static_cast<std::size_t>(j)]);
                            }
                        }
                        for (int attempt = 0; attempt < 8000; ++attempt) {
                            ++iter_count;
                            if (ctx && ctx->iter_budget_sink) {
                                ctx->publish_iterations(iter_count);
                            }
                            if (ctx && ctx->poll_abort(iter_count)) {
                                sync_stats_iterations();
                                if (ctx && ctx->iter_budget_sink) {
                                    ctx->publish_iterations(iter_count);
                                }
                                return Result{safe_to_layout(), iter_count};
                            }
                            if (ctx && ctx->stats_out) {
                                ctx->stats_out->iterations_since_last_event++;
                            }
                            const auto greedy_pos = LayoutControllerGrid2D::greedy_position_from_configuration_spaces(
                                ri, level, rmap, ig, outlines[static_cast<std::size_t>(ri)],
                                doors_tab[static_cast<std::size_t>(ri)], placed, outlines, positions, doors_tab,
                                rng);
                            if (greedy_pos.has_value()) {
                                positions[static_cast<std::size_t>(ri)] = *greedy_pos;
                                placed[static_cast<std::size_t>(ri)] = true;
                                ok = true;
                                break;
                            }

                            const int dx = jitter(rng);
                            const int dy = jitter(rng);
                            const geometry::Vector2Int pos{positions[static_cast<std::size_t>(pj)].x + dx,
                                                           positions[static_cast<std::size_t>(pj)].y + dy};
                            bool bad = false;
                            for (int j = 0; j < n; ++j) {
                                if (!placed[static_cast<std::size_t>(j)]) {
                                    continue;
                                }
                                if (!ConfigurationSpacesGrid2D::compatible_non_overlapping(
                                        outlines[static_cast<std::size_t>(j)], positions[static_cast<std::size_t>(j)],
                                        outlines[static_cast<std::size_t>(ri)], pos)) {
                                    bad = true;
                                    break;
                                }
                            }
                            if (!bad) {
                                positions[static_cast<std::size_t>(ri)] = pos;
                                placed[static_cast<std::size_t>(ri)] = true;
                                ok = true;
                                break;
                            }
                        }
                        if (!ok) {
                            initial_placement_failed = true;
                            break;
                        }
                        it = pending.erase(it);
                        pass_progress = true;
                    }
                    if (!pass_progress && !pending.empty()) {
                        initial_placement_failed = true; // deferred nodes make no progress
                    }
                }
            }

            if (initial_placement_failed) {
                if (ctx && ctx->stats_out) {
                    ctx->stats_out->stage_two_failures++;
                }
                emit(LayoutYieldEvent::StageTwoFailure, state, std::numeric_limits<double>::max());
                continue;
            }

            LayoutControllerGrid2D::polish_corridor_positions(state, rng);

            if (!use_greedy_tree) {
                // C# GeneratorPlanner: tree search over per-chain layout variants. Chain i evolves
                // with SA on the partial layout of chains 0..i-1 (masked); up to
                // `max_chain_branching` valid variants are collected per chain and explored
                // depth-first with backtracking when a chain cannot be completed.
                std::vector<bool> active(static_cast<std::size_t>(n), false);
                const int chain_count = static_cast<int>(chains.size());
                const int branching = std::max(1, max_chain_branching);
                struct ChainSnapshot {
                    std::vector<geometry::PolygonGrid2D> outlines;
                    std::vector<geometry::Vector2Int> positions;
                    std::vector<std::optional<RoomTemplateGrid2D>> templates;
                    std::vector<geometry::TransformationGrid2D> transforms;
                };
                const ChainSnapshot base_state{state.outlines, state.positions, state.templates,
                                               state.transforms};

                bool aborted = false;
                std::function<bool(int)> dfs = [&](int ci) -> bool {
                    if (aborted) {
                        return false;
                    }
                    if (ci >= chain_count) {
                        return true;
                    }
                    if (ctx && ctx->poll_abort(iter_count)) {
                        aborted = true;
                        return false;
                    }
                    const auto& chain = chains[static_cast<std::size_t>(ci)];
                    for (const int node : chain.nodes) {
                        active[static_cast<std::size_t>(node)] = true;
                    }

                    const auto& chain_sa_config = sa_provider ? sa_provider->get(chain.number) : sa_config;
                    LayoutControllerGrid2D controller(chain_sa_config);
                    std::vector<Grid2DLayoutState<TRoom>> variants;
                    const std::function<void(const Grid2DLayoutState<TRoom>&)> variant_sink =
                        [&](const Grid2DLayoutState<TRoom>& st) {
                            if (static_cast<int>(variants.size()) < branching) {
                                variants.push_back(st);
                            }
                        };
                    int sa_iters = 0;
                    const int sa_base = iter_count;
                    // C# re-runs the evolver on the same node when it exhausts without a layout;
                    // each run diverges via the advancing RNG stream.
                    for (int attempt = 0; attempt < branching && variants.empty(); ++attempt) {
                        controller.evolve(state, rng, &sa_iters, sa_base, ctx, &chain.nodes,
                                          &room_shapes_handler, &active, variant_sink, branching);
                        if (ctx && ctx->poll_abort(iter_count + sa_iters)) {
                            aborted = true;
                            return false;
                        }
                    }
                    iter_count += sa_iters;
                    if (ctx && ctx->iter_budget_sink) {
                        ctx->publish_iterations(iter_count);
                    }
                    if (ctx && ctx->stats_out) {
                        ctx->stats_out->iterations_since_last_event += sa_iters;
                        ctx->stats_out->chain_number = chain.number;
                    }
                    if (ctx && ctx->poll_abort(iter_count)) {
                        aborted = true;
                        return false;
                    }

                    if (std::getenv("LEVELSYNTH_DEBUG_DFS") != nullptr) {
                        std::fprintf(stderr, "[dfs] chain=%d nodes=%zu variants=%zu penalty=%.2f\n", chain.number,
                                     chain.nodes.size(), variants.size(), 0.0);
                    }
                    for (const auto& variant : variants) {
                        // Apply settled prefix rooms from the variant; later chains reset to the
                        // base (pre-placed) state — variant snapshots hold parked placeholders there.
                        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
                            if (active[i]) {
                                state.outlines[i] = variant.outlines[i];
                                state.positions[i] = variant.positions[i];
                                state.templates[i] = variant.templates[i];
                                state.transforms[i] = variant.transforms[i];
                            } else {
                                state.outlines[i] = base_state.outlines[i];
                                state.positions[i] = base_state.positions[i];
                                state.templates[i] = base_state.templates[i];
                                state.transforms[i] = base_state.transforms[i];
                            }
                        }
                        if (dfs(ci + 1)) {
                            return true;
                        }
                        if (aborted) {
                            return false;
                        }
                    }

                    for (const int node : chain.nodes) {
                        active[static_cast<std::size_t>(node)] = false;
                    }
                    return false;
                };
                (void)dfs(0);
                if (aborted) {
                    sync_stats_iterations();
                    if (ctx && ctx->iter_budget_sink) {
                        ctx->publish_iterations(iter_count);
                    }
                    return Result{safe_to_layout(), iter_count};
                }
            }

            int tcc_iters = 0;
            const int tcc_pass_limit = std::min(64, std::max(8, n));
            const int tcc_base = iter_count;
            LayoutControllerGrid2D::try_complete_chain(state, rng, tcc_pass_limit, &tcc_iters, ctx, tcc_base);
            iter_count += tcc_iters;
            if (ctx && ctx->iter_budget_sink) {
                ctx->publish_iterations(iter_count);
            }
            if (ctx && ctx->stats_out) {
                ctx->stats_out->iterations_since_last_event += tcc_iters;
            }
            if (ctx && ctx->poll_abort(iter_count)) {
                sync_stats_iterations();
                if (ctx->iter_budget_sink) {
                    ctx->publish_iterations(iter_count);
                }
                return Result{safe_to_layout(), iter_count};
            }

            last_penalty = penalty_total(outlines, positions);
            sync_stats_iterations();

            if (last_penalty <= 0.0) {
                success = true;
                emit(LayoutYieldEvent::LayoutGenerated, state, last_penalty);
                if (ctx && ctx->on_valid) {
                    ctx->on_valid(state.to_layout_grid());
                }
                break;
            }
            if (ctx && ctx->stats_out) {
                ctx->stats_out->stage_two_failures++;
            }
            emit(LayoutYieldEvent::StageTwoFailure, state, last_penalty);
        }

        if (!success && ctx && ctx->on_layout &&
            (ctx->layout_stream == LayoutStreamMode::OnEachLayoutGenerated ||
             ctx->layout_stream == LayoutStreamMode::OnEachSaTryCompleteChain)) {
            emit(LayoutYieldEvent::OutOfIterations, state, last_penalty);
        }
        sync_stats_iterations();
        if (ctx && ctx->iter_budget_sink) {
            ctx->publish_iterations(iter_count);
        }

        LayoutGrid2D<TRoom> layout = state.to_layout_grid();
        return Result{std::move(layout), iter_count};
    }
};

} // namespace edgar::generator::grid2d
