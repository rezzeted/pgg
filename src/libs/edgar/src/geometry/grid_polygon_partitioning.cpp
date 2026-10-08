#include "edgar/geometry/grid_polygon_partitioning.hpp"

#include "edgar/geometry/bipartite_matching.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/rectangle_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <vector>

namespace edgar::geometry {

namespace {

struct Vertex {
    Vector2Int point{};
    int index = 0;
    bool concave = false;
    bool visited = false;
    int prev = -1;
    int next = -1;
};

int get_coord(const Vertex& v, bool is_x) { return is_x ? v.point.x : v.point.y; }

struct BoundarySegment {
    int from = -1;
    int to = -1;
    bool horizontal = false;
    int range_lo = 0;
    int range_hi = 0;
};

struct DiagonalSegment {
    int from = -1;
    int to = -1;
    bool horizontal = false;
    int range_lo = 0;
    int range_hi = 0;
};

class SegmentIntervalBag {
public:
    void add(BoundarySegment s) { segs_.push_back(s); }

    void remove(const BoundarySegment& target) {
        const auto it = std::find_if(segs_.begin(), segs_.end(), [&](const BoundarySegment& s) {
            return s.from == target.from && s.to == target.to && s.horizontal == target.horizontal;
        });
        if (it != segs_.end()) {
            segs_.erase(it);
        }
    }

