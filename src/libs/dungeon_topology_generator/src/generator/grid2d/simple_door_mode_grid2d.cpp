#include "dungeon_topology_generator/generator/grid2d/simple_door_mode_grid2d.hpp"

#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"

#include <unordered_map>
#include <vector>

namespace dungeon_topology_generator::generator::grid2d {

namespace {

// Door lines are a pure function of (door length, corner distance, outline): cache them —
// this was the hottest function in SA perturbation (rebuilt for every room on every trial).
struct SimpleDoorsKey {
    int door_length;
    int corner_distance;
    std::vector<geometry::Vector2Int> points;

    bool operator==(const SimpleDoorsKey& o) const {
        return door_length == o.door_length && corner_distance == o.corner_distance && points == o.points;
    }
};

struct SimpleDoorsKeyHash {
    std::size_t operator()(const SimpleDoorsKey& k) const noexcept {
        std::size_t h = 1469598103934665603ull;
        const auto mix = [&h](std::size_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<std::size_t>(k.door_length));
        mix(static_cast<std::size_t>(k.corner_distance));
        for (const auto& p : k.points) {
            mix(std::hash<geometry::Vector2Int>{}(p));
        }
        return h;
    }
};

} // namespace

std::vector<DoorLineGrid2D> SimpleDoorModeGrid2D::get_doors(const geometry::PolygonGrid2D& room_shape) const {
    static thread_local std::unordered_map<SimpleDoorsKey, std::vector<DoorLineGrid2D>, SimpleDoorsKeyHash> cache;
    const SimpleDoorsKey key{door_length_, corner_distance_, room_shape.points()};
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }

    std::vector<DoorLineGrid2D> doors;
    for (const auto& line : room_shape.get_lines()) {
        if (line.length() < 2 * corner_distance_ + door_length_) {
            continue;
        }
        const geometry::OrthogonalLineGrid2D socket_line =
            line.shrink(corner_distance_, corner_distance_ + door_length_);
        doors.push_back(DoorLineGrid2D{
            .line = socket_line,
            .length = door_length_,
            .direction = line.get_direction()});
    }
    return cache.emplace(std::move(key), std::move(doors)).first->second;
}

} // namespace dungeon_topology_generator::generator::grid2d
