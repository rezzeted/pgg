#pragma once

#include <atomic>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dungeon_topology_generator/generator/grid2d/chain_based_generator_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/room_shapes_handler_grid2d.hpp"
#include "dungeon_topology_generator/geometry/overlap.hpp"

namespace dungeon_topology_generator::generator::grid2d {

namespace detail {

inline int min_coord_x(const geometry::PolygonGrid2D& poly) {
    int v = std::numeric_limits<int>::max();
    for (const auto& p : poly.points()) {
        v = std::min(v, p.x);
    }
    return v;
}

inline int min_coord_y(const geometry::PolygonGrid2D& poly) {
    int v = std::numeric_limits<int>::max();
    for (const auto& p : poly.points()) {
        v = std::min(v, p.y);
    }
    return v;
}

inline int max_world_x(const geometry::PolygonGrid2D& poly, geometry::Vector2Int pos) {
    int mx = INT_MIN;
    for (const auto& p : poly.points()) {
        mx = std::max(mx, p.x + pos.x);
    }
    return mx;
}

} // namespace detail

template <typename TRoom>
GraphBasedGeneratorGrid2D<TRoom>::GraphBasedGeneratorGrid2D(const LevelDescriptionGrid2D<TRoom>& level_description,
                                                          GraphBasedGeneratorConfiguration configuration)
    : level_(level_description), configuration_(std::move(configuration)) {}

template <typename TRoom>
void GraphBasedGeneratorGrid2D<TRoom>::inject_random_generator(std::mt19937 rng) {
    rng_ = std::move(rng);
}

template <typename TRoom>
void GraphBasedGeneratorGrid2D<TRoom>::request_cancel() {
    if (early_stop_configured()) {
        throw std::logic_error(
            "GraphBasedGeneratorGrid2D::request_cancel: early-stop iteration/time limits are mutually exclusive "
            "with cancellation (matches C# GraphBasedGeneratorGrid2D.SetCancellationToken).");
    }
    cancellation_flag_->store(true, std::memory_order_release);
}

template <typename TRoom>
void GraphBasedGeneratorGrid2D<TRoom>::reset_cancellation() {
    if (!early_stop_configured()) {
        cancellation_flag_->store(false, std::memory_order_release);
    }
}

template <typename TRoom>
LayoutGrid2D<TRoom> GraphBasedGeneratorGrid2D<TRoom>::generate_layout() {
    const auto& now_fn = configuration_.steady_clock_now;
    const auto t0 = now_fn();
    iterations_count_ = 0;
    level_.optimize_corridor_constraints = configuration_.optimize_corridor_constraints;

    std::mt19937 rng = rng_.has_value() ? *rng_ : std::mt19937{std::random_device{}()};

    if (configuration_.backend == GraphBasedGeneratorBackend::chain_simulated_annealing) {
        orchestration_stats_ = {};
        ChainGenerateContext<TRoom> gen_ctx;
        gen_ctx.layout_stream = configuration_.layout_stream_mode;
        gen_ctx.max_layout_yields = configuration_.max_layout_yields;
        gen_ctx.on_layout = layout_yield_callback_;
        gen_ctx.on_simulated_annealing_event = on_simulated_annealing_event_;
        gen_ctx.on_valid = on_valid_;
        gen_ctx.on_partial_valid = on_partial_valid_;
        gen_ctx.on_perturbed = on_perturbed_;
        gen_ctx.stats_out = &orchestration_stats_;
        gen_ctx.wall_start = t0;
        gen_ctx.now_fn = now_fn;
        gen_ctx.early_stop_max_total_iterations = configuration_.early_stop_max_total_iterations;
        gen_ctx.early_stop_max_elapsed = configuration_.early_stop_max_elapsed;
        gen_ctx.iter_budget_sink = &iterations_count_;
        gen_ctx.cancellation_requested = early_stop_configured() ? nullptr : cancellation_flag_;
        const common::SAConfigurationProvider* sa_provider = nullptr;
        if (configuration_.sa_config_provider.has_value()) {
            sa_provider = &(*configuration_.sa_config_provider);
        }
        const auto res = ChainBasedGeneratorGrid2D<TRoom>::generate(
            level_, configuration_.simulated_annealing, rng, configuration_.chain_decomposition,
            configuration_.chain_decomposition_configuration, &gen_ctx, sa_provider,
            configuration_.simulated_annealing_max_branching);
        iterations_count_ = res.iterations;
        const auto t1 = now_fn();
        time_total_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (rng_.has_value()) {
            rng_ = std::move(rng);
        }
        return res.layout;
    }

    std::vector<TRoom> order;
    order.reserve(level_.rooms().size());
    for (const auto& kv : level_.rooms()) {
        order.push_back(kv.first);
    }
    std::sort(order.begin(), order.end());

    struct Placed {
        geometry::PolygonGrid2D outline;
        geometry::Vector2Int position{};
    };
    std::vector<Placed> placed;
    placed.reserve(order.size());
    LevelDescriptionMappingGrid2D<TRoom> mapping(level_);
    RoomShapesHandlerGrid2D<TRoom> room_shapes_handler(level_, mapping);
    std::vector<std::optional<RoomTemplateGrid2D>> picked_templates(order.size(), std::nullopt);
    std::vector<geometry::TransformationGrid2D> picked_transforms(
        order.size(), geometry::TransformationGrid2D::Identity);

    int cursor_x = 0;

    LayoutGrid2D<TRoom> result;

    auto strip_should_abort = [&]() -> bool {
        if (configuration_.early_stop_max_total_iterations.has_value() &&
            iterations_count_ >= configuration_.early_stop_max_total_iterations.value()) {
            return true;
        }
        if (configuration_.early_stop_max_elapsed.has_value()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_fn() - t0);
            if (elapsed >= configuration_.early_stop_max_elapsed.value()) {
                return true;
            }
        }
        if (!early_stop_configured() && cancellation_flag_->load(std::memory_order_acquire)) {
            return true;
        }
        return false;
    };

