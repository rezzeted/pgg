#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "edgar/generator/common/room_template_repeat_mode.hpp"
#include "edgar/generator/grid2d/room_description_grid2d.hpp"
#include "edgar/graphs/undirected_graph.hpp"

namespace edgar::generator::grid2d {

template <typename TRoom>
class LevelDescriptionGrid2D {
public:
    std::string name;
    int minimum_room_distance = 0;
    bool optimize_corridor_constraints = true;
    /// C# `LevelDescriptionGrid2D.RoomTemplateRepeatModeDefault` defaults to NoRepeat.
    std::optional<RoomTemplateRepeatMode> room_template_repeat_mode_default = RoomTemplateRepeatMode::NoRepeat;
    std::optional<RoomTemplateRepeatMode> room_template_repeat_mode_override;

    void add_room(const TRoom& room, const RoomDescriptionGrid2D& room_description) {
        if (rooms_.count(room)) {
            throw std::invalid_argument("Map description already contains this room");
        }
        rooms_.emplace(room, room_description);
    }

    void add_connection(const TRoom& room1, const TRoom& room2) {
        if (!rooms_.count(room1)) {
            throw std::invalid_argument("Map description does not contain room1");
        }
        if (!rooms_.count(room2)) {
            throw std::invalid_argument("Map description does not contain room2");
        }
        const Passage p{room1, room2};
        for (const auto& e : passages_) {
            if (e == p) {
                throw std::invalid_argument("Map description already contains given connection");
            }
        }
        passages_.push_back(p);
    }

    const std::map<TRoom, RoomDescriptionGrid2D>& rooms() const { return rooms_; }

    const RoomDescriptionGrid2D& get_room_description(const TRoom& room) const { return rooms_.at(room); }

    graphs::UndirectedAdjacencyListGraph<TRoom> get_graph() const {
        graphs::UndirectedAdjacencyListGraph<TRoom> graph;
        for (const auto& kv : rooms_) {
            graph.add_vertex(kv.first);
        }
        for (const auto& passage : passages_) {
            graph.add_edge(passage.a, passage.b);
        }
        check_if_valid(graph);
        validate_stage_two_connectivity(graph);
        return graph;
    }

    /// Subgraph induced by rooms with `stage() == 1`. Used by two-stage chain decomposition.
    /// Matches C# `MapDescription.GetStageOneGraph`: direct edges between two stage-one rooms,
    /// plus edges contracted through stage-two corridor rooms (a corridor with exactly two
    /// stage-one neighbors contributes an edge between them).
    graphs::UndirectedAdjacencyListGraph<TRoom> get_stage_one_graph() const {
        const auto full = get_graph();
        graphs::UndirectedAdjacencyListGraph<TRoom> graph;
        for (const auto& kv : rooms_) {
            if (kv.second.stage() == 1) {
                graph.add_vertex(kv.first);
            }
        }
        for (const auto& kv : rooms_) {
            const auto& desc = kv.second;
            if (!desc.is_corridor() || desc.stage() != 2) {
                continue;
            }
            const auto neigh = full.neighbours(kv.first);
            if (neigh.size() == 2 && get_room_description(neigh[0]).stage() == 1 &&
                get_room_description(neigh[1]).stage() == 1) {
                graph.add_edge(neigh[0], neigh[1]);
            }
        }
        for (const auto& passage : passages_) {
            const int sa = get_room_description(passage.a).stage();
            const int sb = get_room_description(passage.b).stage();
            if (sa == 1 && sb == 1) {
                graph.add_edge(passage.a, passage.b);
            }
        }
        check_if_valid_stage_one_subgraph(graph);
        return graph;
    }

private:
    struct Passage {
        TRoom a;
        TRoom b;
        bool operator==(const Passage& o) const {
            return (a == o.a && b == o.b) || (a == o.b && b == o.a);
        }
    };

    void check_if_valid(const graphs::UndirectedAdjacencyListGraph<TRoom>& graph) const {
        for (const auto& kv : rooms_) {
            const auto& room = kv.first;
            const auto& desc = kv.second;
            if (!desc.is_corridor()) {
                continue;
            }
            if (!graph_has_vertex(graph, room)) {
                continue;
            }
            const auto neigh = graph.neighbours(room);
            if (neigh.size() != 2) {
                throw std::invalid_argument("Each corridor must have exactly 2 neighbors");
            }
            for (const auto& n : neigh) {
                if (get_room_description(n).is_corridor()) {
                    throw std::invalid_argument("Corridor connected to another corridor");
                }
            }
        }
    }

    static bool graph_has_vertex(const graphs::UndirectedAdjacencyListGraph<TRoom>& g, const TRoom& v) {
        for (const auto& x : g.vertices()) {
            if (x == v) {
                return true;
            }
        }
        return false;
    }

    /// Each stage-two room must share an edge with at least one stage-one room (so it can be merged in two-stage decomposition).
    /// Corridors in the stage-one subgraph must still have degree 2 among stage-one edges only.
    void check_if_valid_stage_one_subgraph(const graphs::UndirectedAdjacencyListGraph<TRoom>& graph) const {
        for (const auto& kv : rooms_) {
            const auto& room = kv.first;
            const auto& desc = kv.second;
            if (desc.stage() != 1 || !desc.is_corridor()) {
                continue;
            }
            if (!graph_has_vertex(graph, room)) {
                continue;
            }
            const auto neigh = graph.neighbours(room);
            if (neigh.size() != 2) {
                throw std::invalid_argument(
                    "Stage-one corridor must have two stage-one neighbors in get_stage_one_graph(); "
                    "avoid corridors that only connect to stage-two rooms in the stage-one subgraph");
            }
            for (const auto& n : neigh) {
                if (get_room_description(n).is_corridor()) {
                    throw std::invalid_argument("Corridor connected to another corridor");
                }
            }
        }
    }

    void validate_stage_two_connectivity(const graphs::UndirectedAdjacencyListGraph<TRoom>& graph) const {
        for (const auto& kv : rooms_) {
            if (kv.second.stage() != 2) {
                continue;
            }
            const TRoom& room = kv.first;
            bool ok = false;
            for (const TRoom& n : graph.neighbours(room)) {
                if (get_room_description(n).stage() == 1) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                throw std::invalid_argument("Stage-two room must neighbor at least one stage-one room");
            }
        }
    }

    std::map<TRoom, RoomDescriptionGrid2D> rooms_;
    std::vector<Passage> passages_;
};

} // namespace edgar::generator::grid2d
