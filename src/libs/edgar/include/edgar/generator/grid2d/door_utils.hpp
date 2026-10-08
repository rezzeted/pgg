#pragma once

// Derived from Edgar-DotNet `DoorUtils.MergeDoorLines` (MIT).

#include "edgar/generator/grid2d/door_line_grid2d.hpp"
#include "edgar/geometry/orthogonal_line_grid2d.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

namespace edgar::generator::grid2d {

inline geometry::Vector2Int direction_unit(geometry::OrthogonalDirection direction) {
    switch (direction) {
    case geometry::OrthogonalDirection::Top:
        return {0, 1};
    case geometry::OrthogonalDirection::Right:
        return {1, 0};
    case geometry::OrthogonalDirection::Bottom:
        return {0, -1};
    case geometry::OrthogonalDirection::Left:
        return {-1, 0};
    default:
        throw std::invalid_argument("merge_door_lines: undefined door direction");
    }
}

inline std::vector<DoorLineGrid2D> merge_door_lines(std::vector<DoorLineGrid2D> door_lines) {
    std::map<geometry::OrthogonalDirection, std::vector<DoorLineGrid2D>> by_dir;
    for (auto& d : door_lines) {
        by_dir[d.get_direction()].push_back(std::move(d));
    }

    std::vector<DoorLineGrid2D> result;

    for (auto& group : by_dir) {
        if (group.first == geometry::OrthogonalDirection::Undefined) {
            throw std::invalid_argument("merge_door_lines: undefined door direction");
        }
        std::vector<DoorLineGrid2D> same = std::move(group.second);
        while (!same.empty()) {
            DoorLineGrid2D door_line = std::move(same.back());
            same.pop_back();
            bool found = true;
            while (found) {
                found = false;
                for (auto it = same.begin(); it != same.end();) {
                    if (it->length != door_line.length || it->socket != door_line.socket) {
                        ++it;
                        continue;
                    }
                    const geometry::Vector2Int dv = direction_unit(door_line.get_direction());
                    if (door_line.line.to + dv == it->line.from) {
                        door_line.line = geometry::OrthogonalLineGrid2D(door_line.line.from, it->line.to);
                        it = same.erase(it);
                        found = true;
                    } else if (door_line.line.from - dv == it->line.to) {
                        door_line.line = geometry::OrthogonalLineGrid2D(it->line.from, door_line.line.to);
                        it = same.erase(it);
                        found = true;
                    } else {
                        ++it;
                    }
                }
            }
            result.push_back(std::move(door_line));
        }
    }
    return result;
}

inline geometry::OrthogonalDirection transform_direction(geometry::OrthogonalDirection direction,
                                                         geometry::TransformationGrid2D transformation) {
    using geometry::OrthogonalDirection;
    const bool horizontal =
        direction == OrthogonalDirection::Left || direction == OrthogonalDirection::Right;
    switch (transformation) {
    case geometry::TransformationGrid2D::MirrorX:
        return horizontal ? direction : geometry::opposite_direction(direction);
    case geometry::TransformationGrid2D::MirrorY:
        return horizontal ? geometry::opposite_direction(direction) : direction;
    case geometry::TransformationGrid2D::Diagonal13:
        switch (direction) {
        case OrthogonalDirection::Top:
            return OrthogonalDirection::Right;
        case OrthogonalDirection::Right:
            return OrthogonalDirection::Top;
        case OrthogonalDirection::Bottom:
            return OrthogonalDirection::Left;
        case OrthogonalDirection::Left:
            return OrthogonalDirection::Bottom;
        default:
            break;
        }
        break;
    case geometry::TransformationGrid2D::Diagonal24:
        switch (direction) {
        case OrthogonalDirection::Top:
            return OrthogonalDirection::Left;
        case OrthogonalDirection::Right:
            return OrthogonalDirection::Bottom;
        case OrthogonalDirection::Bottom:
            return OrthogonalDirection::Right;
        case OrthogonalDirection::Left:
            return OrthogonalDirection::Top;
        default:
            break;
        }
        break;
    default:
        break;
    }
    throw std::invalid_argument("transform_direction: unsupported transformation");
}

/// Port of C# `DoorUtils.TransformDoorLine` (Grid2D variant).
inline DoorLineGrid2D transform_door_line(const DoorLineGrid2D& door_line,
                                          geometry::TransformationGrid2D transformation) {
    using geometry::OrthogonalLineGrid2D;
    using geometry::TransformationGrid2D;
    const auto& pos = door_line.line;
    // Direction comes from the door line itself (manual doors are point lines whose
    // direction lives in DoorLineGrid2D::direction, not derivable from endpoints)
    const auto direction = door_line.get_direction();
    if (direction == geometry::OrthogonalDirection::Undefined) {
        throw std::invalid_argument("transform_door_line: undefined door direction");
    }

    switch (transformation) {
    case TransformationGrid2D::Identity:
        return door_line;
    case TransformationGrid2D::Rotate90:
        return DoorLineGrid2D{pos.rotate(90), door_line.length,
                              geometry::rotate_direction(direction, 90), door_line.socket};
    case TransformationGrid2D::Rotate180:
        return DoorLineGrid2D{pos.rotate(180), door_line.length,
                              geometry::rotate_direction(direction, 180), door_line.socket};
    case TransformationGrid2D::Rotate270:
        return DoorLineGrid2D{pos.rotate(270), door_line.length,
                              geometry::rotate_direction(direction, 270), door_line.socket};
    default:
        break;
    }

    // Mirror/diagonal transformations need to switch door directions (C# logic)
    const auto first = pos.from.transform(transformation);
    const auto last = pos.to.transform(transformation);
    const auto transformed_direction = transform_direction(direction, transformation);
    const OrthogonalLineGrid2D transformed_line(first, last, transformed_direction);
    const auto last_end = last + door_line.length * transformed_line.direction_vector();
    const auto new_direction = geometry::opposite_direction(transformed_direction);
    const OrthogonalLineGrid2D new_position(
        last_end, last_end + transformed_line.length() * transformed_line.switch_orientation().direction_vector());
    if (new_position.length() != pos.length()) {
        throw std::invalid_argument("transform_door_line: transformed length mismatch");
    }
    return DoorLineGrid2D{new_position, door_line.length, new_direction, door_line.socket};
}

} // namespace edgar::generator::grid2d
