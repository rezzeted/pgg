#pragma once

#include "edgar/generator/grid2d/level_description_mapping_grid2d.hpp"
#include "edgar/generator/grid2d/layout_grid2d.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/generator/grid2d/room_template_grid2d.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <optional>
#include <vector>

namespace edgar::generator::grid2d {

/// Mutable Grid2D placement state (C# `ILayout` / `SmartClone` analogue): one object for outlines,
/// positions, templates, and transforms indexed consistently with `RoomIndexMap`.
template <typename TRoom>
struct Grid2DLayoutState {
    const LevelDescriptionGrid2D<TRoom>* level = nullptr;
    LevelDescriptionMappingGrid2D<TRoom> rmap;
    graphs::UndirectedAdjacencyListGraph<int> ig;

    std::vector<geometry::PolygonGrid2D> outlines;
    std::vector<geometry::Vector2Int> positions;
    std::vector<std::optional<RoomTemplateGrid2D>> templates;
    std::vector<geometry::TransformationGrid2D> transforms;

    explicit Grid2DLayoutState(const LevelDescriptionGrid2D<TRoom>& lvl)
        : level(&lvl), rmap(lvl), ig(rmap.int_graph(lvl)) {}

    int room_count() const { return static_cast<int>(rmap.index_to_room.size()); }

    void resize_room_slots(int n) {
        outlines.assign(static_cast<std::size_t>(n), geometry::PolygonGrid2D::get_rectangle(1, 1));
        positions.assign(static_cast<std::size_t>(n), geometry::Vector2Int{});
        transforms.assign(static_cast<std::size_t>(n), geometry::TransformationGrid2D::Identity);
        templates.assign(static_cast<std::size_t>(n), std::nullopt);
    }

    Grid2DLayoutState clone() const {
        Grid2DLayoutState copy(*level);
        copy.outlines = outlines;
        copy.positions = positions;
        copy.templates = templates;
        copy.transforms = transforms;
        return copy;
    }

    LayoutGrid2D<TRoom> to_layout_grid() const;
};

} // namespace edgar::generator::grid2d

#include "edgar/generator/grid2d/basic_layout_converter_grid2d.hpp"

namespace edgar::generator::grid2d {

template <typename TRoom>
inline LayoutGrid2D<TRoom> Grid2DLayoutState<TRoom>::to_layout_grid() const {
    return BasicLayoutConverterGrid2D<TRoom>::convert(*this);
}

} // namespace edgar::generator::grid2d