    std::vector<const BoundarySegment*> query(int coord) const {
        std::vector<const BoundarySegment*> out;
        for (const auto& s : segs_) {
            if (s.range_lo <= coord && coord <= s.range_hi) {
                out.push_back(&s);
            }
        }
        return out;
    }

private:
    std::vector<BoundarySegment> segs_;
};

BoundarySegment make_boundary_seg(const std::vector<Vertex>& verts, int from, int to, bool horizontal) {
    BoundarySegment s;
    s.from = from;
    s.to = to;
    s.horizontal = horizontal;
    if (horizontal) {
        s.range_lo = std::min(verts[static_cast<std::size_t>(from)].point.x, verts[static_cast<std::size_t>(to)].point.x);
        s.range_hi = std::max(verts[static_cast<std::size_t>(from)].point.x, verts[static_cast<std::size_t>(to)].point.x);
    } else {
        s.range_lo = std::min(verts[static_cast<std::size_t>(from)].point.y, verts[static_cast<std::size_t>(to)].point.y);
        s.range_hi = std::max(verts[static_cast<std::size_t>(from)].point.y, verts[static_cast<std::size_t>(to)].point.y);
    }
    return s;
}

DiagonalSegment make_diagonal(const std::vector<Vertex>& verts, int from, int to, bool horizontal) {
    DiagonalSegment s;
    s.from = from;
    s.to = to;
    s.horizontal = horizontal;
    if (horizontal) {
        s.range_lo = std::min(verts[static_cast<std::size_t>(from)].point.x, verts[static_cast<std::size_t>(to)].point.x);
        s.range_hi = std::max(verts[static_cast<std::size_t>(from)].point.x, verts[static_cast<std::size_t>(to)].point.x);
    } else {
        s.range_lo = std::min(verts[static_cast<std::size_t>(from)].point.y, verts[static_cast<std::size_t>(to)].point.y);
        s.range_hi = std::max(verts[static_cast<std::size_t>(from)].point.y, verts[static_cast<std::size_t>(to)].point.y);
    }
    return s;
}

bool is_diagonal(const std::vector<Vertex>& verts, int from, int to, const SegmentIntervalBag& tree, bool horizontal) {
    const int ax = get_coord(verts[static_cast<std::size_t>(from)], !horizontal);
    const int bx = get_coord(verts[static_cast<std::size_t>(to)], !horizontal);
    const int q = get_coord(verts[static_cast<std::size_t>(from)], horizontal);
    for (const BoundarySegment* seg : tree.query(q)) {
        const int start = get_coord(verts[static_cast<std::size_t>(seg->from)], !horizontal);
        if ((ax < start && start < bx) || (bx < start && start < ax)) {
            return false;
        }
    }
    return true;
}

int vertex_compare(const Vertex& a, const Vertex& b, bool x_first) {
    if (x_first) {
        const int dx = a.point.x - b.point.x;
        if (dx != 0) {
            return dx < 0 ? -1 : 1;
        }
        const int dy = a.point.y - b.point.y;
        return dy < 0 ? -1 : (dy > 0 ? 1 : 0);
    }
    const int dy = a.point.y - b.point.y;
    if (dy != 0) {
        return dy < 0 ? -1 : 1;
    }
    const int dx = a.point.x - b.point.x;
    return dx < 0 ? -1 : (dx > 0 ? 1 : 0);
}

std::vector<DiagonalSegment> get_diagonals_safe(std::vector<Vertex>& verts, const SegmentIntervalBag& tree,
                                                bool horizontal) {
    std::vector<int> concave_idx;
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        if (verts[static_cast<std::size_t>(i)].concave) {
            concave_idx.push_back(i);
        }
    }
    std::sort(concave_idx.begin(), concave_idx.end(), [&](int ia, int ib) {
        return vertex_compare(verts[static_cast<std::size_t>(ia)], verts[static_cast<std::size_t>(ib)], horizontal) < 0;
    });
    const int n = static_cast<int>(verts.size());
    std::vector<DiagonalSegment> diagonals;
    for (std::size_t i = 0; i + 1 < concave_idx.size(); ++i) {
        const int fi = concave_idx[i];
        const int ti = concave_idx[i + 1];
        if (get_coord(verts[static_cast<std::size_t>(fi)], horizontal) !=
            get_coord(verts[static_cast<std::size_t>(ti)], horizontal)) {
            continue;
        }
        int diff = (verts[static_cast<std::size_t>(fi)].index - verts[static_cast<std::size_t>(ti)].index + n) % n;
        if (diff == 1 || diff == n - 1) {
            continue;
        }
        if (is_diagonal(verts, fi, ti, tree, horizontal)) {
            diagonals.push_back(make_diagonal(verts, fi, ti, !horizontal));
        }
    }
    return diagonals;
}

void find_crossings(const std::vector<Vertex>& verts, const std::vector<DiagonalSegment>& horizontal_diagonals,
                    const std::vector<DiagonalSegment>& vertical_diagonals,
                    std::vector<std::pair<int, int>>& cross_out) {
    for (int vj = 0; vj < static_cast<int>(vertical_diagonals.size()); ++vj) {
        const auto& vdiag = vertical_diagonals[static_cast<std::size_t>(vj)];
        const int ax = verts[static_cast<std::size_t>(vdiag.from)].point.y;
        const int bx = verts[static_cast<std::size_t>(vdiag.to)].point.y;
        const int ymin = std::min(ax, bx);
        const int ymax = std::max(ax, bx);
        const int vx = verts[static_cast<std::size_t>(vdiag.from)].point.x;

        for (int hj = 0; hj < static_cast<int>(horizontal_diagonals.size()); ++hj) {
            const auto& h = horizontal_diagonals[static_cast<std::size_t>(hj)];
            const int hy = verts[static_cast<std::size_t>(h.from)].point.y;
            const int hx1 = h.range_lo;
            const int hx2 = h.range_hi;
            if (hx1 <= vx && vx <= hx2) {
                if ((ymin <= hy && hy <= ymax) || (ymax <= hy && hy <= ymin)) {
                    cross_out.emplace_back(hj, vj);
                }
            }
        }
    }
}

void split_segment(std::vector<Vertex>& verts, const DiagonalSegment& segment) {
    const int a = segment.from;
    const int b = segment.to;
    const int pa = verts[static_cast<std::size_t>(a)].prev;
    const int na = verts[static_cast<std::size_t>(a)].next;
    const int pb = verts[static_cast<std::size_t>(b)].prev;
    const int nb = verts[static_cast<std::size_t>(b)].next;

    verts[static_cast<std::size_t>(a)].concave = false;
    verts[static_cast<std::size_t>(b)].concave = false;

    const bool seg_horiz = segment.horizontal;
    const bool ao = get_coord(verts[static_cast<std::size_t>(pa)], !seg_horiz) ==
                    get_coord(verts[static_cast<std::size_t>(a)], !seg_horiz);
    const bool bo = get_coord(verts[static_cast<std::size_t>(pb)], !seg_horiz) ==
                    get_coord(verts[static_cast<std::size_t>(b)], !seg_horiz);

    if (ao && bo) {
        verts[static_cast<std::size_t>(a)].prev = pb;
        verts[static_cast<std::size_t>(pb)].next = a;
        verts[static_cast<std::size_t>(b)].prev = pa;
        verts[static_cast<std::size_t>(pa)].next = b;
    } else if (ao && !bo) {
        verts[static_cast<std::size_t>(a)].prev = b;
        verts[static_cast<std::size_t>(b)].next = a;
        verts[static_cast<std::size_t>(pa)].next = nb;
        verts[static_cast<std::size_t>(nb)].prev = pa;
    } else if (!ao && bo) {
        verts[static_cast<std::size_t>(a)].next = b;
        verts[static_cast<std::size_t>(b)].prev = a;
        verts[static_cast<std::size_t>(na)].prev = pb;
        verts[static_cast<std::size_t>(pb)].next = na;
    } else {
        verts[static_cast<std::size_t>(a)].next = nb;
        verts[static_cast<std::size_t>(nb)].prev = a;
        verts[static_cast<std::size_t>(b)].next = na;
        verts[static_cast<std::size_t>(na)].prev = b;
    }
}

void rebuild_vertical_lr_trees(const std::vector<Vertex>& verts, SegmentIntervalBag& left_tree,
                               SegmentIntervalBag& right_tree) {
    left_tree = SegmentIntervalBag();
    right_tree = SegmentIntervalBag();
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        const int ni = verts[static_cast<std::size_t>(i)].next;
        if (ni < 0) {
            continue;
        }
        if (verts[static_cast<std::size_t>(ni)].point.x != verts[static_cast<std::size_t>(i)].point.x) {
            continue;
        }
        const BoundarySegment s = make_boundary_seg(verts, i, ni, false);
        if (verts[static_cast<std::size_t>(ni)].point.y > verts[static_cast<std::size_t>(i)].point.y) {
            left_tree.add(s);
        } else {
            right_tree.add(s);
        }
    }
}

