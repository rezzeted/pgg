#pragma once

#include <algorithm>
#include <stdexcept>

#include "edgar/geometry/vector2_int.hpp"

namespace edgar::geometry {

struct RectangleGrid2D {
    Vector2Int a{}; // bottom-left
    Vector2Int b{}; // top-right

    RectangleGrid2D() = default;

    RectangleGrid2D(Vector2Int p1, Vector2Int p2) {
        if (p1.x == p2.x || p1.y == p2.y) {
            throw std::invalid_argument("RectangleGrid2D: degenerate axis-aligned box");
        }
        a = {std::min(p1.x, p2.x), std::min(p1.y, p2.y)};
        b = {std::max(p1.x, p2.x), std::max(p1.y, p2.y)};
    }

    Vector2Int center() const {
        return {(a.x + b.x) / 2, (a.y + b.y) / 2};
    }

    int area() const { return width() * height(); }

    int width() const { return std::abs(a.x - b.x); }
    int height() const { return std::abs(a.y - b.y); }

    RectangleGrid2D rotate(int degrees) const {
        if (degrees % 90 != 0) {
            throw std::invalid_argument("degrees must be multiple of 90");
        }
        return RectangleGrid2D(a.rotate_around_center(degrees), b.rotate_around_center(degrees));
    }

    friend RectangleGrid2D operator+(RectangleGrid2D r, Vector2Int offset) {
        return RectangleGrid2D(r.a + offset, r.b + offset);
    }
};

} // namespace edgar::geometry
