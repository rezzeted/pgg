#include "edgar/geometry/orthogonal_line_grid2d.hpp"

#include <algorithm>

namespace edgar::geometry {

OrthogonalLineGrid2D::OrthogonalLineGrid2D(Vector2Int f, Vector2Int t) : from(f), to(t) {
    if (f.x != t.x && f.y != t.y) {
        throw std::invalid_argument("OrthogonalLineGrid2D: not axis-aligned");
    }
}

OrthogonalLineGrid2D::OrthogonalLineGrid2D(Vector2Int f, Vector2Int t, OrthogonalDirection degenerated_dir)
    : from(f), to(t), degenerated_direction(degenerated_dir) {
    if (f.x != t.x && f.y != t.y) {
        throw std::invalid_argument("OrthogonalLineGrid2D: not axis-aligned");
    }
    if (f != t && degenerated_dir != OrthogonalDirection::Undefined) {
        if (degenerated_dir != get_direction()) {
            throw std::invalid_argument("OrthogonalLineGrid2D: given direction is wrong");
        }
    }
}

Vector2Int OrthogonalLineGrid2D::direction_vector() const {
    switch (get_direction()) {
    case OrthogonalDirection::Right:
        return {1, 0};
    case OrthogonalDirection::Left:
        return {-1, 0};
    case OrthogonalDirection::Top:
        return {0, 1};
    case OrthogonalDirection::Bottom:
        return {0, -1};
    default:
        throw std::invalid_argument("OrthogonalLineGrid2D::direction_vector: degenerate line");
    }
}

std::vector<Vector2Int> OrthogonalLineGrid2D::grid_points_inclusive() const {
    std::vector<Vector2Int> out;
    Vector2Int cur = from;
    out.push_back(cur);
    while (cur != to) {
        if (cur.x < to.x) {
            ++cur.x;
        } else if (cur.x > to.x) {
            --cur.x;
        } else if (cur.y < to.y) {
            ++cur.y;
        } else if (cur.y > to.y) {
            --cur.y;
        }
        out.push_back(cur);
    }
    return out;
}

bool OrthogonalLineGrid2D::contains_point(Vector2Int p) const {
    if (from.x == to.x) {
        return p.x == from.x && p.y >= std::min(from.y, to.y) && p.y <= std::max(from.y, to.y);
    }
    if (from.y == to.y) {
        return p.y == from.y && p.x >= std::min(from.x, to.x) && p.x <= std::max(from.x, to.x);
    }
    return false;
}

int OrthogonalLineGrid2D::index_of_point(Vector2Int p) const {
    if (!contains_point(p)) {
        return -1;
    }
    return Vector2Int::manhattan_distance(from, p);
}

OrthogonalDirection OrthogonalLineGrid2D::get_direction() const {
    if (from.x == to.x && from.y == to.y) {
        return degenerated_direction;
    }
    if (from.x == to.x) {
        return from.y > to.y ? OrthogonalDirection::Bottom : OrthogonalDirection::Top;
    }
    if (from.y == to.y) {
        return from.x > to.x ? OrthogonalDirection::Left : OrthogonalDirection::Right;
    }
    throw std::invalid_argument("OrthogonalLineGrid2D::get_direction: not orthogonal");
}

OrthogonalLineGrid2D OrthogonalLineGrid2D::rotate(int degrees_clockwise) const {
    // C# Rotate(degrees, checkDirection: true) also rotates the stored direction
    const OrthogonalDirection dir = degenerated_direction == OrthogonalDirection::Undefined
                                        ? OrthogonalDirection::Undefined
                                        : rotate_direction(degenerated_direction, degrees_clockwise);
    return OrthogonalLineGrid2D(from.rotate_around_center(degrees_clockwise),
                                to.rotate_around_center(degrees_clockwise), dir);
}

OrthogonalLineGrid2D OrthogonalLineGrid2D::normalized() const {
    if (from.x == to.x && from.y == to.y) {
        return *this;
    }
    if (from.y == to.y) {
        return from.x <= to.x ? *this : OrthogonalLineGrid2D(to, from);
    }
    if (from.x == to.x) {
        return from.y <= to.y ? *this : OrthogonalLineGrid2D(to, from);
    }
    throw std::invalid_argument("OrthogonalLineGrid2D::normalized: not orthogonal");
}

OrthogonalLineGrid2D OrthogonalLineGrid2D::rotate(int degrees_clockwise, bool normalize_after) const {
    OrthogonalLineGrid2D r = rotate(degrees_clockwise);
    return normalize_after ? r.normalized() : r;
}

int OrthogonalLineGrid2D::compute_rotation_clockwise_degrees() const { return compute_rotation_to_right(); }

int OrthogonalLineGrid2D::compute_rotation_to_right() const {
    switch (get_direction()) {
        case OrthogonalDirection::Right:
            return 0;
        case OrthogonalDirection::Bottom:
            return 270;
        case OrthogonalDirection::Left:
            return 180;
        case OrthogonalDirection::Top:
            return 90;
        default:
            throw std::invalid_argument("OrthogonalLineGrid2D::compute_rotation_to_right: degenerate line");
    }
}

OrthogonalDirection opposite_direction(OrthogonalDirection d) {
    switch (d) {
    case OrthogonalDirection::Top:
        return OrthogonalDirection::Bottom;
    case OrthogonalDirection::Bottom:
        return OrthogonalDirection::Top;
    case OrthogonalDirection::Left:
        return OrthogonalDirection::Right;
    case OrthogonalDirection::Right:
        return OrthogonalDirection::Left;
    default:
        throw std::invalid_argument("opposite_direction: undefined");
    }
}

OrthogonalLineGrid2D OrthogonalLineGrid2D::shrink(int from_amount, int to_amount) const {
    if (length() - from_amount - to_amount < 0) {
        throw std::invalid_argument("OrthogonalLineGrid2D::shrink: not enough length after shrink");
    }
    const int rot = compute_rotation_to_right();
    const OrthogonalLineGrid2D rotated = rotate(rot);
    const Vector2Int moved_from{rotated.from.x + from_amount, rotated.from.y};
    const Vector2Int moved_to{rotated.to.x - to_amount, rotated.to.y};
    return OrthogonalLineGrid2D(moved_from, moved_to, rotate_direction(get_direction(), rot)).rotate(-rot);
}

OrthogonalDirection rotate_direction(OrthogonalDirection d, int degrees_clockwise) {
    if (d == OrthogonalDirection::Undefined) {
        throw std::invalid_argument("rotate_direction: undefined direction");
    }
    const int base = static_cast<int>(d) - 1; // Top=0, Right=1, Bottom=2, Left=3
    const int steps = ((degrees_clockwise / 90) % 4 + 4) % 4;
    const int result = ((base + steps) % 4) + 1;
    return static_cast<OrthogonalDirection>(result);
}

} // namespace edgar::geometry
