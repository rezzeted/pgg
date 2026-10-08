#include "dungeon_topology_generator/geometry/overlap.hpp"

#include "dungeon_topology_generator/geometry/clipper2_util.hpp"
#include "dungeon_topology_generator/geometry/polygon_overlap_grid2d.hpp"

#include <clipper2/clipper.h>

#include <cmath>

namespace dungeon_topology_generator::geometry {

bool polygons_overlap_area(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b) {
    // Exact for orthogonal polygons and much cheaper than a Clipper2 boolean op per query
    return polygons_overlap_via_partitions(a, pos_a, b, pos_b);
}

double polygons_overlap_area_exact(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b) {
    const Clipper2Lib::Path64 pa = polygon_to_path64_world(a, pos_a);
    const Clipper2Lib::Path64 pb = polygon_to_path64_world(b, pos_b);
    Clipper2Lib::Paths64 subj;
    subj.push_back(pa);
    Clipper2Lib::Paths64 clips;
    clips.push_back(pb);
    const Clipper2Lib::Paths64 inter =
        Clipper2Lib::Intersect(subj, clips, Clipper2Lib::FillRule::NonZero);
    return std::fabs(Clipper2Lib::Area(inter));
}

bool polygons_touch(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b, int min_shared_length) {
    const Clipper2Lib::Path64 pa = polygon_to_path64_world(a, pos_a);
    const Clipper2Lib::Path64 pb = polygon_to_path64_world(b, pos_b);

    for (std::size_t i = 0; i < pa.size(); ++i) {
        const auto& p1 = pa[i];
        const auto& p2 = pa[(i + 1) % pa.size()];
        for (std::size_t j = 0; j < pb.size(); ++j) {
            const auto& q1 = pb[j];
            const auto& q2 = pb[(j + 1) % pb.size()];

            if (p1.y == p2.y && q1.y == q2.y && p1.y == q1.y) {
                const auto lo_a = std::min(p1.x, p2.x);
                const auto hi_a = std::max(p1.x, p2.x);
                const auto lo_b = std::min(q1.x, q2.x);
                const auto hi_b = std::max(q1.x, q2.x);
                const auto overlap = std::min(hi_a, hi_b) - std::max(lo_a, lo_b);
                if (overlap >= min_shared_length) {
                    return true;
                }
            }
            if (p1.x == p2.x && q1.x == q2.x && p1.x == q1.x) {
                const auto lo_a = std::min(p1.y, p2.y);
                const auto hi_a = std::max(p1.y, p2.y);
                const auto lo_b = std::min(q1.y, q2.y);
                const auto hi_b = std::max(q1.y, q2.y);
                const auto overlap = std::min(hi_a, hi_b) - std::max(lo_a, lo_b);
                if (overlap >= min_shared_length) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool polygons_have_minimum_distance(const PolygonGrid2D& a, Vector2Int pos_a, const PolygonGrid2D& b, Vector2Int pos_b, int min_distance) {
    const auto& br_a = a.bounding_rectangle();
    const auto& br_b = b.bounding_rectangle();

    const int a_min_x = br_a.a.x + pos_a.x;
    const int a_max_x = br_a.b.x + pos_a.x;
    const int a_min_y = br_a.a.y + pos_a.y;
    const int a_max_y = br_a.b.y + pos_a.y;

    const int b_min_x = br_b.a.x + pos_b.x;
    const int b_max_x = br_b.b.x + pos_b.x;
    const int b_min_y = br_b.a.y + pos_b.y;
    const int b_max_y = br_b.b.y + pos_b.y;

    const int gap_x = std::max(0, std::max(a_min_x - b_max_x, b_min_x - a_max_x));
    const int gap_y = std::max(0, std::max(a_min_y - b_max_y, b_min_y - a_max_y));
    const int dist = std::max(gap_x, gap_y);

    return dist >= min_distance;
}

PolygonGrid2D normalize_polygon(const PolygonGrid2D& polygon) {
    const auto& pts = polygon.points();
    if (pts.size() < 4) {
        return polygon;
    }

    std::size_t best = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].x < pts[best].x || (pts[i].x == pts[best].x && pts[i].y < pts[best].y)) {
            best = i;
        }
    }

    std::vector<Vector2Int> rotated;
    rotated.reserve(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
        rotated.push_back(pts[(best + i) % pts.size()]);
    }
    return PolygonGrid2D(std::move(rotated));
}

} // namespace dungeon_topology_generator::geometry
