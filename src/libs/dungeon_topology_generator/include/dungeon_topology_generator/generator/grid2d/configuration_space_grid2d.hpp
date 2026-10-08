#pragma once

#include "dungeon_topology_generator/generator/grid2d/door_line_grid2d.hpp"
#include "dungeon_topology_generator/geometry/orthogonal_line_grid2d.hpp"

#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d {

struct ConfigurationSpaceGrid2D {
    std::vector<geometry::OrthogonalLineGrid2D> lines;
    std::vector<std::pair<geometry::OrthogonalLineGrid2D, DoorLineGrid2D>> reverse_doors;

    ConfigurationSpaceGrid2D() = default;
    ConfigurationSpaceGrid2D(std::vector<geometry::OrthogonalLineGrid2D> lines_,
                             std::vector<std::pair<geometry::OrthogonalLineGrid2D, DoorLineGrid2D>> reverse_doors_)
        : lines(std::move(lines_)), reverse_doors(std::move(reverse_doors_)) {}

    /// Lazily built set of all grid points covered by `lines` (shared between copies).
    /// Hot-path membership checks (SA perturbation) use this instead of scanning lines.
    const std::unordered_set<geometry::Vector2Int>& points() const;
    bool contains_offset(geometry::Vector2Int p) const;

private:
    mutable std::shared_ptr<const std::unordered_set<geometry::Vector2Int>> points_cache_;
};

} // namespace dungeon_topology_generator::generator::grid2d
