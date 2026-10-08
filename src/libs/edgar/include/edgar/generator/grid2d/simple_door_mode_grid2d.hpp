#pragma once

#include <stdexcept>
#include <vector>

#include "edgar/generator/grid2d/door_mode_grid2d.hpp"

namespace edgar::generator::grid2d {

/// Places doors on orthogonal outlines (C# SimpleDoorModeGrid2D).
class SimpleDoorModeGrid2D : public IDoorModeGrid2D {
public:
    SimpleDoorModeGrid2D(int door_length, int corner_distance)
        : door_length_(door_length), corner_distance_(corner_distance) {
        if (corner_distance < 0) {
            throw std::invalid_argument("corner_distance must be non-negative");
        }
        if (door_length < 0) {
            throw std::invalid_argument("door_length must be non-negative");
        }
    }

    int door_length() const { return door_length_; }
    int corner_distance() const { return corner_distance_; }

    std::vector<DoorLineGrid2D> get_doors(const geometry::PolygonGrid2D& room_shape) const override;

private:
    int door_length_{};
    int corner_distance_{};
};

} // namespace edgar::generator::grid2d
