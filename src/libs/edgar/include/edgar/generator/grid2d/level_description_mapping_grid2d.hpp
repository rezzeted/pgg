#pragma once

#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/generator/grid2d/room_template_grid2d.hpp"
#include "edgar/graphs/undirected_graph.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace edgar::generator::grid2d {

/// Mapping layer similar to C# LevelDescriptionMapping: room id <-> contiguous index and indexed graph access.
template <typename TRoom>
class LevelDescriptionMappingGrid2D {
public:
    std::vector<TRoom> index_to_room;
    std::map<TRoom, int> room_to_index;

    explicit LevelDescriptionMappingGrid2D(const LevelDescriptionGrid2D<TRoom>& level) {
        for (const auto& kv : level.rooms()) {
            index_to_room.push_back(kv.first);
        }
        std::sort(index_to_room.begin(), index_to_room.end());
        for (int i = 0; i < static_cast<int>(index_to_room.size()); ++i) {
            room_to_index[index_to_room[static_cast<std::size_t>(i)]] = i;
        }
    }

    int room_index(const TRoom& room) const { return room_to_index.at(room); }
    const TRoom& room_id(int index) const { return index_to_room[static_cast<std::size_t>(index)]; }

    const RoomDescriptionGrid2D& room_description(const LevelDescriptionGrid2D<TRoom>& level, int index) const {
        return level.get_room_description(room_id(index));
    }

    const std::vector<RoomTemplateGrid2D>& room_templates(const LevelDescriptionGrid2D<TRoom>& level, int index) const {
        return room_description(level, index).room_templates();
    }

    graphs::UndirectedAdjacencyListGraph<int> int_graph(const LevelDescriptionGrid2D<TRoom>& level) const {
        graphs::UndirectedAdjacencyListGraph<int> g;
        const auto ug = level.get_graph();
        for (int i = 0; i < static_cast<int>(index_to_room.size()); ++i) {
            g.add_vertex(i);
        }
        for (const auto& kv : room_to_index) {
            const TRoom& v = kv.first;
            const int vi = kv.second;
            for (const TRoom& n : ug.neighbours(v)) {
                const int ni = room_to_index.at(n);
                if (vi < ni) {
                    g.add_edge(vi, ni);
                }
            }
        }
        return g;
    }
};

} // namespace edgar::generator::grid2d
