#include "dungeon_topology_generator/geometry/polygon_overlap_grid2d.hpp"
#include "dungeon_topology_generator/geometry/grid_polygon_partitioning.hpp"
#include "dungeon_topology_generator/geometry/overlap.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dungeon_topology_generator::geometry {

bool rectangles_overlap_open(const RectangleGrid2D& a, const RectangleGrid2D& b) {
    return a.a.x < b.b.x && a.b.x > b.a.x && a.a.y < b.b.y && a.b.y > b.a.y;
}

static std::vector<RectangleGrid2D> safe_partition(const PolygonGrid2D& poly) {
    try {
        return partition_orthogonal_polygon_to_rectangles(poly);
    } catch (...) {
        return {};
    }
}

namespace {

struct PointsHash {
    std::size_t operator()(const std::vector<Vector2Int>& pts) const noexcept {
        std::size_t h = 1469598103934665603ull;
        for (const auto& p : pts) {
            h ^= std::hash<Vector2Int>{}(p);
            h *= 1099511628211ull;
        }
        return h;
    }
};

} // namespace

const std::vector<RectangleGrid2D>& cached_partition(const PolygonGrid2D& polygon) {
    static thread_local std::unordered_map<std::vector<Vector2Int>, std::vector<RectangleGrid2D>, PointsHash>
        cache;
    const auto& key = polygon.points();
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    return cache.emplace(key, safe_partition(polygon)).first->second;
}

bool polygons_overlap_via_partitions(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b,
                                     Vector2Int pos_b) {
    // Cheap bbox prefilter first: most placement candidates are far apart
    {
        const auto& ba = a.bounding_rectangle();
        const auto& bb = b.bounding_rectangle();
        if (ba.b.x + pos_a.x <= bb.a.x + pos_b.x || bb.b.x + pos_b.x <= ba.a.x + pos_a.x ||
            ba.b.y + pos_a.y <= bb.a.y + pos_b.y || bb.b.y + pos_b.y <= ba.a.y + pos_a.y) {
            return false;
        }
    }
    const auto& ra = cached_partition(a);
    const auto& rb = cached_partition(b);
    if (ra.empty() || rb.empty()) {
        return polygons_overlap_area_exact(a, pos_a, b, pos_b) > 1e-9;
    }
    for (const auto& m : ra) {
        const RectangleGrid2D mw{m.a + pos_a, m.b + pos_a};
        for (const auto& f : rb) {
            const RectangleGrid2D fw{f.a + pos_b, f.b + pos_b};
            if (rectangles_overlap_open(mw, fw)) {
                return true;
            }
        }
    }
    return false;
}

using EventList = std::vector<std::pair<Vector2Int, bool>>;

static EventList overlap_along_line_rect_rect(const RectangleGrid2D& moving_rect,
                                              const RectangleGrid2D& fixed_rect,
                                              const OrthogonalLineGrid2D& line,
                                              int moving_rect_offset) {
    RectangleGrid2D bounding(moving_rect.a + line.from, moving_rect.b + line.to);
    if (!rectangles_overlap_open(bounding, fixed_rect)) {
        return {};
    }
    EventList events;
    const int moving_width = moving_rect.b.x - moving_rect.a.x;
    if (fixed_rect.a.x - moving_width - moving_rect_offset <= line.from.x) {
        events.push_back({line.from, true});
    }
    if (fixed_rect.a.x > line.from.x + moving_width + moving_rect_offset) {
        events.push_back({Vector2Int(fixed_rect.a.x - moving_width + 1 - moving_rect_offset, line.from.y), true});
    }
    if (fixed_rect.b.x - moving_rect_offset < line.to.x) {
        events.push_back({Vector2Int(fixed_rect.b.x - moving_rect_offset, line.from.y), false});
    }
    return events;
}

static EventList merge_events(EventList events1, EventList events2, const OrthogonalLineGrid2D& line) {
    if (events1.empty()) return events2;
    if (events2.empty()) return events1;
    EventList merged;
    std::size_t i1 = 0, i2 = 0;
    bool last_overlap = false;
    bool overlap1 = false, overlap2 = false;
    while (i1 < events1.size() && i2 < events2.size()) {
        const auto& p1 = events1[i1];
        const int pos1 = line.index_of_point(p1.first);
        const auto& p2 = events2[i2];
        const int pos2 = line.index_of_point(p2.first);
        if (pos1 <= pos2) {
            overlap1 = p1.second;
            ++i1;
        }
        if (pos1 >= pos2) {
            overlap2 = p2.second;
            ++i2;
        }
        const bool overlap = overlap1 || overlap2;
        if (overlap != last_overlap) {
            if (pos1 < pos2) {
                merged.push_back({p1.first, overlap});
            } else {
                merged.push_back({p2.first, overlap});
            }
        }
        last_overlap = overlap;
    }
    if (!events2.back().second) {
        while (i1 < events1.size()) {
            const auto& pair = events1[i1];
            if (merged.back().second != pair.second) {
                merged.push_back(pair);
            }
            ++i1;
        }
    }
    if (!events1.back().second) {
        while (i2 < events2.size()) {
            const auto& pair = events2[i2];
            if (merged.back().second != pair.second) {
                merged.push_back(pair);
            }
            ++i2;
        }
    }
    return merged;
}