    auto finish_strip = [&]() -> LayoutGrid2D<TRoom> {
        const auto t1 = now_fn();
        time_total_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (rng_.has_value()) {
            rng_ = std::move(rng);
        }
        return result;
    };

    for (const auto& room : order) {
        const auto& room_desc = level_.get_room_description(room);
        (void)room_desc;
        const int room_index = mapping.room_index(room);
        auto picked = room_shapes_handler.select_for_room(room_index, rng, &picked_templates, &picked_transforms);
        const RoomTemplateGrid2D tmpl = picked.room_template;
        const geometry::TransformationGrid2D tr = picked.transformation;
        geometry::PolygonGrid2D outline = std::move(picked.outline);

        bool placed_ok = false;
        for (int attempt = 0; attempt < 10000; ++attempt) {
            ++iterations_count_;
            if (strip_should_abort()) {
                return finish_strip();
            }
            const int min_px = detail::min_coord_x(outline);
            const int min_py = detail::min_coord_y(outline);
            const geometry::Vector2Int pos{cursor_x - min_px, -min_py};

            bool overlap = false;
            for (const auto& p : placed) {
                if (geometry::polygons_overlap_area(outline, pos, p.outline, p.position)) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                placed.push_back(Placed{outline, pos});
                result.rooms.push_back(
                    BasicLayoutConverterGrid2D<TRoom>::make_room(room, outline, pos, room_desc, tmpl, tr));

                const int mx = detail::max_world_x(outline, pos);
                cursor_x = mx + level_.minimum_room_distance + 1 + configuration_.strip_gap_cells;
                picked_templates[static_cast<std::size_t>(room_index)] = tmpl;
                picked_transforms[static_cast<std::size_t>(room_index)] = tr;
                placed_ok = true;
                break;
            }
            ++cursor_x;
        }

        if (!placed_ok) {
            throw std::runtime_error("GraphBasedGeneratorGrid2D: failed to place room without overlap");
        }
    }

    return finish_strip();
}

} // namespace dungeon_topology_generator::generator::grid2d
