#pragma once

// C# `LayoutController` / `SimulatedAnnealingEvolver` subset for Grid2D:
// door-aware greedy placement, perturb shape vs position (0.4 / 0.6),
// energy via `ConstraintsEvaluatorGrid2D`, SA schedule matching C#.
//
// L2 paritet: inner loop order is Perturb -> IsLayoutValid -> IsDifferentEnough ->
// TryCompleteChain on clone -> yield -> Metropolis (independent).
// Random restarts via ShouldRestart(numberOfFailures).

#include "edgar/generator/common/basic_energy_updater.hpp"
#include "edgar/generator/common/simulated_annealing_configuration.hpp"
#include "edgar/generator/grid2d/configuration_spaces_generator.hpp"
#include "edgar/generator/grid2d/configuration_spaces_grid2d.hpp"
#include "edgar/generator/grid2d/constraints_evaluator_grid2d.hpp"
#include "edgar/generator/grid2d/detail/room_index_map.hpp"
#include "edgar/generator/grid2d/door_line_grid2d.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/generator/grid2d/grid2d_layout_state.hpp"
#include "edgar/generator/grid2d/layout_orchestration.hpp"
#include "edgar/generator/grid2d/room_shapes_handler_grid2d.hpp"
#include "edgar/generator/grid2d/room_template_grid2d.hpp"
#include "edgar/generator/grid2d/simulated_annealing_evolver_grid2d.hpp"
#include "edgar/graphs/undirected_graph.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <vector>

namespace edgar::generator::grid2d {

class LayoutControllerGrid2D {
public:
    explicit LayoutControllerGrid2D(common::SimulatedAnnealingConfiguration config = {}) : config_(std::move(config)) {}

    common::SimulatedAnnealingConfiguration& config() { return config_; }
    const common::SimulatedAnnealingConfiguration& config() const { return config_; }

