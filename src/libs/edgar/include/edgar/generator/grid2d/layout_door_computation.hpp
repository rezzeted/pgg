#pragma once

#include "edgar/generator/grid2d/configuration_spaces_generator.hpp"
#include "edgar/generator/grid2d/layout_grid2d.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/geometry/orthogonal_line_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <algorithm>
#include <random>
#include <unordered_set>
#include <vector>

namespace edgar::generator::grid2d {

namespace detail {

inline std::vector<geometry::OrthogonalLineGrid2D> door_choices_from_config_space(
    geometry::Vector2Int position, const ConfigurationSpaceGrid2D& cs) {
    std::vector<geometry::OrthogonalLineGrid2D> doors;
    for (const auto& [line, door_line] : cs.reverse_doors) {
        const int index = line.index_of_point(position);
        if (index == -1) {
            continue;
        }
        const int offset = line.length() - door_line.line.length();
        const int max_start = std::min({offset, index, line.length() - index});
        const int count = std::min(max_start, door_line.line.length()) + 1;
        for (int i = 0; i < count; ++i) {
            const auto door_start = door_line.line.nth_point(std::max(0, index - offset) + i);
            const auto dv = door_line.line.direction_vector();
            const auto door_end = door_start + dv * door_line.length;
            doors.emplace_back(door_start, door_end);
        }
    }
    return doors;
}

} // namespace detail

template <typename TRoom>
void compute_layout_doors(LayoutGrid2D<TRoom>& layout, const LevelDescriptionGrid2D<TRoom>& level,
                          const graphs::UndirectedAdjacencyListGraph<TRoom>& graph,
                          std::mt19937& rng) {
    ConfigurationSpacesGenerator cs_gen;

    std::unordered_set<TRoom> processed;
    for (std::size_t ai = 0; ai < layout.rooms.size(); ++ai) {
        auto& room_a = layout.rooms[ai];
        for (const auto& neighbour : graph.neighbours(room_a.room)) {
            if (processed.count(neighbour)) {
                continue;
            }
            std::size_t bi = 0;
            for (; bi < layout.rooms.size(); ++bi) {
                if (layout.rooms[bi].room == neighbour) {
                    break;
                }
            }
            if (bi == layout.rooms.size()) {
                continue;
            }
            auto& room_b = layout.rooms[bi];

            auto doors_a = room_a.room_template.doors().get_doors(room_a.outline);
            auto doors_b = room_b.room_template.doors().get_doors(room_b.outline);

            // Match Edgar-DotNet `BasicLayoutConverterGrid2D.GetDoors(configuration1, configuration2)`:
            // CS = GetConfigurationSpace(configuration2, configuration1) -> moving = neighbour, fixed = vertex.
            // position = configuration2.Position - configuration1.Position; door lines then += configuration1.Position.
            const auto cs = cs_gen.get_configuration_space(
                room_b.outline, doors_b, room_a.outline, doors_a);
            const geometry::Vector2Int position = room_b.position - room_a.position;
            auto choices = detail::door_choices_from_config_space(position, cs);
            for (auto& c : choices) {
                c = c + room_a.position;
            }

            if (!choices.empty()) {
                std::uniform_int_distribution<int> pick(0, static_cast<int>(choices.size() - 1));
                const auto& chosen = choices[static_cast<std::size_t>(pick(rng))];

                room_a.doors.push_back(LayoutDoorGrid2D<TRoom>{
                    room_a.room, room_b.room, chosen + (geometry::Vector2Int{0, 0} - room_a.position)});
                room_b.doors.push_back(LayoutDoorGrid2D<TRoom>{
                    room_b.room, room_a.room, chosen + (geometry::Vector2Int{0, 0} - room_b.position)});
            }
        }
        processed.insert(room_a.room);
    }
}

} // namespace edgar::generator::grid2d
