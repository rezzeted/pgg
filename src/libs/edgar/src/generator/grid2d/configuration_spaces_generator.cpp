#include "edgar/generator/grid2d/configuration_spaces_generator.hpp"
#include "edgar/generator/grid2d/door_utils.hpp"
#include "edgar/geometry/clipper2_util.hpp"
#include "edgar/geometry/orthogonal_line_intersection.hpp"
#include "edgar/geometry/orthogonal_line_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace edgar::generator::grid2d {

// ---------------------------------------------------------------------------
// Content-addressed cache for configuration spaces.
// C# precomputes configuration spaces once per (RoomTemplateInstance, RoomTemplateInstance)
// pair inside `ConfigurationSpaces`; the port used to recompute them on every placement
// attempt, which dominated generation time on dense graphs. Keys carry full geometry
// (outline points, door lines incl. direction/length/socket identity, offsets), so cached
// entries can never go stale.
// ---------------------------------------------------------------------------

namespace {

struct DoorLineKey {
    geometry::Vector2Int from;
    geometry::Vector2Int to;
    int length;
    int direction;
    const void* socket;

    bool operator==(const DoorLineKey& o) const {
        return from == o.from && to == o.to && length == o.length && direction == o.direction &&
               socket == o.socket;
    }
};

struct ConfigurationSpaceKey {
    std::vector<geometry::Vector2Int> moving_points;
    std::vector<geometry::Vector2Int> fixed_points;
    std::vector<DoorLineKey> moving_doors;
    std::vector<DoorLineKey> fixed_doors;
    std::vector<int> offsets;
    bool over_corridor;
    geometry::PolygonGrid2D corridor = geometry::PolygonGrid2D::get_rectangle(1, 1);
    std::vector<DoorLineKey> corridor_doors;