    template <typename TRoom>
    static std::optional<geometry::Vector2Int> greedy_position_from_configuration_spaces(
        int room_index, const LevelDescriptionGrid2D<TRoom>& level, const detail::RoomIndexMap<TRoom>& rmap,
        const graphs::UndirectedAdjacencyListGraph<int>& ig, const geometry::PolygonGrid2D& moving_outline,
        const std::vector<DoorLineGrid2D>& moving_doors, const std::vector<bool>& placed,
        const std::vector<geometry::PolygonGrid2D>& outlines, const std::vector<geometry::Vector2Int>& positions,
        const std::vector<std::vector<DoorLineGrid2D>>& doors_at_index, std::mt19937& rng) {
        std::vector<int> neigh;
        for (int nb : ig.neighbours(room_index)) {
            if (placed[static_cast<std::size_t>(nb)]) {
                neigh.push_back(nb);
            }
        }
        if (neigh.empty()) {
            return std::nullopt;
        }
        const auto& rd = level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(room_index)]);
        std::vector<bool> corridor_by_index(placed.size());
        for (std::size_t i = 0; i < placed.size(); ++i) {
            corridor_by_index[i] =
                level.get_room_description(rmap.index_to_room[i]).is_corridor();
        }
        return sample_maximum_intersection_position(
            moving_outline, moving_doors, neigh, room_index, outlines, positions, doors_at_index, placed, rng, 120,
            rd.is_corridor(), &corridor_by_index);
    }

    template <typename TRoom>
    static void polish_corridor_positions(const LevelDescriptionGrid2D<TRoom>& level,
                                          const detail::RoomIndexMap<TRoom>& rmap,
                                          const graphs::UndirectedAdjacencyListGraph<int>& ig,
                                          std::vector<geometry::PolygonGrid2D>& outlines,
                                          std::vector<geometry::Vector2Int>& positions,
                                          std::vector<std::optional<RoomTemplateGrid2D>>& templates,
                                          std::mt19937& rng) {
        const int n = static_cast<int>(outlines.size());
        if (n <= 0) {
            return;
        }
        std::vector<bool> placed(static_cast<std::size_t>(n), true);
        std::vector<std::vector<DoorLineGrid2D>> doors_tab(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const auto& ot = templates[static_cast<std::size_t>(i)];
            if (ot.has_value()) {
                doors_tab[static_cast<std::size_t>(i)] =
                    ot->doors().get_doors(outlines[static_cast<std::size_t>(i)]);
            }
        }
        for (int i = 0; i < n; ++i) {
            if (!level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(i)]).is_corridor()) {
                continue;
            }
            const auto gp = greedy_position_from_configuration_spaces(
                i, level, rmap, ig, outlines[static_cast<std::size_t>(i)],
                doors_tab[static_cast<std::size_t>(i)], placed, outlines, positions, doors_tab, rng);
            if (gp.has_value()) {
                positions[static_cast<std::size_t>(i)] = *gp;
            }
        }
    }

    template <typename TRoom>
    static bool add_node_greedily(const LevelDescriptionGrid2D<TRoom>& level,
                                  const detail::RoomIndexMap<TRoom>& rmap,
                                  const graphs::UndirectedAdjacencyListGraph<int>& ig,
                                  std::vector<geometry::PolygonGrid2D>& outlines,
                                  std::vector<geometry::Vector2Int>& positions,
                                  std::vector<std::optional<RoomTemplateGrid2D>>& templates,
                                  std::vector<geometry::TransformationGrid2D>& transforms,
                                  std::vector<bool>& placed, int room_index, std::mt19937& rng,
                                  const RoomShapesHandlerGrid2D<TRoom>* room_shapes_handler = nullptr) {
        const int n = static_cast<int>(outlines.size());
        const TRoom rid = rmap.index_to_room[static_cast<std::size_t>(room_index)];
        const auto& rd = level.get_room_description(rid);
        const auto& tmpls = rd.room_templates();

        std::vector<bool> is_corridor(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            is_corridor[static_cast<std::size_t>(i)] =
                level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(i)]).is_corridor();
        }

        std::vector<int> placed_neighbours;
        for (int nb : ig.neighbours(room_index)) {
            if (placed[static_cast<std::size_t>(nb)]) {
                placed_neighbours.push_back(nb);
            }
        }

        double best_energy = std::numeric_limits<double>::max();
        geometry::PolygonGrid2D best_outline = geometry::PolygonGrid2D::get_rectangle(1, 1);
        geometry::Vector2Int best_position = positions[static_cast<std::size_t>(room_index)];
        RoomTemplateGrid2D best_template = tmpls.front();
        geometry::TransformationGrid2D best_transform = geometry::TransformationGrid2D::Identity;
        bool found = false;


        for (const auto& tmpl : tmpls) {
            const auto& trs = tmpl.allowed_transformations();
            std::vector<geometry::TransformationGrid2D> transforms_to_try;
            if (trs.empty()) {
                transforms_to_try.push_back(geometry::TransformationGrid2D::Identity);
            } else {
                transforms_to_try = trs;
            }

            for (const auto& tr : transforms_to_try) {
                geometry::PolygonGrid2D outline = tmpl.outline().transform(tr);
                auto doors = tmpl.doors().get_doors(outline);

                if (!placed_neighbours.empty() && !doors.empty()) {
                    std::vector<std::vector<DoorLineGrid2D>> doors_tab(static_cast<std::size_t>(n));
                    for (int j = 0; j < n; ++j) {
                        if (j == room_index) {
                            doors_tab[static_cast<std::size_t>(j)] = doors;
                            continue;
                        }
                        if (placed[static_cast<std::size_t>(j)] && templates[static_cast<std::size_t>(j)].has_value()) {
                            doors_tab[static_cast<std::size_t>(j)] =
                                templates[static_cast<std::size_t>(j)]->doors().get_doors(outlines[static_cast<std::size_t>(j)]);
                        }
                    }

                    std::vector<geometry::Vector2Int> candidates;
                    for (int nb : placed_neighbours) {
                        if (doors_tab[static_cast<std::size_t>(nb)].empty()) continue;
                        auto space = ConfigurationSpacesGrid2D::configuration_space_between(
                            outline, doors, outlines[static_cast<std::size_t>(nb)],
                            doors_tab[static_cast<std::size_t>(nb)]);
                        auto offsets = enumerate_configuration_space_offsets(space);
                        for (auto& off : offsets) {
                            candidates.push_back(off + positions[static_cast<std::size_t>(nb)]);
                        }
                    }

                    // Filter candidates: only keep those that lie on the CS for ALL placed neighbors.
                    // This mirrors C# `GetMaximumIntersection`.
                    std::vector<geometry::Vector2Int> filtered_candidates;
                    filtered_candidates.reserve(candidates.size());
                    ConfigurationSpacesGenerator cs_check;
                    for (const auto& cand_pos : candidates) {
                        bool ok_all = true;
                        for (int nb : placed_neighbours) {
                            if (doors_tab[static_cast<std::size_t>(nb)].empty()) continue;
                            const auto cs = cs_check.get_configuration_space(
                                outline, doors,
                                outlines[static_cast<std::size_t>(nb)],
                                doors_tab[static_cast<std::size_t>(nb)]);
                            const geometry::Vector2Int delta{
                                cand_pos.x - positions[static_cast<std::size_t>(nb)].x,
                                cand_pos.y - positions[static_cast<std::size_t>(nb)].y};
                            if (!offset_on_configuration_space(delta, cs)) {
                                ok_all = false;
                                break;
                            }
                        }
                        if (ok_all) {
                            filtered_candidates.push_back(cand_pos);
                        }
                    }
                    // If no candidate satisfies all neighbors, fall back to full union.
                    const auto& eval_candidates = filtered_candidates.empty() ? candidates : filtered_candidates;

                    for (const auto& cand_pos : eval_candidates) {
                        bool overlap = false;
                        for (int j = 0; j < n; ++j) {
                            if (j == room_index || !placed[static_cast<std::size_t>(j)]) continue;
                            if (geometry::polygons_overlap_area(outlines[static_cast<std::size_t>(j)],
                                                                positions[static_cast<std::size_t>(j)],
                                                                outline, cand_pos)) {
                                overlap = true;
                                break;
                            }
                        }
                        if (overlap) continue;

                        outlines[static_cast<std::size_t>(room_index)] = outline;
                        positions[static_cast<std::size_t>(room_index)] = cand_pos;
                        const auto vcs = ConstraintsEvaluatorGrid2D::precompute_cs_validity(
                            outlines, positions, doors_tab, ig);
                        auto energy_data = ConstraintsEvaluatorGrid2D::incident_to_room(
                            static_cast<std::size_t>(room_index), outlines, positions, vcs,
                            level.minimum_room_distance, &is_corridor, level.optimize_corridor_constraints,
                            &ig);
                        double penalty = common::BasicEnergyUpdater::total_penalty(energy_data);

                        if (penalty < best_energy) {
                            best_energy = penalty;
                            best_outline = outline;
                            best_position = cand_pos;
                            best_template = tmpl;
                            best_transform = tr;
                            found = true;
                        }
                    }
                } else {
                    // No placed neighbors to anchor on: try random free spots at growing distance
                    // (e.g. first node of a new chain or a node deferred by two-stage ordering).
                    std::uniform_int_distribution<int> jitter(-64, 64);
                    std::optional<geometry::Vector2Int> free_pos;
                    for (int attempt = 0; attempt < 500 && !free_pos.has_value(); ++attempt) {
                        const geometry::Vector2Int cand_pos{
                            positions[static_cast<std::size_t>(room_index)].x + jitter(rng) * (1 + attempt / 50),
                            positions[static_cast<std::size_t>(room_index)].y + jitter(rng) * (1 + attempt / 50)};
                        bool overlap = false;
                        for (int j = 0; j < n; ++j) {
                            if (j == room_index || !placed[static_cast<std::size_t>(j)]) continue;
                            if (geometry::polygons_overlap_area(outlines[static_cast<std::size_t>(j)],
                                                                positions[static_cast<std::size_t>(j)],
                                                                outline, cand_pos)) {
                                overlap = true;
                                break;
                            }
                        }
                        if (!overlap) {
                            free_pos = cand_pos;
                        }
                    }
                    if (free_pos.has_value()) {
                        outlines[static_cast<std::size_t>(room_index)] = outline;
                        positions[static_cast<std::size_t>(room_index)] = *free_pos;
                        auto energy_data = ConstraintsEvaluatorGrid2D::incident_to_room(
                            static_cast<std::size_t>(room_index), outlines, positions,
                            level.minimum_room_distance, &is_corridor, level.optimize_corridor_constraints,
                            &ig);
                        double penalty = common::BasicEnergyUpdater::total_penalty(energy_data);

                        if (penalty < best_energy) {
                            best_energy = penalty;
                            best_outline = outline;
                            best_position = *free_pos;
                            best_template = tmpl;
                            best_transform = tr;
                            found = true;
                        }
                    }
                }
            }
        }

        if (found) {
            outlines[static_cast<std::size_t>(room_index)] = std::move(best_outline);
            positions[static_cast<std::size_t>(room_index)] = best_position;
            templates[static_cast<std::size_t>(room_index)] = std::move(best_template);
            transforms[static_cast<std::size_t>(room_index)] = best_transform;
            placed[static_cast<std::size_t>(room_index)] = true;
        }
        return found;
    }

    template <typename TRoom>
    static bool add_chain_greedy(const LevelDescriptionGrid2D<TRoom>& level,
                                 const detail::RoomIndexMap<TRoom>& rmap,
                                 const graphs::UndirectedAdjacencyListGraph<int>& ig,
                                 std::vector<geometry::PolygonGrid2D>& outlines,
                                 std::vector<geometry::Vector2Int>& positions,
                                 std::vector<std::optional<RoomTemplateGrid2D>>& templates,
                                 std::vector<geometry::TransformationGrid2D>& transforms,
                                 std::vector<bool>& placed, const std::vector<int>& chain_nodes, std::mt19937& rng,
                                 const RoomShapesHandlerGrid2D<TRoom>* room_shapes_handler = nullptr) {
        for (int room_index : chain_nodes) {
            if (placed[static_cast<std::size_t>(room_index)]) continue;
            if (!add_node_greedily(level, rmap, ig, outlines, positions, templates, transforms,
                                   placed, room_index, rng, room_shapes_handler)) {
                return false;
            }
        }
        return true;
    }

    template <typename TRoom>
    static bool try_insert_corridors(const LevelDescriptionGrid2D<TRoom>& level,
                                     const detail::RoomIndexMap<TRoom>& rmap,
                                     const graphs::UndirectedAdjacencyListGraph<int>& ig,
                                     std::vector<geometry::PolygonGrid2D>& outlines,
                                     std::vector<geometry::Vector2Int>& positions,
                                     std::vector<std::optional<RoomTemplateGrid2D>>& templates,
                                     std::vector<geometry::TransformationGrid2D>& transforms,
                                     std::vector<bool>& placed, std::mt19937& rng,
                                     const RoomShapesHandlerGrid2D<TRoom>* room_shapes_handler = nullptr) {
        const int n = static_cast<int>(outlines.size());
        for (int i = 0; i < n; ++i) {
            const TRoom rid = rmap.index_to_room[static_cast<std::size_t>(i)];
            const auto& rd = level.get_room_description(rid);
            if (!rd.is_corridor() || rd.stage() != 2) continue;
            if (placed[static_cast<std::size_t>(i)]) continue;

            if (!add_node_greedily(level, rmap, ig, outlines, positions, templates, transforms,
                                            placed, i, rng, room_shapes_handler)) {
                return false;
            }
        }
        return true;
    }

    template <typename TRoom>
    static bool try_complete_chain(const LevelDescriptionGrid2D<TRoom>& level,
                                   const detail::RoomIndexMap<TRoom>& rmap,
                                   const graphs::UndirectedAdjacencyListGraph<int>& ig,
                                   std::vector<geometry::PolygonGrid2D>& outlines,
                                   std::vector<geometry::Vector2Int>& positions,
                                   std::vector<std::optional<RoomTemplateGrid2D>>& templates,
                                   std::mt19937& rng, int max_passes_without_progress, int* iterations_out,
                                   const ChainGenerateContext<TRoom>* ctx = nullptr,
                                   int chain_base_iterations = 0,
                                   const std::vector<bool>* active_rooms = nullptr) {
        const int n = static_cast<int>(outlines.size());
        if (n <= 0) {
            return true;
        }
        std::vector<bool> is_corridor(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            is_corridor[static_cast<std::size_t>(i)] =
                level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(i)]).is_corridor();
        }
        auto build_doors_tab = [&]() {
            std::vector<std::vector<DoorLineGrid2D>> dt(static_cast<std::size_t>(n));
            for (int j = 0; j < n; ++j) {
                const auto& ot = templates[static_cast<std::size_t>(j)];
                if (ot.has_value()) {
                    dt[static_cast<std::size_t>(j)] =
                        ot->doors().get_doors(outlines[static_cast<std::size_t>(j)]);
                }
            }
            return dt;
        };
        auto eval_full = [&]() {
            const auto dt = build_doors_tab();
            const auto vcs = ConstraintsEvaluatorGrid2D::precompute_cs_validity(outlines, positions, dt, ig);
            return ConstraintsEvaluatorGrid2D::evaluate(
                outlines, positions, vcs, level.minimum_room_distance, &is_corridor,
                level.optimize_corridor_constraints, &ig);
        };
        if (eval_full().is_valid()) {
            return true;
        }
        std::vector<bool> placed(static_cast<std::size_t>(n), true);
        int no_progress = 0;
        int sweep_steps = 0;
        // Hard cap on total sweeps: greedy moves that change positions without reducing the
        // penalty would otherwise reset the no-progress counter forever (C# TryCompleteChain
        // only adds corridors and always terminates).
        const int hard_sweep_cap = std::max(256, 2 * n * max_passes_without_progress);
        while (no_progress < max_passes_without_progress && sweep_steps < hard_sweep_cap) {
            bool progress = false;
            for (int r = 0; r < n; ++r) {
                // Inactive rooms (incremental assembly) stay parked: never repositioned here
                if (active_rooms != nullptr && !(*active_rooms)[static_cast<std::size_t>(r)]) {
                    continue;
                }
                std::vector<std::vector<DoorLineGrid2D>> doors_tab = build_doors_tab();
                const geometry::Vector2Int old_p = positions[static_cast<std::size_t>(r)];
                const auto gp = greedy_position_from_configuration_spaces(
                    r, level, rmap, ig, outlines[static_cast<std::size_t>(r)],
                    doors_tab[static_cast<std::size_t>(r)], placed, outlines, positions, doors_tab, rng);
                if (gp.has_value() && (gp->x != old_p.x || gp->y != old_p.y)) {
                    positions[static_cast<std::size_t>(r)] = *gp;
                    progress = true;
                }
                ++sweep_steps;
                if (ctx && (sweep_steps % 32) == 0 &&
                    ctx->poll_abort(chain_base_iterations + sweep_steps)) {
                    if (iterations_out) {
                        *iterations_out += sweep_steps;
                    }
                    return eval_full().is_valid();
                }
            }
            if (eval_full().is_valid()) {
                if (iterations_out) {
                    *iterations_out += sweep_steps;
                }
                return true;
            }
            if (!progress) {
                ++no_progress;
            } else {
                no_progress = 0;
            }
        }
        if (iterations_out) {
            *iterations_out += sweep_steps;
        }
        return eval_full().is_valid();
    }

    template <typename TRoom>
    void evolve(const LevelDescriptionGrid2D<TRoom>& level, const detail::RoomIndexMap<TRoom>& rmap,
                const graphs::UndirectedAdjacencyListGraph<int>& ig, std::vector<geometry::PolygonGrid2D>& outlines,
                std::vector<geometry::Vector2Int>& positions,
                std::vector<std::optional<RoomTemplateGrid2D>>& templates,
                std::vector<geometry::TransformationGrid2D>& transforms, std::mt19937& rng, int* iterations_out,
                int chain_base_iterations = 0,
                const ChainGenerateContext<TRoom>* ctx = nullptr,
                Grid2DLayoutState<TRoom>* state_for_inner_clone = nullptr,
                const std::vector<int>* chain_nodes = nullptr,
                const RoomShapesHandlerGrid2D<TRoom>* room_shapes_handler = nullptr,
                const std::vector<bool>* active_rooms = nullptr,
                const std::function<void(const Grid2DLayoutState<TRoom>&)>& on_variant = nullptr,
                int max_variants = 0,
                const std::vector<bool>* completion_mask = nullptr) {
        const int n = static_cast<int>(outlines.size());
        if (n <= 0) {
            if (iterations_out) {
                *iterations_out = 0;
            }
            return;
        }

        // Incremental chain assembly (C# GeneratorPlanner/evolver operate on partial layouts
        // holding only the chains added so far): inactive rooms are parked far away with a
        // 1x1 outline and the effective graph drops their edges, so they contribute zero
        // energy and no constraints. Only active rooms are perturbed and written back.
        if (active_rooms != nullptr) {
            bool all_active = true;
            for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
                if (!(*active_rooms)[i]) {
                    all_active = false;
                    break;
                }
            }
            if (!all_active) {
                auto outlines_w = outlines;
                auto positions_w = positions;
                auto templates_w = templates;
                auto transforms_w = transforms;
                int park = 0;
                for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
                    if (!(*active_rooms)[i]) {
                        outlines_w[i] = geometry::PolygonGrid2D::get_square(1);
                        positions_w[i] = {park * 64, 1 << 20};
                        ++park;
                    }
                }
                graphs::UndirectedAdjacencyListGraph<int> ig_w;
                for (const int v : ig.vertices()) {
                    ig_w.add_vertex(v);
                }
                for (const int v : ig.vertices()) {
                    if (!(*active_rooms)[static_cast<std::size_t>(v)]) {
                        continue;
                    }
                    for (const int nb : ig.neighbours(v)) {
                        if (v < nb && (*active_rooms)[static_cast<std::size_t>(nb)]) {
                            ig_w.add_edge(v, nb);
                        }
                    }
                }
                int inner_iters = 0;
                Grid2DLayoutState<TRoom> state_w(level);
                state_w.ig = ig_w;
                state_w.outlines = outlines_w;
                state_w.positions = positions_w;
                state_w.templates = templates_w;
                state_w.transforms = transforms_w;
                evolve(level, rmap, ig_w, state_w.outlines, state_w.positions, state_w.templates,
                       state_w.transforms, rng, &inner_iters, chain_base_iterations, ctx, &state_w, chain_nodes,
                       room_shapes_handler, static_cast<const std::vector<bool>*>(nullptr), on_variant,
                       max_variants, active_rooms);
                outlines_w = state_w.outlines;
                positions_w = state_w.positions;
                templates_w = state_w.templates;
                transforms_w = state_w.transforms;
                for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
                    if ((*active_rooms)[i]) {
                        outlines[i] = outlines_w[i];
                        positions[i] = positions_w[i];
                        templates[i] = templates_w[i];
                        transforms[i] = transforms_w[i];
                    }
                }
                if (iterations_out) {
                    *iterations_out += inner_iters;
                }
                return;
            }
        }

        std::vector<bool> is_corridor(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            is_corridor[static_cast<std::size_t>(i)] =
                level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(i)]).is_corridor();
        }

        auto doors_at_index = [&]() {
            std::vector<std::vector<DoorLineGrid2D>> tab(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                const auto& ot = templates[static_cast<std::size_t>(i)];
                if (ot.has_value()) {
                    tab[static_cast<std::size_t>(i)] =
                        ot->doors().get_doors(outlines[static_cast<std::size_t>(i)]);
                }
            }
            return tab;
        };

        auto full_cs_validity = [&]() {
            const auto dt = doors_at_index();
            return ConstraintsEvaluatorGrid2D::precompute_cs_validity(outlines, positions, dt, ig);
        };

        // Maintained incrementally: updated only for perturbed room's edges.
        std::vector<std::vector<bool>> cs_valid_cache = full_cs_validity();

        // Door lines per room, rebuilt only for the perturbed room (get_doors itself is cached)
        std::vector<std::vector<DoorLineGrid2D>> doors_tab_state = doors_at_index();
        auto update_doors_for_room = [&](int r) {
            const auto& ot = templates[static_cast<std::size_t>(r)];
            doors_tab_state[static_cast<std::size_t>(r)] =
                ot.has_value() ? ot->doors().get_doors(outlines[static_cast<std::size_t>(r)])
                               : std::vector<DoorLineGrid2D>{};
        };

        auto update_cs_for_room = [&](int r) {
            ConstraintsEvaluatorGrid2D::update_cs_validity_for_room(
                static_cast<std::size_t>(r), cs_valid_cache, outlines, positions, doors_tab_state, ig);
        };

        auto overlap_total = [&]() {
            return ConstraintsEvaluatorGrid2D::evaluate(
                outlines, positions, cs_valid_cache, level.minimum_room_distance, &is_corridor,
                level.optimize_corridor_constraints, &ig).overlap_penalty;
        };

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

        std::vector<int> perturbable;
        if (chain_nodes && !chain_nodes->empty()) {
            // C# PerturbLayout never perturbs corridor nodes (they are placed by TryCompleteChain)
            for (const int node : *chain_nodes) {
                const auto& rd = level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(node)]);
                if (!rd.is_corridor()) {
                    perturbable.push_back(node);
                }
            }
            std::sort(perturbable.begin(), perturbable.end());
        } else {
            for (int k = 0; k < n; ++k) {
                const auto& rd = level.get_room_description(rmap.index_to_room[static_cast<std::size_t>(k)]);
                if (!rd.is_corridor()) {
                    perturbable.push_back(k);
                }
            }
        }
        if (perturbable.empty()) {
            // Chain consists of corridors only: nothing to anneal (they are placed by TryCompleteChain)
            if (iterations_out) {
                *iterations_out = 0;
            }
            return;
        }
        std::uniform_int_distribution<int> pick_perturbable(0, static_cast<int>(perturbable.size()) - 1);
        std::uniform_int_distribution<int> pick_dx(-config_.max_perturbation_radius,
                                                     config_.max_perturbation_radius);
        std::uniform_int_distribution<int> pick_dy(-config_.max_perturbation_radius,
                                                     config_.max_perturbation_radius);
        std::uniform_real_distribution<double> uni01(0.0, 1.0);
        std::uniform_real_distribution<double> shape_vs_pos(0.0, 1.0);

        int iterations = 0;
        int last_event_iterations = 0;
        int inner_tcc_iters_sum = 0;
        int inner_layouts_emitted = 0;
        int number_of_failures = 0;
        int stage_two_failures = 0;
        int variants_emitted = 0;
        bool should_stop = false;

        std::vector<bool> placed(static_cast<std::size_t>(n), true);

        auto bb_center = [](const geometry::PolygonGrid2D& poly) -> geometry::Vector2Int {
            const auto& pts = poly.points();
            if (pts.empty()) return {0, 0};
            int min_x = pts[0].x, max_x = pts[0].x;
            int min_y = pts[0].y, max_y = pts[0].y;
            for (std::size_t k = 1; k < pts.size(); ++k) {
                min_x = std::min(min_x, pts[k].x);
                max_x = std::max(max_x, pts[k].x);
                min_y = std::min(min_y, pts[k].y);
                max_y = std::max(max_y, pts[k].y);
            }
            return {(min_x + max_x) / 2, (min_y + max_y) / 2};
        };

        double avg_size = compute_average_room_size(level, rmap);
        const double energy_scale = std::max(1.0, 10.0 * avg_size);

        auto energy = [&]() {
            return common::BasicEnergyUpdater::total_penalty(
                ConstraintsEvaluatorGrid2D::evaluate(outlines, positions, cs_valid_cache, level.minimum_room_distance,
                                                     &is_corridor, level.optimize_corridor_constraints, &ig),
                energy_scale);
        };

        double e = energy();
        double total_overlap = overlap_total();

        struct RoomSnapshot {
            geometry::Vector2Int center;
            const RoomTemplateGrid2D* tmpl;
        };

        std::vector<std::vector<RoomSnapshot>> yielded_snapshots;

        auto make_snapshot = [&]() -> std::vector<RoomSnapshot> {
            std::vector<RoomSnapshot> snap(static_cast<std::size_t>(n));
            for (int idx = 0; idx < n; ++idx) {
                const auto lc = bb_center(outlines[static_cast<std::size_t>(idx)]);
                const auto& pos = positions[static_cast<std::size_t>(idx)];
                snap[static_cast<std::size_t>(idx)] = {
                    {pos.x + lc.x, pos.y + lc.y},
                    templates[static_cast<std::size_t>(idx)].has_value()
                        ? &*templates[static_cast<std::size_t>(idx)] : nullptr
                };
            }
            return snap;
        };

        auto is_different_enough = [&](const std::vector<RoomSnapshot>& candidate) -> bool {
            if (yielded_snapshots.empty()) return true;
            for (const auto& prev : yielded_snapshots) {
                double diff = 0.0;
                for (int idx = 0; idx < n; ++idx) {
                    const auto& c = candidate[static_cast<std::size_t>(idx)];
                    const auto& p = prev[static_cast<std::size_t>(idx)];
                    double center_dist = static_cast<double>(
                        std::abs(c.center.x - p.center.x) + std::abs(c.center.y - p.center.y));
                    double weight = (c.tmpl == p.tmpl) ? 1.0 : 4.0;
                    diff += std::pow(5.0 * center_dist / avg_size, 2.0) * weight;
                }
                diff /= static_cast<double>(n);
                if (0.4 * diff < 1.0) return false;
            }
            return true;
        };

        auto emit_sa = [&](LayoutYieldEvent ev, const Grid2DLayoutState<TRoom>& st, double pen) {
            if (!ctx) {
                return;
            }
            const bool sa_stream = ctx->layout_stream == LayoutStreamMode::OnEachSaTryCompleteChain;
            if (ev == LayoutYieldEvent::LayoutGenerated && sa_stream &&
                ctx->max_layout_yields > 0 && inner_layouts_emitted >= ctx->max_layout_yields) {
                return;
            }

            const int iter_total = chain_base_iterations + iterations + inner_tcc_iters_sum;
            LayoutYieldInfo info;
            info.event_type = ev;
            info.iterations_total = iter_total;
            info.energy = pen;
            if (ctx->stats_out) {
                info.iterations_since_last_event = iterations + inner_tcc_iters_sum - last_event_iterations;
                info.layouts_generated = ctx->stats_out->layouts_generated;
                info.chain_number = ctx->stats_out->chain_number;
                ctx->stats_out->iterations_total = iter_total;
            }
            if (ctx->on_simulated_annealing_event) {
                ctx->on_simulated_annealing_event(info);
            }

            if (ev == LayoutYieldEvent::LayoutGenerated && !sa_stream) {
                return;
            }
            if (!ctx->on_layout) {
                if (ctx->stats_out && ev == LayoutYieldEvent::StageTwoFailure) {
                    ctx->stats_out->stage_two_failures++;
                }
                return;
            }
            if (ev == LayoutYieldEvent::LayoutGenerated) {
                ++inner_layouts_emitted;
            }
            ctx->on_layout(info, st.to_layout_grid());
            if (ctx->stats_out) {
                if (ev == LayoutYieldEvent::LayoutGenerated) {
                    ctx->stats_out->layouts_generated++;
                    ctx->stats_out->iterations_since_last_event = 0;
                } else if (ev == LayoutYieldEvent::StageTwoFailure) {
                    ctx->stats_out->stage_two_failures++;
                }
            }
        };

        for (int i = 0; i < cycles; ++i) {
            if (ctx && ctx->poll_abort(chain_base_iterations + iterations + inner_tcc_iters_sum)) {
                if (iterations_out) {
                    *iterations_out = iterations + inner_tcc_iters_sum;
                }
                return;
            }

            if (should_restart(number_of_failures, rng)) {
                if (state_for_inner_clone) {
                    emit_sa(LayoutYieldEvent::RandomRestart, *state_for_inner_clone, e);
                }
                if (iterations_out) {
                    *iterations_out = iterations + inner_tcc_iters_sum;
                }
                return;
            }

            if (iterations - last_event_iterations > config_.max_iterations_without_success) {
                break;
            }

            if (should_stop) {
                break;
            }

            bool was_accepted = false;

            for (int j = 0; j < config_.trials_per_cycle; ++j) {
                if (stage_two_failures > config_.max_stage_two_failures) {
                    should_stop = true;
                    break;
                }

                ++iterations;
                const int r = perturbable[static_cast<std::size_t>(pick_perturbable(rng))];
                const geometry::Vector2Int old_pos = positions[static_cast<std::size_t>(r)];
                const geometry::PolygonGrid2D old_outline = outlines[static_cast<std::size_t>(r)];
                const std::optional<RoomTemplateGrid2D> old_tmpl = templates[static_cast<std::size_t>(r)];
                const geometry::TransformationGrid2D old_tr = transforms[static_cast<std::size_t>(r)];
                bool did_shape_perturb = false;

                const common::EnergyData incident_old =
                    ConstraintsEvaluatorGrid2D::incident_to_room(static_cast<std::size_t>(r), outlines, positions,
                                                                 cs_valid_cache,
                                                                 level.minimum_room_distance, &is_corridor,
                                                                 level.optimize_corridor_constraints, &ig);
                const double incident_old_tot = common::BasicEnergyUpdater::total_penalty(incident_old, energy_scale);

#ifndef NDEBUG
                if (iterations % 256 == 0) {
                    const double full = energy();
                    assert(std::abs(full - e) < 1e-4);
                }
#endif

                if (shape_vs_pos(rng) < 0.4) {
                    if (room_shapes_handler != nullptr) {
                        std::optional<int> prev_alias = std::nullopt;
                        if (old_tmpl.has_value()) {
                            prev_alias = room_shapes_handler->alias_for(*old_tmpl, old_tr);
                        }
                        auto pick = room_shapes_handler->select_for_room(
                            r, rng, &templates, &transforms, prev_alias);
                        outlines[static_cast<std::size_t>(r)] = pick.outline;
                        templates[static_cast<std::size_t>(r)] = pick.room_template;
                        transforms[static_cast<std::size_t>(r)] = pick.transformation;
                        did_shape_perturb = true;
                    } else {
                        const TRoom rid = rmap.index_to_room[static_cast<std::size_t>(r)];
                        const auto& rd = level.get_room_description(rid);
                        const auto& tmpls = rd.room_templates();
                        if (!tmpls.empty()) {
                            std::uniform_int_distribution<std::size_t> pick_t(0, tmpls.size() - 1);
                            const RoomTemplateGrid2D& tmpl = tmpls[pick_t(rng)];
                            const auto& trs = tmpl.allowed_transformations();
                            geometry::TransformationGrid2D tr = geometry::TransformationGrid2D::Identity;
                            if (!trs.empty()) {
                                std::uniform_int_distribution<std::size_t> pick_tr(0, trs.size() - 1);
                                tr = trs[pick_tr(rng)];
                            }
                            outlines[static_cast<std::size_t>(r)] = tmpl.outline().transform(tr);
                            templates[static_cast<std::size_t>(r)] = tmpl;
                            transforms[static_cast<std::size_t>(r)] = tr;
                            did_shape_perturb = true;
                        }
                    }
                    update_doors_for_room(r);
                    const auto& doors_tab = doors_tab_state;
                    const std::vector<DoorLineGrid2D>& my_doors = doors_tab[static_cast<std::size_t>(r)];
                    std::vector<int> neigh;
                    for (int nb : ig.neighbours(r)) {
                        neigh.push_back(nb);
                    }
                    if (!neigh.empty() && !my_doors.empty()) {
                        const auto np = sample_maximum_intersection_position(
                            outlines[static_cast<std::size_t>(r)], my_doors, neigh, r, outlines, positions,
                            doors_tab, placed, rng, static_cast<std::size_t>(std::max(1, config_.max_cs_perturbation_checks)),
                            is_corridor[static_cast<std::size_t>(r)], &is_corridor);
                        if (np.has_value()) {
                            positions[static_cast<std::size_t>(r)] = *np;
                        }
                    }
                } else {
                    const auto& doors_tab = doors_tab_state;
                    const std::vector<DoorLineGrid2D>& my_doors = doors_tab[static_cast<std::size_t>(r)];
                    std::vector<int> neigh;
                    for (int nb : ig.neighbours(r)) {
                        neigh.push_back(nb);
                    }
                    if (!neigh.empty() && !my_doors.empty()) {
                        const auto np = sample_maximum_intersection_position(
                            outlines[static_cast<std::size_t>(r)], my_doors, neigh, r, outlines, positions,
                            doors_tab, placed, rng, static_cast<std::size_t>(std::max(1, config_.max_cs_perturbation_checks)),
                            is_corridor[static_cast<std::size_t>(r)], &is_corridor);
                        if (np.has_value()) {
                            positions[static_cast<std::size_t>(r)] = *np;
                        } else if (config_.enable_random_walk_fallback && config_.max_perturbation_radius > 0) {
                            positions[static_cast<std::size_t>(r)] = {
                                old_pos.x + pick_dx(rng), old_pos.y + pick_dy(rng)};
                        }
                    } else if (config_.enable_random_walk_fallback && config_.max_perturbation_radius > 0) {
                        positions[static_cast<std::size_t>(r)] = {
                            old_pos.x + pick_dx(rng), old_pos.y + pick_dy(rng)};
                    }
                }

                update_cs_for_room(r);
                const common::EnergyData incident_new =
                    ConstraintsEvaluatorGrid2D::incident_to_room(static_cast<std::size_t>(r), outlines, positions,
                                                                 cs_valid_cache,
                                                                 level.minimum_room_distance, &is_corridor,
                                                                 level.optimize_corridor_constraints, &ig);
                const double new_e =
                    e - incident_old_tot + common::BasicEnergyUpdater::total_penalty(incident_new, energy_scale);
                const double energy_delta = new_e - e;

                // C# `IsLayoutValid`: all nodes must have zero overlap AND zero move-distance.
                const double new_overlap = total_overlap - incident_old.overlap_penalty + incident_new.overlap_penalty;
                const bool is_valid = (new_overlap <= 0.0) && (new_e <= 0.0);

                if (is_valid) {
                    // C# restart bookkeeping (default RestartSuccessPlace.OnValidAndDifferent):
                    // a cycle counts as "not failed" when it produces a valid layout,
                    // NOT merely when Metropolis accepts a move.
                    was_accepted = true;
                    if (ctx && ctx->on_partial_valid && state_for_inner_clone) {
                        ctx->on_partial_valid(state_for_inner_clone->to_layout_grid());
                    }
                    auto snap = make_snapshot();
                    if (is_different_enough(snap)) {
                        if (state_for_inner_clone) {
                            Grid2DLayoutState<TRoom> cl = state_for_inner_clone->clone();

                            std::vector<bool> clone_placed(static_cast<std::size_t>(n), true);
                            for (int idx = 0; idx < n; ++idx) {
                                if (completion_mask != nullptr &&
                                    !(*completion_mask)[static_cast<std::size_t>(idx)]) {
                                    clone_placed[static_cast<std::size_t>(idx)] = true;
                                    continue; // inactive: stays parked, excluded from completion
                                }
                                const TRoom rid2 = rmap.index_to_room[static_cast<std::size_t>(idx)];
                                const auto& rd2 = level.get_room_description(rid2);
                                if (rd2.is_corridor() && rd2.stage() == 2) {
                                    clone_placed[static_cast<std::size_t>(idx)] = false;
                                }
                            }
                            const bool corridors_ok = try_insert_corridors(
                                *cl.level, cl.rmap, cl.ig, cl.outlines, cl.positions,
                                cl.templates, cl.transforms, clone_placed, rng, room_shapes_handler);

                            int tcc_iters = 0;
                            const int tcc_pass = std::min(64, std::max(8, n));
                            const int tcc_chain_base = chain_base_iterations + iterations + inner_tcc_iters_sum;
                            const bool tcc_ok = corridors_ok && try_complete_chain(
                                *cl.level, cl.rmap, cl.ig, cl.outlines, cl.positions, cl.templates, rng, tcc_pass,
                                &tcc_iters, ctx, tcc_chain_base, completion_mask);
                            inner_tcc_iters_sum += tcc_iters;
                            if (ctx && ctx->stats_out) {
                                ctx->stats_out->iterations_since_last_event += tcc_iters;
                            }
                            std::vector<std::vector<DoorLineGrid2D>> cl_doors(static_cast<std::size_t>(n));
                            for (int idx2 = 0; idx2 < n; ++idx2) {
                                if (cl.templates[static_cast<std::size_t>(idx2)].has_value()) {
                                    cl_doors[static_cast<std::size_t>(idx2)] =
                                        cl.templates[static_cast<std::size_t>(idx2)]->doors().get_doors(
                                            cl.outlines[static_cast<std::size_t>(idx2)]);
                                }
                            }
                            const auto cl_vcs = ConstraintsEvaluatorGrid2D::precompute_cs_validity(
                                cl.outlines, cl.positions, cl_doors, cl.ig);
                            const double pen_after =
                                common::BasicEnergyUpdater::total_penalty(ConstraintsEvaluatorGrid2D::evaluate(
                                    cl.outlines, cl.positions, cl_vcs, level.minimum_room_distance,
                                    &is_corridor, level.optimize_corridor_constraints, &cl.ig),
                                    energy_scale);
                            if (tcc_ok) {
                                yielded_snapshots.push_back(std::move(snap));
                                emit_sa(LayoutYieldEvent::LayoutGenerated, cl, pen_after);
                                last_event_iterations = iterations + inner_tcc_iters_sum;
                                stage_two_failures = 0;
                                // C# Evolve(..., count): collect valid variants of the chain for the
                                // planner's tree search; stop the SA once enough are gathered.
                                if (on_variant) {
                                    on_variant(cl);
                                    if (max_variants > 0) {
                                        ++variants_emitted;
                                        if (variants_emitted >= max_variants) {
                                            if (iterations_out) {
                                                *iterations_out = iterations + inner_tcc_iters_sum;
                                            }
                                            return;
                                        }
                                    }
                                }
                            } else {
                                stage_two_failures++;
                                emit_sa(LayoutYieldEvent::StageTwoFailure, cl, pen_after);
                            }
                        }
                    }
                }

                // Metropolis accept/reject (independent of TCC above)
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
                    total_overlap = new_overlap;
                    delta_e_avg = (delta_e_avg * static_cast<double>(accepted_solutions - 1) + delta_abs) /
                                  static_cast<double>(accepted_solutions);
                    if (ctx && ctx->on_perturbed && state_for_inner_clone) {
                        ctx->on_perturbed(state_for_inner_clone->to_layout_grid());
                    }
                } else {
                    positions[static_cast<std::size_t>(r)] = old_pos;
                    if (did_shape_perturb) {
                        outlines[static_cast<std::size_t>(r)] = old_outline;
                        templates[static_cast<std::size_t>(r)] = old_tmpl;
                        transforms[static_cast<std::size_t>(r)] = old_tr;
                        update_doors_for_room(r);
                    }
                    update_cs_for_room(r);
                }

                if (ctx && (iterations % 32) == 0 &&
                    ctx->poll_abort(chain_base_iterations + iterations + inner_tcc_iters_sum)) {
                    should_stop = true;
                    break;
                }

                if (e <= 0.0) {
                    if (iterations_out) {
                        *iterations_out = iterations + inner_tcc_iters_sum;
                    }
                    return;
                }
            }

            if (!was_accepted) {
                number_of_failures++;
            }
            t *= ratio;
        }

        if (state_for_inner_clone && ctx && ctx->on_layout) {
            emit_sa(LayoutYieldEvent::OutOfIterations, *state_for_inner_clone, e);
        }

        if (iterations_out) {
            *iterations_out = iterations + inner_tcc_iters_sum;
        }
    }

    void evolve_random_walk(std::vector<geometry::PolygonGrid2D>& outlines,
                            std::vector<geometry::Vector2Int>& positions, std::mt19937& rng,
                            int* iterations_out) const {
        SimulatedAnnealingEvolverGrid2D ev(config_);
        ev.evolve(outlines, positions, rng, iterations_out);
    }

    template <typename TRoom>
    static void polish_corridor_positions(Grid2DLayoutState<TRoom>& state, std::mt19937& rng) {
        polish_corridor_positions(*state.level, state.rmap, state.ig, state.outlines, state.positions, state.templates,
                                  rng);
    }

    template <typename TRoom>
    static bool try_complete_chain(Grid2DLayoutState<TRoom>& state, std::mt19937& rng, int max_passes_without_progress,
                                   int* iterations_out, const ChainGenerateContext<TRoom>* ctx = nullptr,
                                   int chain_base_iterations = 0) {
        return try_complete_chain(*state.level, state.rmap, state.ig, state.outlines, state.positions, state.templates,
                                  rng, max_passes_without_progress, iterations_out, ctx, chain_base_iterations);
    }

    template <typename TRoom>
    void evolve(Grid2DLayoutState<TRoom>& state, std::mt19937& rng, int* iterations_out,
                 int chain_base_iterations = 0, const ChainGenerateContext<TRoom>* ctx = nullptr,
                 const std::vector<int>* chain_nodes = nullptr,
                 const RoomShapesHandlerGrid2D<TRoom>* room_shapes_handler = nullptr,
                 const std::vector<bool>* active_rooms = nullptr,
                 const std::function<void(const Grid2DLayoutState<TRoom>&)>& on_variant = nullptr,
                 int max_variants = 0) {
        evolve(*state.level, state.rmap, state.ig, state.outlines, state.positions, state.templates, state.transforms,
               rng, iterations_out, chain_base_iterations, ctx, &state, chain_nodes, room_shapes_handler,
               active_rooms, on_variant, max_variants);
    }

