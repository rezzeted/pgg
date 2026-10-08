#pragma once

// C# `BasicLayoutConverterGrid2D`: internal layout state -> public `LayoutGrid2D`.
// Include this only after `grid2d_layout_state.hpp` (full `Grid2DLayoutState` definition).

#include "edgar/generator/grid2d/layout_door_computation.hpp"
#include "edgar/generator/grid2d/layout_grid2d.hpp"
#include "edgar/generator/grid2d/layout_room_grid2d.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/generator/grid2d/room_template_grid2d.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"

#include <random>

namespace edgar::generator::grid2d {

template <typename TRoom>
struct Grid2DLayoutState;

/// Converts `Grid2DLayoutState` to the public `LayoutGrid2D` representation (C# `BasicLayoutConverterGrid2D`).
/// Preconditions for `convert(state)`: `state.outlines`, `positions`, `templates`, `transforms` must have size
/// `room_count()`; every `state.templates[i]` must be `has_value()` for `i in [0, n)` (same contract as the previous
/// `Grid2DLayoutState::to_layout_grid` implementation).
template <typename TRoom>
struct BasicLayoutConverterGrid2D {
    static LayoutGrid2D<TRoom> convert(const Grid2DLayoutState<TRoom>& state) {
        const int n = state.room_count();
        LayoutGrid2D<TRoom> layout;
        layout.rooms.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const TRoom id = state.rmap.index_to_room[static_cast<std::size_t>(i)];
            const auto& rd = state.level->get_room_description(id);
            layout.rooms.push_back(LayoutRoomGrid2D<TRoom>{
                .room = id,
                .outline = state.outlines[static_cast<std::size_t>(i)],
                .position = state.positions[static_cast<std::size_t>(i)],
                .is_corridor = rd.is_corridor(),
                .room_template = *state.templates[static_cast<std::size_t>(i)],
                .room_description = rd,
                .transformation = state.transforms[static_cast<std::size_t>(i)],
            });
        }
        return layout;
    }

    /// Same as `convert(state)`; if `add_doors` is true, calls `compute_layout_doors` using the level graph
    /// (`level->get_graph()`). Requires `state.level != nullptr`.
    static LayoutGrid2D<TRoom> convert(const Grid2DLayoutState<TRoom>& state, bool add_doors, std::mt19937& rng) {
        auto layout = convert(state);
        if (add_doors && state.level) {
            const auto graph = state.level->get_graph();
            compute_layout_doors(layout, *state.level, graph, rng);
        }
        return layout;
    }

    /// Shared helper for strip backend: one public room from placement (same fields as `convert` per room).
    static LayoutRoomGrid2D<TRoom> make_room(TRoom room_id, const geometry::PolygonGrid2D& outline,
                                             const geometry::Vector2Int& position,
                                             const RoomDescriptionGrid2D& room_desc,
                                             const RoomTemplateGrid2D& room_template,
                                             const geometry::TransformationGrid2D& transformation) {
        return LayoutRoomGrid2D<TRoom>{
            .room = room_id,
            .outline = outline,
            .position = position,
            .is_corridor = room_desc.is_corridor(),
            .room_template = room_template,
            .room_description = room_desc,
            .transformation = transformation,
        };
    }
};

} // namespace edgar::generator::grid2d
