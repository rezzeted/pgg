#include "edgar/generator/grid2d/manual_door_mode_grid2d.hpp"

#include <set>
#include <stdexcept>
#include <tuple>

#include "edgar/geometry/orthogonal_line_grid2d.hpp"

namespace edgar::generator::grid2d {

std::vector<DoorLineGrid2D> ManualDoorModeGrid2D::get_doors(const geometry::PolygonGrid2D& room_shape) const {
    std::vector<DoorLineGrid2D> result;
    std::set<std::tuple<int, int, int, int, const void*>> seen;
    for (const auto& door : doors_) {
        const auto key = std::make_tuple(door.from.x, door.from.y, door.to.x, door.to.y, door.socket.get());
        if (!seen.insert(key).second) {
            throw std::invalid_argument("ManualDoorModeGrid2D: all door positions must be unique");
        }
    }

    for (const auto& door : doors_) {
        const geometry::OrthogonalLineGrid2D door_line(door.from, door.to);
        bool found = false;
        for (const auto& side : room_shape.get_lines()) {
            if (side.index_of_point(door_line.from) == -1 || side.index_of_point(door_line.to) == -1) {
                continue;
            }

            const bool is_good_direction =
                (door_line.from + side.direction_vector() * door_line.length()) == door_line.to;
            const geometry::Vector2Int from = is_good_direction ? door_line.from : door_line.to;
            found = true;
            // C# ManualDoorModeGrid2D yields a degenerate point line (from == to) carrying the
            // side's direction; the door length lives in DoorLineGrid2D::length.
            const auto side_direction = side.get_direction();
            result.push_back(DoorLineGrid2D{
                .line = geometry::OrthogonalLineGrid2D(from, from, side_direction),
                .length = door_line.length(),
                .direction = side_direction,
                .socket = door.socket});
        }

        if (!found) {
            throw std::invalid_argument("ManualDoorModeGrid2D: door line is not on room outline");
        }
    }

    return result;
}

} // namespace edgar::generator::grid2d