private:
    common::SimulatedAnnealingConfiguration config_;

    static bool should_restart(int number_of_failures, std::mt19937& rng) {
        std::uniform_int_distribution<int> dist;
        if (number_of_failures > 8 && dist(rng, std::uniform_int_distribution<int>::param_type{0, 1}) == 0)
            return true;
        if (number_of_failures > 6 && dist(rng, std::uniform_int_distribution<int>::param_type{0, 2}) == 0)
            return true;
        if (number_of_failures > 4 && dist(rng, std::uniform_int_distribution<int>::param_type{0, 4}) == 0)
            return true;
        if (number_of_failures > 2 && dist(rng, std::uniform_int_distribution<int>::param_type{0, 6}) == 0)
            return true;
        return false;
    }

    template <typename TRoom>
    static double compute_average_room_size(const LevelDescriptionGrid2D<TRoom>& level,
                                             const detail::RoomIndexMap<TRoom>& rmap) {
        double total = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < rmap.index_to_room.size(); ++i) {
            const auto& rd = level.get_room_description(rmap.index_to_room[i]);
            for (const auto& tmpl : rd.room_templates()) {
                const auto& pts = tmpl.outline().points();
                if (pts.empty()) continue;
                int min_x = pts[0].x, max_x = pts[0].x;
                int min_y = pts[0].y, max_y = pts[0].y;
                for (std::size_t k = 1; k < pts.size(); ++k) {
                    min_x = std::min(min_x, pts[k].x);
                    max_x = std::max(max_x, pts[k].x);
                    min_y = std::min(min_y, pts[k].y);
                    max_y = std::max(max_y, pts[k].y);
                }
                total += static_cast<double>((max_x - min_x) + (max_y - min_y)) / 2.0;
                ++count;
            }
        }
        return (count > 0) ? total / static_cast<double>(count) : 10.0;
    }
};

} // namespace edgar::generator::grid2d