    bool operator==(const ConfigurationSpaceKey& o) const {
        return moving_points == o.moving_points && fixed_points == o.fixed_points &&
               moving_doors == o.moving_doors && fixed_doors == o.fixed_doors && offsets == o.offsets &&
               over_corridor == o.over_corridor &&
               (!over_corridor || (corridor.points() == o.corridor.points() && corridor_doors == o.corridor_doors));
    }
};

DoorLineKey make_door_key(const DoorLineGrid2D& d) {
    return DoorLineKey{d.line.from, d.line.to, d.length, static_cast<int>(d.get_direction()), d.socket.get()};
}

std::vector<DoorLineKey> make_door_keys(const std::vector<DoorLineGrid2D>& doors) {
    std::vector<DoorLineKey> out;
    out.reserve(doors.size());
    for (const auto& d : doors) {
        out.push_back(make_door_key(d));
    }
    return out;
}

struct ConfigurationSpaceKeyHash {
    std::size_t operator()(const ConfigurationSpaceKey& k) const {
        std::size_t h = 1469598103934665603ull; // FNV-1a offset basis
        const auto mix = [&h](std::size_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        const auto mix_point = [&mix](const geometry::Vector2Int& p) {
            mix(std::hash<int>{}(p.x));
            mix(std::hash<int>{}(p.y));
        };
        const auto mix_doors = [&mix, &mix_point](const std::vector<DoorLineKey>& doors) {
            for (const auto& d : doors) {
                mix_point(d.from);
                mix_point(d.to);
                mix(std::hash<int>{}(d.length));
                mix(std::hash<int>{}(d.direction));
                mix(std::hash<const void*>{}(d.socket));
            }
        };
        for (const auto& p : k.moving_points) mix_point(p);
        for (const auto& p : k.fixed_points) mix_point(p);
        mix_doors(k.moving_doors);
        mix_doors(k.fixed_doors);
        for (const int o : k.offsets) mix(std::hash<int>{}(o));
        mix(k.over_corridor ? 1 : 0);
        if (k.over_corridor) {
            for (const auto& p : k.corridor.points()) mix_point(p);
            mix_doors(k.corridor_doors);
        }
        return h;
    }
};

constexpr std::size_t kMaxCacheEntries = 200000;

std::unordered_map<ConfigurationSpaceKey, ConfigurationSpaceGrid2D, ConfigurationSpaceKeyHash>& cs_cache() {
    static thread_local std::unordered_map<ConfigurationSpaceKey, ConfigurationSpaceGrid2D, ConfigurationSpaceKeyHash>
        cache;
    return cache;
}

} // namespace

void clear_configuration_space_cache() {
    cs_cache().clear();
}

namespace {

bool door_lines_equal_unordered(const std::vector<DoorLineGrid2D>& a, const std::vector<DoorLineGrid2D>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    std::vector<bool> used(b.size(), false);
    for (const auto& da : a) {
        bool found = false;
        for (std::size_t i = 0; i < b.size(); ++i) {
            if (used[i]) {
                continue;
            }
            if (b[i].length == da.length && b[i].line.from == da.line.from && b[i].line.to == da.line.to) {
                used[i] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<RoomTemplateInstanceGrid2D> ConfigurationSpacesGenerator::get_room_template_instances(
    const RoomTemplateGrid2D& room_template) {
    std::vector<RoomTemplateInstanceGrid2D> result;
    const auto& shape = room_template.outline();
    const std::vector<DoorLineGrid2D> door_lines = room_template.doors().get_doors(shape);

    auto transformations = room_template.allowed_transformations();
    if (transformations.empty()) {
        transformations.push_back(geometry::TransformationGrid2D::Identity);
    }

    for (const auto transformation : transformations) {
        // Both the shape and doors are moved so the polygon is in the first quadrant and touches axes
        geometry::PolygonGrid2D transformed = shape.transform(transformation);
        const geometry::Vector2Int smallest = transformed.bounding_rectangle().a;
        transformed = (transformed + (-1 * smallest)).normalized();

        std::vector<DoorLineGrid2D> transformed_doors;
        transformed_doors.reserve(door_lines.size());
        for (const auto& d : door_lines) {
            const DoorLineGrid2D td = transform_door_line(d, transformation);
            transformed_doors.push_back(DoorLineGrid2D{td.line + (-1 * smallest), td.length, td.get_direction(),
                                                       td.socket});
        }

        bool found = false;
        for (auto& instance : result) {
            if (instance.outline == transformed &&
                door_lines_equal_unordered(instance.door_lines, transformed_doors)) {
                instance.transformations.push_back(transformation);
                found = true;
                break;
            }
        }
        if (!found) {
            result.push_back(RoomTemplateInstanceGrid2D{transformed, std::move(transformed_doors), {transformation}});
        }
    }
    return result;
}

static int rotation_for_direction(geometry::OrthogonalDirection direction) {
    switch (direction) {
    case geometry::OrthogonalDirection::Right:
        return 0;
    case geometry::OrthogonalDirection::Bottom:
        return 270;
    case geometry::OrthogonalDirection::Left:
        return 180;
    case geometry::OrthogonalDirection::Top:
        return 90;
    default:
        throw std::invalid_argument("ConfigurationSpacesGenerator: undefined door direction");
    }
}

ConfigurationSpaceGrid2D ConfigurationSpacesGenerator::get_configuration_space(
    const geometry::PolygonGrid2D& polygon, const std::vector<DoorLineGrid2D>& door_lines,
    const geometry::PolygonGrid2D& fixed_center, const std::vector<DoorLineGrid2D>& door_lines_fixed,
    const std::vector<int>* offsets) {
    if (offsets != nullptr && offsets->empty()) {
        throw std::invalid_argument("ConfigurationSpacesGenerator: offsets must be non-empty when set");
    }

    ConfigurationSpaceKey key{polygon.points(), fixed_center.points(), make_door_keys(door_lines),
                              make_door_keys(door_lines_fixed), offsets != nullptr ? *offsets : std::vector<int>{},
                              false, geometry::PolygonGrid2D::get_rectangle(1, 1), {}};
    auto& cache = cs_cache();
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }

    std::vector<DoorLineGrid2D> door_lines_m = merge_door_lines(door_lines);
    std::vector<DoorLineGrid2D> door_lines_f = merge_door_lines(door_lines_fixed);

    std::vector<geometry::OrthogonalLineGrid2D> configuration_space_lines;
    std::vector<std::pair<geometry::OrthogonalLineGrid2D, DoorLineGrid2D>> reverse_door;

    std::array<std::vector<DoorLineGrid2D>, 5> lines_by_dir{};
    for (const auto& d : door_lines_f) {
        lines_by_dir[static_cast<int>(d.get_direction())].push_back(d);
    }

    for (const auto& door_line : door_lines_m) {
        const geometry::OrthogonalLineGrid2D& line = door_line.line;
        const geometry::OrthogonalDirection opposite_direction = geometry::opposite_direction(door_line.get_direction());
        const int rotation = rotation_for_direction(door_line.get_direction());
        const geometry::OrthogonalLineGrid2D rotated_line = line.rotate(rotation);

        for (const auto& c_door_line_fixed : lines_by_dir[static_cast<int>(opposite_direction)]) {
            if (c_door_line_fixed.length != door_line.length || c_door_line_fixed.socket != door_line.socket) {
                continue;
            }
            const geometry::OrthogonalLineGrid2D cline = c_door_line_fixed.line.rotate(rotation);
            const int y = cline.from.y - rotated_line.from.y;
            const geometry::Vector2Int from(cline.from.x - rotated_line.to.x + (rotated_line.length() - door_line.length), y);
            const geometry::Vector2Int to(cline.to.x - rotated_line.from.x - (rotated_line.length() + door_line.length), y);

            if (from.x < to.x) {
                continue;
            }

            if (offsets == nullptr) {
                geometry::OrthogonalLineGrid2D result_line(from, to, geometry::OrthogonalDirection::Left);
                result_line = result_line.rotate(-rotation);
                reverse_door.emplace_back(result_line,
                    DoorLineGrid2D{
                        .line = cline.rotate(-rotation),
                        .length = c_door_line_fixed.length,
                        .direction = c_door_line_fixed.get_direction(),
                        .socket = c_door_line_fixed.socket});
                configuration_space_lines.push_back(result_line);
            } else {
                for (int offset : *offsets) {
                    const geometry::Vector2Int offset_vector{0, offset};
                    geometry::OrthogonalLineGrid2D result_line(from - offset_vector, to - offset_vector,
                                                               geometry::OrthogonalDirection::Left);
                    result_line = result_line.rotate(-rotation);
                    reverse_door.emplace_back(result_line,
                        DoorLineGrid2D{
                            .line = cline.rotate(-rotation),
                            .length = c_door_line_fixed.length,
                            .direction = c_door_line_fixed.get_direction(),
                            .socket = c_door_line_fixed.socket});
                    configuration_space_lines.push_back(result_line);
                }
            }
        }
    }

    configuration_space_lines =
        geometry::remove_overlapping_along_lines(polygon, fixed_center, configuration_space_lines);

    configuration_space_lines = geometry::OrthogonalLineIntersection::remove_intersections(configuration_space_lines);

    ConfigurationSpaceGrid2D result{std::move(configuration_space_lines), std::move(reverse_door)};
    if (cache.size() < kMaxCacheEntries) {
        cache.emplace(std::move(key), result);
    }
    return result;
}

ConfigurationSpaceGrid2D ConfigurationSpacesGenerator::get_configuration_space_over_corridor(
    const geometry::PolygonGrid2D& polygon, const std::vector<DoorLineGrid2D>& door_lines,
    const geometry::PolygonGrid2D& fixed_polygon, const std::vector<DoorLineGrid2D>& fixed_door_lines,
    const geometry::PolygonGrid2D& corridor, const std::vector<DoorLineGrid2D>& corridor_door_lines) {
    ConfigurationSpaceKey key{polygon.points(), fixed_polygon.points(), make_door_keys(door_lines),
                              make_door_keys(fixed_door_lines), {}, true, corridor,
                              make_door_keys(corridor_door_lines)};
    auto& cache = cs_cache();
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }

    const ConfigurationSpaceGrid2D fixed_and_corridor_cs =
        get_configuration_space(corridor, corridor_door_lines, fixed_polygon, fixed_door_lines);

    std::vector<DoorLineGrid2D> merged_corridor_doors = merge_door_lines(corridor_door_lines);
    std::vector<DoorLineGrid2D> new_corridor_door_lines;

    for (const auto& corridor_position_line : fixed_and_corridor_cs.lines) {
        for (const auto& corridor_door_line : merged_corridor_doors) {
            const int rotation = rotation_for_direction(corridor_door_line.get_direction());
            const geometry::OrthogonalLineGrid2D rotated_line = corridor_door_line.line.rotate(rotation);
            const geometry::OrthogonalLineGrid2D rotated_corridor_line =
                corridor_position_line.rotate(rotation).normalized();

            if (rotated_corridor_line.get_direction() == geometry::OrthogonalDirection::Right) {
                const geometry::OrthogonalLineGrid2D correct_position_line =
                    rotated_corridor_line + rotated_line.from;
                const geometry::Vector2Int dv = rotated_line.direction_vector();
                const int len = rotated_line.length();
                const geometry::Vector2Int to_ext{
                    correct_position_line.to.x + dv.x * len,
                    correct_position_line.to.y + dv.y * len,
                };
                const geometry::OrthogonalLineGrid2D correct_length_line(correct_position_line.from, to_ext,
                                                                         rotated_corridor_line.get_direction());
                new_corridor_door_lines.push_back(
                    DoorLineGrid2D{
                        .line = correct_length_line.rotate(-rotation),
                        .length = corridor_door_line.length,
                        .direction = corridor_door_line.get_direction(),
                        .socket = corridor_door_line.socket});
            } else if (rotated_corridor_line.get_direction() == geometry::OrthogonalDirection::Top) {
                for (const auto& corridor_position : rotated_corridor_line.grid_points_inclusive()) {
                    const geometry::OrthogonalLineGrid2D transformed_door_line = rotated_line + corridor_position;
                    new_corridor_door_lines.push_back(DoorLineGrid2D{
                        .line = transformed_door_line.rotate(-rotation),
                        .length = corridor_door_line.length,
                        .direction = corridor_door_line.get_direction(),
                        .socket = corridor_door_line.socket});
                }
            }
        }
    }

    auto result = get_configuration_space(polygon, door_lines, fixed_polygon, new_corridor_door_lines);
    if (cache.size() < kMaxCacheEntries) {
        cache.emplace(std::move(key), result);
    }
    return result;
}

ConfigurationSpaceGrid2D ConfigurationSpacesGenerator::get_configuration_space_over_corridors(
    const geometry::PolygonGrid2D& polygon, const std::vector<DoorLineGrid2D>& door_lines,
    const geometry::PolygonGrid2D& fixed_polygon, const std::vector<DoorLineGrid2D>& fixed_door_lines,
    const std::vector<std::pair<geometry::PolygonGrid2D, std::vector<DoorLineGrid2D>>>& corridors) {
    std::vector<geometry::OrthogonalLineGrid2D> configuration_space_lines;

    for (const auto& corridor_entry : corridors) {
        const ConfigurationSpaceGrid2D cs = get_configuration_space_over_corridor(
            polygon, door_lines, fixed_polygon, fixed_door_lines, corridor_entry.first, corridor_entry.second);
        configuration_space_lines.insert(configuration_space_lines.end(), cs.lines.begin(), cs.lines.end());
    }

    configuration_space_lines = geometry::OrthogonalLineIntersection::remove_intersections(configuration_space_lines);

    return ConfigurationSpaceGrid2D{std::move(configuration_space_lines), {}};
}

} // namespace edgar::generator::grid2d