void split_convave(std::vector<Vertex>& verts) {
    SegmentIntervalBag left_tree;
    SegmentIntervalBag right_tree;
    rebuild_vertical_lr_trees(verts, left_tree, right_tree);

    std::size_t i = 0;
    while (i < verts.size()) {
        const int vi = static_cast<int>(i);
        ++i;

        const int v_prev = verts[static_cast<std::size_t>(vi)].prev;
        const int v_next = verts[static_cast<std::size_t>(vi)].next;
        const int y = verts[static_cast<std::size_t>(vi)].point.y;
        const int px = verts[static_cast<std::size_t>(vi)].point.x;

        if (!verts[static_cast<std::size_t>(vi)].concave) {
            continue;
        }

        bool dir = false;
        if (verts[static_cast<std::size_t>(v_prev)].point.x == px) {
            dir = verts[static_cast<std::size_t>(v_prev)].point.y < y;
        } else {
            dir = verts[static_cast<std::size_t>(v_next)].point.y > y;
        }
        const int direction = dir ? 1 : -1;

        const BoundarySegment* closest_segment = nullptr;
        int closest_distance = direction == 1 ? INT_MAX : INT_MIN;

        if (direction > 0) {
            for (const BoundarySegment* s : right_tree.query(y)) {
                const int x = verts[static_cast<std::size_t>(s->from)].point.x;
                if (x > px && x < closest_distance) {
                    closest_distance = x;
                    closest_segment = s;
                }
            }
        } else {
            for (const BoundarySegment* s : left_tree.query(y)) {
                const int x = verts[static_cast<std::size_t>(s->from)].point.x;
                if (x < px && x > closest_distance) {
                    closest_distance = x;
                    closest_segment = s;
                }
            }
        }

        if (closest_segment == nullptr) {
            continue;
        }

        const int split_x = closest_distance;
        BoundarySegment old_seg = *closest_segment;

        Vertex split_a;
        split_a.point = {split_x, y};
        split_a.index = 0;
        split_a.concave = false;
        Vertex split_b;
        split_b.point = {split_x, y};
        split_b.index = 0;
        split_b.concave = false;

        verts[static_cast<std::size_t>(vi)].concave = false;

        verts.push_back(split_a);
        verts.push_back(split_b);
        const int sa = static_cast<int>(verts.size()) - 2;
        const int sb = static_cast<int>(verts.size()) - 1;

        verts[static_cast<std::size_t>(sa)].prev = old_seg.from;
        verts[static_cast<std::size_t>(old_seg.from)].next = sa;

        verts[static_cast<std::size_t>(sb)].next = old_seg.to;
        verts[static_cast<std::size_t>(old_seg.to)].prev = sb;

        SegmentIntervalBag* tree = direction > 0 ? &right_tree : &left_tree;
        tree->remove(old_seg);
        tree->add(make_boundary_seg(verts, old_seg.from, sa, false));
        tree->add(make_boundary_seg(verts, sb, old_seg.to, false));

        if (verts[static_cast<std::size_t>(v_prev)].point.x == px) {
            verts[static_cast<std::size_t>(sa)].next = v_next;
            verts[static_cast<std::size_t>(sb)].prev = vi;
            verts[static_cast<std::size_t>(v_next)].prev = sa;
            verts[static_cast<std::size_t>(vi)].next = sb;
        } else {
            verts[static_cast<std::size_t>(sa)].next = vi;
            verts[static_cast<std::size_t>(sb)].prev = v_prev;
            verts[static_cast<std::size_t>(v_prev)].next = sa;
            verts[static_cast<std::size_t>(vi)].prev = sb;
        }

        rebuild_vertical_lr_trees(verts, left_tree, right_tree);
    }
}

