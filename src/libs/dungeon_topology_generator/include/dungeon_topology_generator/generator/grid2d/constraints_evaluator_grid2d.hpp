#pragma once

#include "dungeon_topology_generator/generator/grid2d/constraints/basic_constraint_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/constraints/corridor_constraint_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/constraints/minimum_distance_constraint_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/configuration_space_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/configuration_spaces_generator.hpp"
#include "dungeon_topology_generator/generator/grid2d/configuration_spaces_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/door_line_grid2d.hpp"
#include "dungeon_topology_generator/generator/common/energy_data.hpp"
#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"
#include "dungeon_topology_generator/graphs/undirected_graph.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d {

/// C# `ConstraintsEvaluator` — overlap + door-distance, optional corridor / minimum-distance terms.
///
/// The `valid_on_cs` matrix encodes, for each pair (i,j), whether:
///   - i,j are graph neighbors AND their relative position lies on a valid door CS position.
/// Non-graph-neighbor pairs have `valid_on_cs[i][j] == true` (no penalty).
/// This matrix is precomputed once per evaluation context via `precompute_cs_validity()`.
class ConstraintsEvaluatorGrid2D {
public:
    /// Precompute CS validity for all graph-neighbor pairs.
    /// `valid_on_cs[i][j]` is true if (i,j) are NOT graph neighbors, or they ARE and the offset lies on CS.
    /// Non-graph-neighbor pairs are true (no door penalty needed).
    static std::vector<std::vector<bool>> precompute_cs_validity(
        const std::vector<geometry::PolygonGrid2D>& outlines,
        const std::vector<geometry::Vector2Int>& positions,
        const std::vector<std::vector<DoorLineGrid2D>>& doors,
        const graphs::UndirectedAdjacencyListGraph<int>& ig)
    {
        const std::size_t n = outlines.size();
        std::vector<std::vector<bool>> valid(n, std::vector<bool>(n, true));
        ConfigurationSpacesGenerator cs_gen;
        for (std::size_t i = 0; i < n; ++i) {
            for (int nb : ig.neighbours(static_cast<int>(i))) {
                const auto j = static_cast<std::size_t>(nb);
                if (j <= i) continue;
                if (doors[i].empty() || doors[j].empty()) continue;
                const auto cs = cs_gen.get_configuration_space(
                    outlines[j], doors[j], outlines[i], doors[i]);
                const geometry::Vector2Int delta{
                    positions[j].x - positions[i].x,
                    positions[j].y - positions[i].y};
                const bool ok = offset_on_configuration_space(delta, cs);
                valid[i][j] = ok;
                valid[j][i] = ok;
            }
        }
        return valid;
    }

    /// Incrementally update CS validity for all pairs incident to room `r`.
    /// Only recomputes CS for graph edges touching `r`, leaving others unchanged.
    static void update_cs_validity_for_room(
        std::size_t r,
        std::vector<std::vector<bool>>& valid_on_cs,
        const std::vector<geometry::PolygonGrid2D>& outlines,
        const std::vector<geometry::Vector2Int>& positions,
        const std::vector<std::vector<DoorLineGrid2D>>& doors,
        const graphs::UndirectedAdjacencyListGraph<int>& ig)
    {
        ConfigurationSpacesGenerator cs_gen;
        for (int nb : ig.neighbours(static_cast<int>(r))) {
            const auto j = static_cast<std::size_t>(nb);
            if (doors[r].empty() || doors[j].empty()) {
                valid_on_cs[r][j] = true;
                valid_on_cs[j][r] = true;
                continue;
            }
            const auto cs = cs_gen.get_configuration_space(
                outlines[j], doors[j], outlines[r], doors[r]);
            const geometry::Vector2Int delta{
                positions[j].x - positions[r].x,
                positions[j].y - positions[r].y};
            const bool ok = offset_on_configuration_space(delta, cs);
            valid_on_cs[r][j] = ok;
            valid_on_cs[j][r] = ok;
        }
    }

    /// Legacy overload (overlap only, no door-distance).
    static common::EnergyData evaluate_pair(std::size_t i, std::size_t j,
                                            const std::vector<geometry::PolygonGrid2D>& outlines,
                                            const std::vector<geometry::Vector2Int>& positions,
                                            int minimum_room_distance = 0,
                                            const std::vector<bool>* is_corridor = nullptr,
                                            bool optimize_corridor_constraints = true,
                                            const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        const auto basic = constraints::BasicConstraintGrid2D::evaluate_pair(i, j, outlines, positions);
        const auto corridor = constraints::CorridorConstraintGrid2D::evaluate_pair(
            i, j, basic, is_corridor, optimize_corridor_constraints);
        const auto min_distance = constraints::MinimumDistanceConstraintGrid2D::evaluate_pair(
            i, j, outlines, positions, minimum_room_distance, graph);
        out.overlap_penalty += basic.overlap_penalty;
        out.corridor_penalty += corridor.corridor_penalty;
        out.minimum_distance_penalty += min_distance.minimum_distance_penalty;
        return out;
    }