static EventList reverse_events(const EventList& events, const OrthogonalLineGrid2D& line) {
    if (events.empty()) return events;
    EventList reversed = events;
    std::reverse(reversed.begin(), reversed.end());
    EventList result;
    if (events.back().second) {
        result.push_back({line.to, true});
    }
    const auto dir = line.direction_vector();
    for (const auto& ev : reversed) {
        if (!(ev.first == line.from && ev.second)) {
            result.push_back({ev.first - dir, !ev.second});
        }
    }
    return result;
}

static EventList overlap_along_line_rect_rects(const RectangleGrid2D& moving_rect,
                                               const std::vector<RectangleGrid2D>& fixed_rects,
                                               const OrthogonalLineGrid2D& line,
                                               int moving_rect_offset) {
    EventList events;
    for (const auto& fixed_rect : fixed_rects) {
        auto new_events = overlap_along_line_rect_rect(moving_rect, fixed_rect, line, moving_rect_offset);
        events = merge_events(std::move(events), std::move(new_events), line);
    }
    return events;
}

static bool moving_fixed_overlap_at(const PolygonGrid2D& moving, const PolygonGrid2D& fixed, Vector2Int position) {
    const auto& mr = cached_partition(moving);
    const auto& fr = cached_partition(fixed);
    if (mr.empty() || fr.empty()) {
        return polygons_overlap_area(moving, position, fixed, {0, 0});
    }
    for (const auto& m : mr) {
        const RectangleGrid2D mw{m.a + position, m.b + position};
        for (const auto& f : fr) {
            if (rectangles_overlap_open(mw, f)) {
                return true;
            }
        }
    }
    return false;
}

namespace detail {

std::vector<std::pair<Vector2Int, bool>> overlap_along_line_polygon_partition_bruteforce(
    const PolygonGrid2D& moving_polygon, const PolygonGrid2D& fixed_polygon, const OrthogonalLineGrid2D& line) {
    const auto pts = line.grid_points_inclusive();
    std::vector<std::pair<Vector2Int, bool>> events;
    std::optional<bool> prev;
    for (const Vector2Int& p : pts) {
        const bool ov = moving_fixed_overlap_at(moving_polygon, fixed_polygon, p);
        if (!prev.has_value()) {
            if (ov) events.push_back({p, ov});
            prev = ov;
        } else if (ov != *prev) {
            events.push_back({p, ov});
            prev = ov;
        }
    }
    return events;
}

} // namespace detail

std::vector<std::pair<Vector2Int, bool>> overlap_along_line_polygon_partition(
    const PolygonGrid2D& moving_polygon,
    const PolygonGrid2D& fixed_polygon,
    const OrthogonalLineGrid2D& line) {
    const auto dir = line.get_direction();
    const bool reverse = (dir == OrthogonalDirection::Bottom || dir == OrthogonalDirection::Left);
    auto working_line = reverse ? line.switch_orientation() : line;
    const int rotation = working_line.compute_rotation_clockwise_degrees();
    const auto rotated_line = working_line.rotate(rotation);
    auto moving_decomp = safe_partition(moving_polygon);
    auto fixed_decomp = safe_partition(fixed_polygon);
    if (moving_decomp.empty() || fixed_decomp.empty()) {
        return detail::overlap_along_line_polygon_partition_bruteforce(moving_polygon, fixed_polygon, line);
    }
    for (auto& r : moving_decomp) r = r.rotate(rotation);
    for (auto& r : fixed_decomp) r = r.rotate(rotation);
    int smallest_x = moving_decomp[0].a.x;
    for (const auto& r : moving_decomp) {
        smallest_x = std::min(smallest_x, r.a.x);
    }
    EventList events;
    for (const auto& moving_rect : moving_decomp) {
        const int offset = moving_rect.a.x - smallest_x;
        auto new_events = overlap_along_line_rect_rects(moving_rect, fixed_decomp, rotated_line, offset);
        events = merge_events(std::move(events), std::move(new_events), rotated_line);
    }
    if (reverse) {
        events = reverse_events(events, rotated_line);
    }
    for (auto& ev : events) {
        ev.first = ev.first.rotate_around_center(-rotation);
    }
    return events;
}

} // namespace dungeon_topology_generator::geometry