std::vector<RectangleGrid2D> find_regions(std::vector<Vertex>& verts) {
    for (auto& v : verts) {
        v.visited = false;
    }
    std::vector<RectangleGrid2D> rectangles;
    for (std::size_t si = 0; si < verts.size(); ++si) {
        if (verts[si].visited) {
            continue;
        }
        int cur = static_cast<int>(si);
        int minx = INT_MAX;
        int miny = INT_MAX;
        int maxx = INT_MIN;
        int maxy = INT_MIN;
        while (!verts[static_cast<std::size_t>(cur)].visited) {
            Vertex& v = verts[static_cast<std::size_t>(cur)];
            minx = std::min(v.point.x, minx);
            miny = std::min(v.point.y, miny);
            maxx = std::max(v.point.x, maxx);
            maxy = std::max(v.point.y, maxy);
            v.visited = true;
            cur = v.next;
        }
        if (minx == maxx || miny == maxy) {
            throw std::runtime_error("GridPolygonPartitioning: degenerate rectangle");
        }
        rectangles.emplace_back(Vector2Int{minx, miny}, Vector2Int{maxx, maxy});
    }
    return rectangles;
}

} // namespace

std::vector<RectangleGrid2D> partition_orthogonal_polygon_to_rectangles(const PolygonGrid2D& polygon) {
    const auto& points = polygon.points();
    const int n = static_cast<int>(points.size());
    if (n < 4) {
        return {};
    }

    std::vector<Vertex> verts;
    verts.reserve(static_cast<std::size_t>(n));

    Vector2Int prev = points[static_cast<std::size_t>(n - 3)];
    Vector2Int curr = points[static_cast<std::size_t>(n - 2)];
    Vector2Int next = points[static_cast<std::size_t>(n - 1)];

    for (int i = 0; i < n; ++i) {
        prev = curr;
        curr = next;
        next = points[static_cast<std::size_t>(i)];
        bool concave = false;
        if (prev.x == curr.x) {
            if (curr.x == next.x) {
                throw std::invalid_argument("GridPolygonPartitioning: polygon must be normalized");
            }
            const bool dir0 = prev.y < curr.y;
            const bool dir1 = curr.x > next.x;
            concave = dir0 == dir1;
        } else {
            if (next.y == curr.y) {
                throw std::invalid_argument("GridPolygonPartitioning: polygon must be normalized");
            }
            const bool dir0 = prev.x < curr.x;
            const bool dir1 = curr.y > next.y;
            concave = dir0 != dir1;
        }
        Vertex vx;
        vx.point = curr;
        vx.index = i - 1;
        vx.concave = concave;
        verts.push_back(vx);
    }

    for (int i = 0; i < n; ++i) {
        const int to = (i + 1) % n;
        verts[static_cast<std::size_t>(i)].next = to;
        verts[static_cast<std::size_t>(to)].prev = i;
    }

    SegmentIntervalBag horizontal_tree;
    SegmentIntervalBag vertical_tree;
    for (int i = 0; i < n; ++i) {
        const int to = (i + 1) % n;
        if (verts[static_cast<std::size_t>(i)].point.x == verts[static_cast<std::size_t>(to)].point.x) {
            vertical_tree.add(make_boundary_seg(verts, i, to, false));
        } else {
            horizontal_tree.add(make_boundary_seg(verts, i, to, true));
        }
    }

    auto horizontal_diagonals = get_diagonals_safe(verts, vertical_tree, false);
    auto vertical_diagonals = get_diagonals_safe(verts, horizontal_tree, true);

    std::vector<std::pair<int, int>> crossings;
    find_crossings(verts, horizontal_diagonals, vertical_diagonals, crossings);

    const int h_count = static_cast<int>(horizontal_diagonals.size());
    const int v_count = static_cast<int>(vertical_diagonals.size());

    const std::vector<int> selected = bipartite_max_independent_set(h_count, v_count, crossings);

    for (int s : selected) {
        if (s < h_count) {
            split_segment(verts, horizontal_diagonals[static_cast<std::size_t>(s)]);
        } else {
            split_segment(verts, vertical_diagonals[static_cast<std::size_t>(s - h_count)]);
        }
    }

    split_convave(verts);

    return find_regions(verts);
}

} // namespace edgar::geometry
