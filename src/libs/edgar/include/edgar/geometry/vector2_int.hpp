#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <array>
#include <vector>

#include "edgar/detail/integer_math.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"

namespace edgar::geometry {

struct Vector2Int {
    int x{};
    int y{};

    constexpr Vector2Int() = default;
    constexpr Vector2Int(int px, int py) : x(px), y(py) {}

    static int manhattan_distance(Vector2Int a, Vector2Int b) {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    }

    static double euclidean_distance(Vector2Int a, Vector2Int b) {
        const double dx = static_cast<double>(a.x - b.x);
        const double dy = static_cast<double>(a.y - b.y);
        return std::sqrt(dx * dx + dy * dy);
    }

    static int max_distance(Vector2Int a, Vector2Int b) {
        return (std::max)(std::abs(a.x - b.x), std::abs(a.y - b.y));
    }

    constexpr bool operator==(Vector2Int o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(Vector2Int o) const { return !(*this == o); }

    constexpr Vector2Int operator+(Vector2Int o) const { return {x + o.x, y + o.y}; }
    constexpr Vector2Int operator-(Vector2Int o) const { return {x - o.x, y - o.y}; }
    constexpr Vector2Int operator*(int k) const { return {k * x, k * y}; }

    friend constexpr Vector2Int operator*(int k, Vector2Int v) { return v * k; }

    constexpr bool operator<(Vector2Int o) const {
        return x < o.x || (x == o.x && y < o.y);
    }
    constexpr bool operator<=(Vector2Int o) const {
        return x < o.x || (x == o.x && y <= o.y);
    }
    constexpr bool operator>(Vector2Int o) const { return !(*this <= o); }
    constexpr bool operator>=(Vector2Int o) const { return !(*this < o); }

    Vector2Int rotate_around_center(int degrees) const {
        const int s = detail::int_sin_deg(degrees);
        const int c = detail::int_cos_deg(degrees);
        return {x * c + y * s, -x * s + y * c};
    }

    Vector2Int transform(TransformationGrid2D t) const;

    constexpr int dot_product(Vector2Int o) const { return x * o.x + y * o.y; }

    constexpr Vector2Int element_wise_product(Vector2Int o) const {
        return {x * o.x, y * o.y};
    }

    std::vector<Vector2Int> adjacent_vectors() const {
        return {
            {x + 1, y},
            {x - 1, y},
            {x, y + 1},
            {x, y - 1},
        };
    }
};

inline Vector2Int Vector2Int::transform(TransformationGrid2D transformation) const {
    switch (transformation) {
        case TransformationGrid2D::Identity:
            return *this;
        case TransformationGrid2D::Rotate90:
            return rotate_around_center(90);
        case TransformationGrid2D::Rotate180:
            return rotate_around_center(180);
        case TransformationGrid2D::Rotate270:
            return rotate_around_center(270);
        case TransformationGrid2D::MirrorX:
            return {x, -y};
        case TransformationGrid2D::MirrorY:
            return {-x, y};
        case TransformationGrid2D::Diagonal13:
            return {y, x};
        case TransformationGrid2D::Diagonal24:
            return {-y, -x};
    }
    return *this;
}

} // namespace edgar::geometry

namespace std {

template <>
struct hash<edgar::geometry::Vector2Int> {
    std::size_t operator()(const edgar::geometry::Vector2Int& v) const noexcept {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(v.x)) << 32) ^
               static_cast<std::uint32_t>(v.y);
    }
};

} // namespace std