    /// Full overload with precomputed `valid_on_cs` matrix (C# parity).
    static common::EnergyData evaluate_pair(std::size_t i, std::size_t j,
                                            const std::vector<geometry::PolygonGrid2D>& outlines,
                                            const std::vector<geometry::Vector2Int>& positions,
                                            const std::vector<std::vector<bool>>& valid_on_cs,
                                            int minimum_room_distance = 0,
                                            const std::vector<bool>* is_corridor = nullptr,
                                            bool optimize_corridor_constraints = true,
                                            const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        const auto basic = constraints::BasicConstraintGrid2D::evaluate_pair(i, j, outlines, positions, valid_on_cs);
        const auto corridor = constraints::CorridorConstraintGrid2D::evaluate_pair(
            i, j, basic, is_corridor, optimize_corridor_constraints);
        const auto min_distance = constraints::MinimumDistanceConstraintGrid2D::evaluate_pair(
            i, j, outlines, positions, minimum_room_distance, graph);
        out.overlap_penalty += basic.overlap_penalty;
        out.move_distance_penalty += basic.move_distance_penalty;
        out.corridor_penalty += corridor.corridor_penalty;
        out.minimum_distance_penalty += min_distance.minimum_distance_penalty;
        return out;
    }

    /// Legacy incident_to_room (no door-distance).
    static common::EnergyData incident_to_room(std::size_t r, const std::vector<geometry::PolygonGrid2D>& outlines,
                                               const std::vector<geometry::Vector2Int>& positions,
                                               int minimum_room_distance = 0,
                                               const std::vector<bool>* is_corridor = nullptr,
                                               bool optimize_corridor_constraints = true,
                                               const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        for (std::size_t j = 0; j < outlines.size(); ++j) {
            if (j == r) continue;
            const std::size_t a = std::min(r, j);
            const std::size_t b = std::max(r, j);
            const auto p = evaluate_pair(a, b, outlines, positions, minimum_room_distance,
                                         is_corridor, optimize_corridor_constraints, graph);
            out.overlap_penalty += p.overlap_penalty;
            out.corridor_penalty += p.corridor_penalty;
            out.minimum_distance_penalty += p.minimum_distance_penalty;
        }
        return out;
    }

    /// Full incident_to_room with precomputed CS validity.
    static common::EnergyData incident_to_room(std::size_t r, const std::vector<geometry::PolygonGrid2D>& outlines,
                                               const std::vector<geometry::Vector2Int>& positions,
                                               const std::vector<std::vector<bool>>& valid_on_cs,
                                               int minimum_room_distance = 0,
                                               const std::vector<bool>* is_corridor = nullptr,
                                               bool optimize_corridor_constraints = true,
                                               const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        for (std::size_t j = 0; j < outlines.size(); ++j) {
            if (j == r) continue;
            const std::size_t a = std::min(r, j);
            const std::size_t b = std::max(r, j);
            const auto p = evaluate_pair(a, b, outlines, positions, valid_on_cs, minimum_room_distance,
                                         is_corridor, optimize_corridor_constraints, graph);
            out.overlap_penalty += p.overlap_penalty;
            out.move_distance_penalty += p.move_distance_penalty;
            out.corridor_penalty += p.corridor_penalty;
            out.minimum_distance_penalty += p.minimum_distance_penalty;
        }
        return out;
    }

    /// Legacy evaluate (no door-distance).
    static common::EnergyData evaluate(const std::vector<geometry::PolygonGrid2D>& outlines,
                                       const std::vector<geometry::Vector2Int>& positions,
                                       int minimum_room_distance = 0,
                                       const std::vector<bool>* is_corridor = nullptr,
                                       bool optimize_corridor_constraints = true,
                                       const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        for (std::size_t i = 0; i < outlines.size(); ++i) {
            for (std::size_t j = i + 1; j < outlines.size(); ++j) {
                const auto p = evaluate_pair(i, j, outlines, positions, minimum_room_distance,
                                             is_corridor, optimize_corridor_constraints, graph);
                out.overlap_penalty += p.overlap_penalty;
                out.corridor_penalty += p.corridor_penalty;
                out.minimum_distance_penalty += p.minimum_distance_penalty;
            }
        }
        return out;
    }

    /// Full evaluate with precomputed CS validity.
    static common::EnergyData evaluate(const std::vector<geometry::PolygonGrid2D>& outlines,
                                       const std::vector<geometry::Vector2Int>& positions,
                                       const std::vector<std::vector<bool>>& valid_on_cs,
                                       int minimum_room_distance = 0,
                                       const std::vector<bool>* is_corridor = nullptr,
                                       bool optimize_corridor_constraints = true,
                                       const graphs::UndirectedAdjacencyListGraph<int>* graph = nullptr) {
        common::EnergyData out;
        for (std::size_t i = 0; i < outlines.size(); ++i) {
            for (std::size_t j = i + 1; j < outlines.size(); ++j) {
                const auto p = evaluate_pair(i, j, outlines, positions, valid_on_cs, minimum_room_distance,
                                             is_corridor, optimize_corridor_constraints, graph);
                out.overlap_penalty += p.overlap_penalty;
                out.move_distance_penalty += p.move_distance_penalty;
                out.corridor_penalty += p.corridor_penalty;
                out.minimum_distance_penalty += p.minimum_distance_penalty;
            }
        }
        return out;
    }
};

} // namespace dungeon_topology_generator::generator::grid2d
