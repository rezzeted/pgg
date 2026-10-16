// G8 emit (B2). See emit.h.

#include "pch.h"

#include "emit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "codes.h"
#include "roof.h"  // point_in_poly, build_roof_model

namespace building_topology_generator {

namespace {

constexpr int kOutside = -2;

// A merged wall run on a grid line with one side pair.
struct WallRun {
    bool vertical = false;
    int line = 0;
    int c0 = 0, c1 = 0;  // cells along the wall
    int lo = kOutside, hi = kOutside;  // low side (left/below cell), high side
};

std::vector<WallRun> scan_walls(const FloorPlan& fp) {
    std::vector<WallRun> walls;
    auto at = [&](int i, int j) { return fp.inside(i, j) ? fp.at(i, j) : kCellOutside; };
    for (int X = 0; X <= fp.w; ++X)
        for (int z = 0; z < fp.h;) {
            const int lo = at(X - 1, z), hi = at(X, z);
            if (lo == hi || (lo < 0 && hi < 0)) {
                ++z;
                continue;
            }
            int z1 = z + 1;
            while (z1 < fp.h && at(X - 1, z1) == lo && at(X, z1) == hi) ++z1;
            walls.push_back({true, X, z, z1, lo, hi});
            z = z1;
        }
    for (int Z = 0; Z <= fp.h; ++Z)
        for (int i = 0; i < fp.w;) {
            const int lo = at(i, Z - 1), hi = at(i, Z);
            if (lo == hi || (lo < 0 && hi < 0)) {
                ++i;
                continue;
            }
            int i1 = i + 1;
            while (i1 < fp.w && at(i1, Z - 1) == lo && at(i1, Z) == hi) ++i1;
            walls.push_back({false, Z, i, i1, lo, hi});
            i = i1;
        }
    return walls;
}

std::string side_id(const FloorPlan& fp, int r) {
    return r >= 0 ? fp.rooms[static_cast<size_t>(r)].id : "outside";
}

// Trace the boundary of the room's raster cells as an orthogonal polygon in
// meters (building frame, NO wall half-thickness inset: an L has no single
// shrink — the net area carries the metric, the outline is informational).
// Edges carry the room on their right (negative signed area in (x, z), the
// same winding as the rectangle outline). Falls back to the cell bbox when
// the boundary walk breaks (defensive: a 4-connected room traces one loop).
std::vector<glm::dvec2> room_outline_traced(const FloorPlan& fp, int r, double s) {
    struct Edge {
        int ax, az, bx, bz;
    };
    std::vector<Edge> edges;
    std::map<std::pair<int, int>, std::vector<int>> from;
    auto add = [&](int ax, int az, int bx, int bz) {
        from[{ax, az}].push_back(static_cast<int>(edges.size()));
        edges.push_back({ax, az, bx, bz});
    };
    const int32_t want = static_cast<int32_t>(r);
    int bx0 = fp.w, bz0 = fp.h, bx1 = 0, bz1 = 0;
    for (int j = 0; j < fp.h; ++j)
        for (int i = 0; i < fp.w; ++i) {
            if (fp.at(i, j) != want) continue;
            bx0 = std::min(bx0, i);
            bz0 = std::min(bz0, j);
            bx1 = std::max(bx1, i + 1);
            bz1 = std::max(bz1, j + 1);
            if (fp.room_at(i - 1, j) != want) add(i, j, i, j + 1);
            if (fp.room_at(i + 1, j) != want) add(i + 1, j + 1, i + 1, j);
            if (fp.room_at(i, j - 1) != want) add(i + 1, j, i, j);
            if (fp.room_at(i, j + 1) != want) add(i, j + 1, i + 1, j + 1);
        }
    auto bbox_outline = [&] {
        return std::vector<glm::dvec2>{{fp.origin.x + bx0 * s, fp.origin.y + bz0 * s},
                                       {fp.origin.x + bx0 * s, fp.origin.y + bz1 * s},
                                       {fp.origin.x + bx1 * s, fp.origin.y + bz1 * s},
                                       {fp.origin.x + bx1 * s, fp.origin.y + bz0 * s}};
    };
    if (edges.empty()) return bbox_outline();
    std::vector<char> used(edges.size(), 0);
    std::vector<glm::dvec2> out;
    glm::ivec2 cur{edges[0].ax, edges[0].az};
    const glm::ivec2 start = cur;
    for (;;) {
        out.push_back({fp.origin.x + cur.x * s, fp.origin.y + cur.y * s});
        auto it = from.find({cur.x, cur.y});
        if (it == from.end()) return bbox_outline();
        int next = -1;
        for (int e : it->second)
            if (!used[static_cast<size_t>(e)]) {
                next = e;
                break;
            }
        if (next < 0) break;
        used[static_cast<size_t>(next)] = 1;
        cur = {edges[static_cast<size_t>(next)].bx, edges[static_cast<size_t>(next)].bz};
        if (cur == start) break;
    }
    for (char u : used)
        if (!u) return bbox_outline();
    if (cur != start) return bbox_outline();
    return out;
}

}  // namespace

bool emit_floor_ir(const FloorPlan& fp, const DerivedOpenings& ops,
                   const ExpandedProgram& prog, const PlanParams& params,
                   const CostBreakdown& cost, const AnnealReport& anneal, Ir& ir,
                   std::string& err) {
    if (ir.storeys.empty()) {
        err = "emit_floor_ir: the IR has no storeys";
        return false;
    }
    const IrStorey& storey = ir.storeys.front();  // B2: the ground storey
    const double s = params.ir.cell;
    const int N = static_cast<int>(fp.rooms.size());

    // -- Spaces ---------------------------------------------------------------
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        IrSpace sp;
        sp.id = room.id;
        sp.storey = storey.id;
        sp.fn_code = room.fn_code;
        sp.zone_code = room.zone_code;
        // Cells from the raster: for a plain rect this is the rect loop's
        // row-major order exactly; for a winged room it adds the wing cells.
        for (int j = 0; j < fp.h; ++j)
            for (int i = 0; i < fp.w; ++i)
                if (fp.at(i, j) == static_cast<int32_t>(r)) sp.cells.emplace_back(i, j);
        // Inner-face polygon: the rect inset by the half thickness per side
        // (exterior dominates a mixed side), wound CCW seen from +Y
        // (negative signed area in (x, z), as the massing outlines).
        auto neighbor_t = [&](int i, int j) {
            const int32_t n = fp.inside(i, j) ? fp.at(i, j) : kCellOutside;
            return (n == kCellOutside ? params.ir.t_exterior : params.ir.t_partition) / 2;
        };
        double t_x0 = 0.0, t_x1 = 0.0, t_z0 = 0.0, t_z1 = 0.0;
        for (int j = room.z0; j < room.z1; ++j) {
            t_x0 = std::max(t_x0, neighbor_t(room.x0 - 1, j));
            t_x1 = std::max(t_x1, neighbor_t(room.x1, j));
        }
        for (int i = room.x0; i < room.x1; ++i) {
            t_z0 = std::max(t_z0, neighbor_t(i, room.z0 - 1));
            t_z1 = std::max(t_z1, neighbor_t(i, room.z1));
        }
        const double xa = fp.origin.x + room.x0 * s + t_x0;
        const double xb = fp.origin.x + room.x1 * s - t_x1;
        const double za = fp.origin.y + room.z0 * s + t_z0;
        const double zb = fp.origin.y + room.z1 * s - t_z1;
        sp.outline = room.l_shaped()
                         ? room_outline_traced(fp, r, s)
                         : std::vector<glm::dvec2>{{xa, za}, {xa, zb}, {xb, zb}, {xb, za}};
        sp.area_net = fp.room_area_net(r, params.ir);
        sp.habitable = room.habitable;
        sp.hearth_code = room.hearth_code;
        ir.spaces.push_back(std::move(sp));
    }

    // -- Walls ------------------------------------------------------------------
    const std::vector<WallRun> runs = scan_walls(fp);
    std::vector<int> wall_of_run;  // run index -> ir.walls index
    int wall_counter = 0;
    for (const WallRun& run : runs) {
        IrWall w;
        char id[32];
        std::snprintf(id, sizeof(id), "w_g_%03d", wall_counter++);
        w.id = id;
        w.storey = storey.id;
        if (run.vertical) {
            w.a = {fp.origin.x + run.line * s, fp.origin.y + run.c0 * s};
            w.b = {fp.origin.x + run.line * s, fp.origin.y + run.c1 * s};
            // a -> b runs +z: side_l is the -x (low) side.
            w.side_l = side_id(fp, run.lo);
            w.side_r = side_id(fp, run.hi);
        } else {
            w.a = {fp.origin.x + run.c0 * s, fp.origin.y + run.line * s};
            w.b = {fp.origin.x + run.c1 * s, fp.origin.y + run.line * s};
            // a -> b runs +x: side_l is the +z (high) side.
            w.side_l = side_id(fp, run.hi);
            w.side_r = side_id(fp, run.lo);
        }
        const bool exterior = run.lo < 0 || run.hi < 0;
        w.class_code = exterior ? kWallExterior : kWallPartition;
        w.thickness = exterior ? params.ir.t_exterior : params.ir.t_partition;
        // One owner per shared wall (I9): the lexicographically smaller room
        // id; exterior walls belong to their room.
        w.owner = w.side_l == "outside" ? w.side_r
                  : w.side_r == "outside"
                      ? w.side_l
                      : std::min(w.side_l, w.side_r);
        w.h = storey.h;
        wall_of_run.push_back(static_cast<int>(ir.walls.size()));
        ir.walls.push_back(std::move(w));
    }

    // -- Openings ---------------------------------------------------------------
    // Door ids d_g_###, window ids w_g_###, one counter per storey (creation
    // order: doors first, as derived).
    int opening_counter = 0;
    std::vector<std::string> emitted_id(ops.openings.size());
    for (size_t oi = 0; oi < ops.openings.size(); ++oi) {
        const DerivedOpening& o = ops.openings[oi];
        // The host wall: same orientation and line, span covering [m0, m1],
        // side pair matching the opening's rooms.
        const std::string ra = side_id(fp, o.room_a);
        const std::string rb = o.room_b == kOutside ? "outside" : side_id(fp, o.room_b);
        int host = -1;
        for (size_t k = 0; k < runs.size(); ++k) {
            const WallRun& run = runs[k];
            if (run.vertical != o.vertical || run.line != o.line) continue;
            const double w0 = (run.vertical ? fp.origin.y : fp.origin.x) + run.c0 * s;
            const double w1 = (run.vertical ? fp.origin.y : fp.origin.x) + run.c1 * s;
            if (o.m0 < w0 - 1e-6 || o.m1 > w1 + 1e-6) continue;
            const std::string rl = side_id(fp, run.lo), rh = side_id(fp, run.hi);
            const bool match = (rl == ra && rh == rb) || (rl == rb && rh == ra);
            if (!match) continue;
            host = static_cast<int>(k);
            break;
        }
        if (host < 0) {
            err = "emit_floor_ir: no host wall for an opening of " + ra;
            return false;
        }
        IrWall& wall = ir.walls[static_cast<size_t>(wall_of_run[static_cast<size_t>(host)])];
        IrOpening io;
        char id[32];
        std::snprintf(id, sizeof(id), "%s_g_%03d",
                      o.kind_code == kOpeningDoor ? "d" : "w", opening_counter++);
        io.id = id;
        emitted_id[oi] = id;
        io.kind_code = o.kind_code;
        io.type = o.type;
        const double w0 = (o.vertical ? wall.a.y : wall.a.x);
        io.s0 = o.m0 - w0;
        io.s1 = o.m1 - w0;
        io.y0 = o.y0;
        io.y1 = o.y1;
        wall.openings.push_back(std::move(io));
    }

    // -- Nodes ------------------------------------------------------------------
    // One node per wall endpoint; faces are the unit directions of the walls
    // leaving the node. Owner: the volume containing the point (frozen-IR
    // convention, §10).
    struct NodeAcc {
        glm::dvec2 p;
        std::vector<glm::dvec2> dirs;
    };
    std::map<std::pair<long, long>, NodeAcc> node_map;
    auto node_key = [](const glm::dvec2& p) {
        return std::pair{std::lround(p.x / kIrQuant), std::lround(p.y / kIrQuant)};
    };
    auto add_face = [&](const glm::dvec2& p, const glm::dvec2& dir) {
        NodeAcc& acc = node_map[node_key(p)];
        acc.p = p;
        for (const glm::dvec2& d : acc.dirs)
            if (d == dir) return;
        acc.dirs.push_back(dir);
    };
    for (const IrWall& w : ir.walls) {
        glm::dvec2 d = w.b - w.a;
        const double len = glm::length(d);
        if (len < 1e-9) continue;
        d /= len;
        add_face(w.b, d);
        add_face(w.a, -d);
    }
    int node_counter = 0;
    for (auto& [key, acc] : node_map) {
        IrNode node;
        char id[32];
        std::snprintf(id, sizeof(id), "n_g_%02d", node_counter++);
        node.id = id;
        node.storey = storey.id;
        node.p = acc.p;
        node.owner = ir.volumes.empty() ? "main" : ir.volumes.front().id;
        for (const IrVolume& v : ir.volumes)
            if (point_in_poly(v.outline, acc.p.x, acc.p.y)) {
                node.owner = v.id;
                break;
            }
        std::sort(acc.dirs.begin(), acc.dirs.end(),
                  [](const glm::dvec2& a, const glm::dvec2& b) {
                      if (a.x != b.x) return a.x < b.x;
                      return a.y < b.y;
                  });
        for (const glm::dvec2& d : acc.dirs) node.faces.push_back({d, storey.h});
        ir.nodes.push_back(std::move(node));
    }

    // -- Access graph -----------------------------------------------------------
    ir.access.nodes.push_back("outside");
    for (const IrSpace& sp : ir.spaces) ir.access.nodes.push_back(sp.id);
    for (size_t oi = 0; oi < ops.openings.size(); ++oi) {
        const DerivedOpening& o = ops.openings[oi];
        if (o.kind_code != kOpeningDoor) continue;
        IrAccessEdge e;
        const std::string ra = side_id(fp, o.room_a);
        const std::string rb = o.room_b == kOutside ? "outside" : side_id(fp, o.room_b);
        // Frozen convention: the street comes first.
        e.a = rb == "outside" ? rb : std::min(ra, rb);
        e.b = rb == "outside" ? ra : std::max(ra, rb);
        e.via = "door";
        e.ref = emitted_id[oi];
        ir.access.edges.push_back(std::move(e));
    }

    // -- Anchors ----------------------------------------------------------------
    // Entrance: 0.6 m out of the facade, facing the street (frozen IR). Only
    // the front street door anchors — a room hosting both the entrance and
    // the back door gets exactly one (the back door is a plain opening).
    for (const DerivedOpening& o : ops.openings) {
        if (o.kind_code != kOpeningDoor || o.room_b != kOutside) continue;
        const PlanRoom& room = fp.rooms[static_cast<size_t>(o.room_a)];
        if (!room.entrance_front || o.vertical || o.line != room.z1) continue;
        IrAnchor a;
        a.kind_code = kAnchorEntrance;
        const double mid = (o.m0 + o.m1) / 2.0;
        const double z = fp.origin.y + o.line * s;
        a.p = glm::dvec3(mid, storey.y, z + 0.6);
        a.dir = glm::dvec3(0.0, 0.0, 1.0);
        a.space = room.id;
        ir.anchors.push_back(a);
    }
    // Hearths: back-to-back on the wall shared with another hearth room when
    // possible (chimney pairing, §9.2), else the longest interior side;
    // 0.9 m off the wall axis, facing into the room (frozen IR convention).
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (room.hearth_code == 0) continue;
        int other = -1;
        for (const auto& [a, b] : ops.adjacency) {
            const int o = a == r ? b : b == r ? a : -1;
            if (o >= 0 && fp.rooms[static_cast<size_t>(o)].hearth_code != 0) {
                other = o;
                break;
            }
        }
        // Wall side toward `other` (or the longest interior side): 0=-x 1=+x
        // 2=-z 3=+z, the direction out of the room.
        int side = -1;
        if (other >= 0) {
            const PlanRoom& B = fp.rooms[static_cast<size_t>(other)];
            if (room.x1 == B.x0) side = 1;
            else if (room.x0 == B.x1) side = 0;
            else if (room.z1 == B.z0) side = 3;
            else if (room.z0 == B.z1) side = 2;
        }
        if (side < 0) {
            // Longest side shared with any room.
            int best_len = 0;
            for (const auto& [a, b] : ops.adjacency) {
                const int o = a == r ? b : b == r ? a : -1;
                if (o < 0) continue;
                const PlanRoom& B = fp.rooms[static_cast<size_t>(o)];
                if (room.x1 == B.x0 || room.x0 == B.x1) {
                    const int len =
                        std::min(room.z1, B.z1) - std::max(room.z0, B.z0);
                    const int sd = room.x1 == B.x0 ? 1 : 0;
                    if (len > best_len) {
                        best_len = len;
                        side = sd;
                    }
                } else if (room.z1 == B.z0 || room.z0 == B.z1) {
                    const int len =
                        std::min(room.x1, B.x1) - std::max(room.x0, B.x0);
                    const int sd = room.z1 == B.z0 ? 3 : 2;
                    if (len > best_len) {
                        best_len = len;
                        side = sd;
                    }
                }
            }
        }
        if (side < 0) side = 2;  // fully detached room: fall back to the -z side
        const double wx = side == 0 ? fp.origin.x + room.x0 * s
                          : side == 1 ? fp.origin.x + room.x1 * s
                                      : 0.0;
        const double wz = side == 2 ? fp.origin.y + room.z0 * s
                          : side == 3 ? fp.origin.y + room.z1 * s
                                      : 0.0;
        // Outward normal of the side; the anchor faces into the room.
        glm::dvec2 out_n(0.0);
        double along = 0.0;
        if (side == 0) {
            out_n = {-1, 0};
            along = fp.origin.y + (room.z0 + room.z1) * s / 2.0;
        } else if (side == 1) {
            out_n = {1, 0};
            along = fp.origin.y + (room.z0 + room.z1) * s / 2.0;
        } else if (side == 2) {
            out_n = {0, -1};
            along = fp.origin.x + (room.x0 + room.x1) * s / 2.0;
        } else {
            out_n = {0, 1};
            along = fp.origin.x + (room.x0 + room.x1) * s / 2.0;
        }
        // For a hearth pair the anchor centers on the shared span, not the
        // whole side.
        if (other >= 0) {
            const PlanRoom& B = fp.rooms[static_cast<size_t>(other)];
            if (side < 2) {
                const double za = fp.origin.y + std::max(room.z0, B.z0) * s;
                const double zb = fp.origin.y + std::min(room.z1, B.z1) * s;
                along = (za + zb) / 2.0;
            } else {
                const double xa = fp.origin.x + std::max(room.x0, B.x0) * s;
                const double xb = fp.origin.x + std::min(room.x1, B.x1) * s;
                along = (xa + xb) / 2.0;
            }
        }
        IrAnchor a;
        a.kind_code = kAnchorHearth;
        const glm::dvec2 wall_p = side < 2 ? glm::dvec2(wx, along) : glm::dvec2(along, wz);
        a.p = glm::dvec3(wall_p.x - out_n.x * 0.9, storey.y, wall_p.y - out_n.y * 0.9);
        a.dir = glm::dvec3(-out_n.x, 0.0, -out_n.y);
        a.space = room.id;
        ir.anchors.push_back(a);
    }
    // Bed slots: 1.0 m off the first exterior side (order -x +x -z +z),
    // facing into the room.
    for (int r = 0; r < N; ++r) {
        const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
        if (room.fn_code != kFnBedroom) continue;
        int side = -1;
        for (int sd : {0, 1, 2, 3}) {
            bool exterior = false;
            if (sd == 0)
                for (int j = room.z0; j < room.z1 && !exterior; ++j)
                    exterior = !fp.inside(room.x0 - 1, j) || fp.at(room.x0 - 1, j) == kCellOutside;
            if (sd == 1)
                for (int j = room.z0; j < room.z1 && !exterior; ++j)
                    exterior = !fp.inside(room.x1, j) || fp.at(room.x1, j) == kCellOutside;
            if (sd == 2)
                for (int i = room.x0; i < room.x1 && !exterior; ++i)
                    exterior = !fp.inside(i, room.z0 - 1) || fp.at(i, room.z0 - 1) == kCellOutside;
            if (sd == 3)
                for (int i = room.x0; i < room.x1 && !exterior; ++i)
                    exterior = !fp.inside(i, room.z1) || fp.at(i, room.z1) == kCellOutside;
            if (exterior) {
                side = sd;
                break;
            }
        }
        if (side < 0) side = 0;
        IrAnchor a;
        a.kind_code = kAnchorBedSlot;
        glm::dvec2 in_n(0.0);
        glm::dvec2 base(0.0);
        const double zm = fp.origin.y + (room.z0 + room.z1) * s / 2.0;
        const double xm = fp.origin.x + (room.x0 + room.x1) * s / 2.0;
        if (side == 0) {
            in_n = {1, 0};
            base = {fp.origin.x + room.x0 * s, zm};
        } else if (side == 1) {
            in_n = {-1, 0};
            base = {fp.origin.x + room.x1 * s, zm};
        } else if (side == 2) {
            in_n = {0, 1};
            base = {xm, fp.origin.y + room.z0 * s};
        } else {
            in_n = {0, -1};
            base = {xm, fp.origin.y + room.z1 * s};
        }
        a.p = glm::dvec3(base.x + in_n.x * 1.0, storey.y, base.y + in_n.y * 1.0);
        a.dir = glm::dvec3(in_n.x, 0.0, in_n.y);
        a.space = room.id;
        ir.anchors.push_back(a);
    }

    // -- Report -------------------------------------------------------------------
    for (const auto& [name, v] : cost.terms) ir.report_cost[name] = v;
    for (const auto& [name, n] : cost.hard_terms) ir.report_cost["hard_" + name] = n;
    ir.report_cost["hard"] = cost.hard;
    ir.report_cost["soft"] = cost.soft;
    ir.report_moves = anneal.log;

    (void)prog;
    return true;
}

// ---------------------------------------------------------------------------
// B3: multi-storey emit
// ---------------------------------------------------------------------------

namespace {

// A wall/railing run over the rasterized cells, keeping every label
// transition (the stair well and the roof void are labels of their own).
struct B3Run {
    bool vertical = false;
    int line = 0;
    int c0 = 0, c1 = 0;
    int32_t lo = kCellOutside, hi = kCellOutside;
};

std::vector<B3Run> scan_runs_all(const FloorPlan& fp) {
    std::vector<B3Run> runs;
    auto at = [&](int i, int j) { return fp.inside(i, j) ? fp.at(i, j) : kCellOutside; };
    for (int X = 0; X <= fp.w; ++X)
        for (int z = 0; z < fp.h;) {
            const int32_t lo = at(X - 1, z), hi = at(X, z);
            if (lo == hi) {
                ++z;
                continue;
            }
            int z1 = z + 1;
            while (z1 < fp.h && at(X - 1, z1) == lo && at(X, z1) == hi) ++z1;
            runs.push_back({true, X, z, z1, lo, hi});
            z = z1;
        }
    for (int Z = 0; Z <= fp.h; ++Z)
        for (int i = 0; i < fp.w;) {
            const int32_t lo = at(i, Z - 1), hi = at(i, Z);
            if (lo == hi) {
                ++i;
                continue;
            }
            int i1 = i + 1;
            while (i1 < fp.w && at(i1, Z - 1) == lo && at(i1, Z) == hi) ++i1;
            runs.push_back({false, Z, i, i1, lo, hi});
            i = i1;
        }
    return runs;
}

double dist_pt_seg_2d(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b) {
    const glm::dvec2 ab = b - a;
    const double l2 = glm::dot(ab, ab);
    const double t =
        l2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    return glm::length(p - (a + ab * t));
}

constexpr double kDeg2Rad = 0.017453292519943295;

}  // namespace

bool emit_building_ir(const BuildingPlanResult& bp, const ExpandedProgram& prog,
                      const PlanParams& params, Ir& ir, std::string& err) {
    if (!bp.ok) {
        err = "emit_building_ir: the plan failed: " + bp.error;
        return false;
    }
    if (bp.storeys.size() != ir.storeys.size()) {
        err = "emit_building_ir: plan/IR storey count mismatch";
        return false;
    }
    const double s = params.ir.cell;
    const int nst = static_cast<int>(ir.storeys.size());

    // Roof data per volume: the ceiling clip planes (the sloped planes
    // shifted down to the pie underside, §5.6) and the ridge height.
    struct VolRoof {
        std::vector<IrPlane> ceilings;
        double ridge_y = 0.0;
    };
    std::vector<VolRoof> vroofs(ir.volumes.size());
    for (size_t vi = 0; vi < ir.volumes.size(); ++vi) {
        const IrVolume& v = ir.volumes[vi];
        RoofModel m;
        if (!build_roof_model(v, m, err)) {
            err = "emit_building_ir: volume " + v.id + ": " + err;
            return false;
        }
        VolRoof& vr = vroofs[vi];
        const double rt = v.roof.roof_t > 0.0 ? v.roof.roof_t : params.ir.roof_t;
        for (const IrRoofPlane& pl : m.planes) {
            IrPlane c;
            c.o = pl.o - glm::dvec3(0.0, rt / std::cos(pl.pitch * kDeg2Rad), 0.0);
            c.n = pl.n;
            vr.ceilings.push_back(c);
        }
        double ry = v.roof.eave_y;
        for (const IrRoofEdge& e : m.ridges) ry = std::max({ry, e.a.y, e.b.y});
        vr.ridge_y = ry;
    }
    auto volume_at = [&](const glm::dvec2& p) -> int {
        for (size_t vi = 0; vi < ir.volumes.size(); ++vi)
            if (point_in_poly(ir.volumes[vi].outline, p.x, p.y)) return static_cast<int>(vi);
        return -1;
    };
    // Gable end = the nearest outline edge has a ~90 degree pitch (§7.2).
    auto gable_at = [&](int vi, const glm::dvec2& p) {
        if (vi < 0) return false;
        const IrVolume& v = ir.volumes[static_cast<size_t>(vi)];
        double best = 1e30;
        int be = -1;
        const size_t n = v.outline.size();
        for (size_t e = 0; e < n; ++e) {
            const double d = dist_pt_seg_2d(p, v.outline[e], v.outline[(e + 1) % n]);
            if (d < best) {
                best = d;
                be = static_cast<int>(e);
            }
        }
        return be >= 0 && be < static_cast<int>(v.roof.pitch_by_edge.size()) &&
               v.roof.pitch_by_edge[static_cast<size_t>(be)] >= 89.0;
    };
    auto prefix_of = [&](int k) {
        const IrStorey& st = ir.storeys[static_cast<size_t>(k)];
        if (st.kind_code == kStoreyGround) return std::string("g");
        if (st.kind_code == kStoreyAttic) return std::string("a");
        return k == 1 ? std::string("u") : "u" + std::to_string(k);
    };

    ir.access.nodes.push_back("outside");
    double hard_sum = 0.0, soft_sum = 0.0;

    for (int k = 0; k < nst; ++k) {
        const PlanResult& pr = bp.storeys[static_cast<size_t>(k)];
        const FloorPlan& fp = pr.plan;
        const DerivedOpenings& ops = pr.openings;
        const IrStorey& storey = ir.storeys[static_cast<size_t>(k)];
        const std::string p = prefix_of(k);
        const bool is_attic = storey.kind_code == kStoreyAttic;
        const int N = static_cast<int>(fp.rooms.size());

        auto space_id = [&](int r) { return p + "_" + fp.rooms[static_cast<size_t>(r)].id; };
        auto side_name = [&](int32_t label) -> std::string {
            if (label >= 0) return space_id(label);
            if (label == kCellOutside) return "outside";
            return "void";  // stair well or roof void (§5.6, frozen contract)
        };

        // -- Spaces ---------------------------------------------------------
        for (int r = 0; r < N; ++r) {
            const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
            IrSpace sp;
            sp.id = space_id(r);
            sp.storey = storey.id;
            sp.fn_code = room.fn_code;
            sp.zone_code = room.zone_code;
            // Cells from the raster: for a plain rect this is the rect loop's
            // row-major order exactly; for a winged room it adds the wings.
            for (int j = 0; j < fp.h; ++j)
                for (int i = 0; i < fp.w; ++i)
                    if (fp.at(i, j) == static_cast<int32_t>(r)) sp.cells.emplace_back(i, j);
            auto neighbor_t = [&](int i, int j) {
                const int32_t n = fp.inside(i, j) ? fp.at(i, j) : kCellOutside;
                return side_half_t(n, params.ir);
            };
            double t_x0 = 0.0, t_x1 = 0.0, t_z0 = 0.0, t_z1 = 0.0;
            for (int j = room.z0; j < room.z1; ++j) {
                t_x0 = std::max(t_x0, neighbor_t(room.x0 - 1, j));
                t_x1 = std::max(t_x1, neighbor_t(room.x1, j));
            }
            for (int i = room.x0; i < room.x1; ++i) {
                t_z0 = std::max(t_z0, neighbor_t(i, room.z0 - 1));
                t_z1 = std::max(t_z1, neighbor_t(i, room.z1));
            }
            const double xa = fp.origin.x + room.x0 * s + t_x0;
            const double xb = fp.origin.x + room.x1 * s - t_x1;
            const double za = fp.origin.y + room.z0 * s + t_z0;
            const double zb = fp.origin.y + room.z1 * s - t_z1;
            sp.outline = room.l_shaped()
                             ? room_outline_traced(fp, r, s)
                             : std::vector<glm::dvec2>{{xa, za}, {xa, zb}, {xb, zb}, {xb, za}};
            sp.area_net = fp.room_area_net(r, params.ir);
            sp.habitable = room.habitable;
            sp.hearth_code = room.hearth_code;
            ir.spaces.push_back(std::move(sp));
            ir.access.nodes.push_back(space_id(r));
        }

        // -- Walls ------------------------------------------------------------
        const std::vector<B3Run> runs = scan_runs_all(fp);
        std::vector<int> wall_of_run(runs.size(), -1);
        const size_t wall_begin = ir.walls.size();
        int wall_counter = 0;
        for (size_t ri = 0; ri < runs.size(); ++ri) {
            const B3Run& run = runs[ri];
            const bool has_room = run.lo >= 0 || run.hi >= 0;
            const bool has_void = run.lo == kCellVoid || run.hi == kCellVoid;
            const bool has_rv = run.lo == kCellRoofVoid || run.hi == kCellRoofVoid;
            const bool has_out = run.lo == kCellOutside || run.hi == kCellOutside;
            if (has_void && !has_out) continue;  // open well edge -> railing below
            if (!has_room && !has_out) continue;  // void|roofvoid: no wall body

            IrWall w;
            char id[32];
            std::snprintf(id, sizeof(id), "w_%s_%03d", p.c_str(), wall_counter++);
            w.id = id;
            w.storey = storey.id;
            if (run.vertical) {
                w.a = {fp.origin.x + run.line * s, fp.origin.y + run.c0 * s};
                w.b = {fp.origin.x + run.line * s, fp.origin.y + run.c1 * s};
                w.side_l = side_name(run.lo);
                w.side_r = side_name(run.hi);
            } else {
                w.a = {fp.origin.x + run.c0 * s, fp.origin.y + run.line * s};
                w.b = {fp.origin.x + run.c1 * s, fp.origin.y + run.line * s};
                w.side_l = side_name(run.hi);
                w.side_r = side_name(run.lo);
            }
            const glm::dvec2 mid = (w.a + w.b) * 0.5;
            const int vi = volume_at(mid);
            const VolRoof* vr = vi >= 0 ? &vroofs[static_cast<size_t>(vi)] : nullptr;
            const double ridge_h =
                vr ? vr->ridge_y - storey.y + 0.1 : storey.h + 2.0;
            if (!is_attic) {
                w.class_code = has_out ? kWallExterior : kWallPartition;
                w.thickness = has_out ? params.ir.t_exterior : params.ir.t_partition;
                w.h = storey.h;
            } else if (has_out && gable_at(vi, mid)) {
                // Gable end (§7.2): full height, clipped by the slopes.
                w.class_code = kWallGable;
                w.thickness = params.ir.t_exterior;
                w.h = ridge_h;
                if (vr) w.top_profile = vr->ceilings;
            } else if (has_out) {
                // Eave side (§5.6): the parapet up to the eave, no clip.
                w.class_code = kWallExterior;
                w.thickness = params.ir.t_exterior;
                w.h = storey.h;
            } else if (has_rv) {
                w.class_code = kWallKnee;
                w.thickness = params.ir.t_knee;
                w.h = params.ir.h_knee + 0.8;
                if (vr) w.top_profile = vr->ceilings;
            } else {
                w.class_code = kWallPartition;
                w.thickness = params.ir.t_partition;
                w.h = ridge_h;
                if (vr) w.top_profile = vr->ceilings;
            }
            // Owner (I9): the room side (the smaller id on a shared wall);
            // a shell wall without a room side belongs to the volume.
            const bool l_room = run.lo >= 0, r_room = run.hi >= 0;
            if (l_room && r_room)
                w.owner = std::min(w.side_l, w.side_r);
            else if (l_room)
                w.owner = side_name(run.lo);
            else if (r_room)
                w.owner = side_name(run.hi);
            else if (vi >= 0)
                w.owner = ir.volumes[static_cast<size_t>(vi)].id;
            else
                w.owner = "main";
            wall_of_run[ri] = static_cast<int>(ir.walls.size());
            ir.walls.push_back(std::move(w));
        }

        // -- Railings (§9.1): the well rim minus the arrival opening ---------
        {
            std::set<int> arrival;
            for (const StairCore& st : bp.stairs)
                if (st.to == k)
                    arrival.insert(st.arrival_cells.begin(), st.arrival_cells.end());
            auto at = [&](int i, int j) {
                return fp.inside(i, j) ? fp.at(i, j) : kCellOutside;
            };
            auto rim_side = [](int32_t c) { return c >= 0 || c == kCellRoofVoid; };
            struct RSeg {
                bool vertical;
                int line, c0, c1;
            };
            std::vector<RSeg> segs;
            auto add_edges = [&](bool vertical, int line, const std::vector<int>& cells) {
                for (size_t a = 0; a < cells.size();) {
                    size_t b = a + 1;
                    while (b < cells.size() && cells[b] == cells[b - 1] + 1) ++b;
                    segs.push_back({vertical, line, cells[a], cells[b - 1] + 1});
                    a = b;
                }
            };
            for (int X = 0; X <= fp.w; ++X) {
                std::vector<int> cells;
                for (int z = 0; z < fp.h; ++z) {
                    const int32_t lo = at(X - 1, z), hi = at(X, z);
                    int other = -1;
                    if (lo == kCellVoid && rim_side(hi)) other = X;      // cell (X, z)
                    if (hi == kCellVoid && rim_side(lo)) other = X - 1;  // cell (X-1, z)
                    if (other >= 0 && other < fp.w && !arrival.count(z * fp.w + other))
                        cells.push_back(z);
                }
                add_edges(true, X, cells);
            }
            for (int Z = 0; Z <= fp.h; ++Z) {
                std::vector<int> cells;
                for (int i = 0; i < fp.w; ++i) {
                    const int32_t lo = at(i, Z - 1), hi = at(i, Z);
                    int other = -1;
                    if (lo == kCellVoid && rim_side(hi)) other = Z;
                    if (hi == kCellVoid && rim_side(lo)) other = Z - 1;
                    if (other >= 0 && other < fp.h &&
                        !arrival.count(other * fp.w + i))
                        cells.push_back(i);
                }
                add_edges(false, Z, cells);
            }
            int rc = 0;
            for (const RSeg& seg : segs) {
                IrRailing r;
                char id[32];
                std::snprintf(id, sizeof(id), "r_%s_%02d", p.c_str(), rc++);
                r.id = id;
                r.storey = storey.id;
                if (seg.vertical) {
                    r.a = {fp.origin.x + seg.line * s, fp.origin.y + seg.c0 * s};
                    r.b = {fp.origin.x + seg.line * s, fp.origin.y + seg.c1 * s};
                } else {
                    r.a = {fp.origin.x + seg.c0 * s, fp.origin.y + seg.line * s};
                    r.b = {fp.origin.x + seg.c1 * s, fp.origin.y + seg.line * s};
                }
                r.h = 0.9;
                ir.railings.push_back(r);
            }
        }

        // -- Openings ---------------------------------------------------------
        int opening_counter = 0;
        std::vector<std::string> emitted_id(ops.openings.size());
        for (size_t oi = 0; oi < ops.openings.size(); ++oi) {
            const DerivedOpening& o = ops.openings[oi];
            const std::string ra = side_name(static_cast<int32_t>(o.room_a));
            const std::string rb =
                o.room_b == -2 ? "outside" : side_name(static_cast<int32_t>(o.room_b));
            int host = -1;
            for (size_t q = 0; q < runs.size(); ++q) {
                if (wall_of_run[q] < 0) continue;
                const B3Run& run = runs[q];
                if (run.vertical != o.vertical || run.line != o.line) continue;
                const double w0 = (run.vertical ? fp.origin.y : fp.origin.x) + run.c0 * s;
                const double w1 = (run.vertical ? fp.origin.y : fp.origin.x) + run.c1 * s;
                if (o.m0 < w0 - 1e-6 || o.m1 > w1 + 1e-6) continue;
                const std::string rl = side_name(run.lo), rh = side_name(run.hi);
                if (!((rl == ra && rh == rb) || (rl == rb && rh == ra))) continue;
                host = static_cast<int>(q);
                break;
            }
            if (host < 0) {
                err = "emit_building_ir: no host wall for an opening of " + ra;
                return false;
            }
            IrWall& wall =
                ir.walls[static_cast<size_t>(wall_of_run[static_cast<size_t>(host)])];
            IrOpening io;
            char id[32];
            std::snprintf(id, sizeof(id), "%s_%s_%03d",
                          o.kind_code == kOpeningDoor ? "d" : "w", p.c_str(),
                          opening_counter++);
            io.id = id;
            emitted_id[oi] = id;
            io.kind_code = o.kind_code;
            io.type = o.type;
            const double w0 = (o.vertical ? wall.a.y : wall.a.x);
            io.s0 = o.m0 - w0;
            io.s1 = o.m1 - w0;
            io.y0 = o.y0;
            io.y1 = o.y1;
            // Attic windows sit lower (§5.6/frozen: sill 0.6, top 2.1) and
            // only ever land on gable walls (the eave sides are roofvoid).
            if (is_attic && o.kind_code == kOpeningWindow) {
                io.y0 = 0.6;
                io.y1 = 0.6 + params.window_h;
            }
            wall.openings.push_back(std::move(io));
        }

        // -- Access edges from the doors (frozen convention: outside first) ---
        for (size_t oi = 0; oi < ops.openings.size(); ++oi) {
            const DerivedOpening& o = ops.openings[oi];
            if (o.kind_code != kOpeningDoor) continue;
            const std::string ra = side_name(static_cast<int32_t>(o.room_a));
            const std::string rb =
                o.room_b == -2 ? "outside" : side_name(static_cast<int32_t>(o.room_b));
            if (ra == "void" || rb == "void") continue;  // no doors into the well
            IrAccessEdge e;
            e.a = rb == "outside" ? rb : std::min(ra, rb);
            e.b = rb == "outside" ? ra : std::max(ra, rb);
            e.via = "door";
            e.ref = emitted_id[oi];
            ir.access.edges.push_back(std::move(e));
        }

        // -- Nodes (the attic has none: its walls are roof-clipped) -----------
        if (!is_attic) {
            struct NodeAcc {
                glm::dvec2 p;
                std::vector<glm::dvec2> dirs;
            };
            std::map<std::pair<long, long>, NodeAcc> node_map;
            auto node_key = [](const glm::dvec2& np) {
                return std::pair{std::lround(np.x / kIrQuant),
                                 std::lround(np.y / kIrQuant)};
            };
            auto add_face = [&](const glm::dvec2& np, const glm::dvec2& dir) {
                NodeAcc& acc = node_map[node_key(np)];
                acc.p = np;
                for (const glm::dvec2& d : acc.dirs)
                    if (d == dir) return;
                acc.dirs.push_back(dir);
            };
            for (size_t wi = wall_begin; wi < ir.walls.size(); ++wi) {
                const IrWall& w = ir.walls[wi];
                glm::dvec2 d = w.b - w.a;
                const double len = glm::length(d);
                if (len < 1e-9) continue;
                d /= len;
                add_face(w.b, d);
                add_face(w.a, -d);
            }
            int node_counter = 0;
            for (auto& [key, acc] : node_map) {
                IrNode node;
                char id[32];
                std::snprintf(id, sizeof(id), "n_%s_%02d", p.c_str(), node_counter++);
                node.id = id;
                node.storey = storey.id;
                node.p = acc.p;
                node.owner = ir.volumes.empty() ? "main" : ir.volumes.front().id;
                const int vi = volume_at(acc.p);
                if (vi >= 0) node.owner = ir.volumes[static_cast<size_t>(vi)].id;
                std::sort(acc.dirs.begin(), acc.dirs.end(),
                          [](const glm::dvec2& da, const glm::dvec2& db) {
                              if (da.x != db.x) return da.x < db.x;
                              return da.y < db.y;
                          });
                for (const glm::dvec2& d : acc.dirs) node.faces.push_back({d, storey.h});
                ir.nodes.push_back(std::move(node));
            }
        }

        // -- Anchors ----------------------------------------------------------
        // Entrance (ground only — the upper subprograms carry no entrance
        // flags): 0.6 m out of the facade, facing the street.
        for (const DerivedOpening& o : ops.openings) {
            if (o.kind_code != kOpeningDoor || o.room_b != -2) continue;
            const PlanRoom& room = fp.rooms[static_cast<size_t>(o.room_a)];
            if (!room.entrance_front || o.vertical || o.line != room.z1) continue;
            IrAnchor a;
            a.kind_code = kAnchorEntrance;
            const double mid = (o.m0 + o.m1) / 2.0;
            const double z = fp.origin.y + o.line * s;
            a.p = glm::dvec3(mid, storey.y, z + 0.6);
            a.dir = glm::dvec3(0.0, 0.0, 1.0);
            a.space = space_id(o.room_a);
            ir.anchors.push_back(a);
        }
        // Hearths (as B2, per storey).
        for (int r = 0; r < N; ++r) {
            const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
            if (room.hearth_code == 0) continue;
            int other = -1;
            for (const auto& [a, b] : ops.adjacency) {
                const int oi = a == r ? b : b == r ? a : -1;
                if (oi >= 0 && fp.rooms[static_cast<size_t>(oi)].hearth_code != 0) {
                    other = oi;
                    break;
                }
            }
            int side = -1;
            if (other >= 0) {
                const PlanRoom& B = fp.rooms[static_cast<size_t>(other)];
                if (room.x1 == B.x0) side = 1;
                else if (room.x0 == B.x1) side = 0;
                else if (room.z1 == B.z0) side = 3;
                else if (room.z0 == B.z1) side = 2;
            }
            if (side < 0) {
                int best_len = 0;
                for (const auto& [a, b] : ops.adjacency) {
                    const int oi = a == r ? b : b == r ? a : -1;
                    if (oi < 0) continue;
                    const PlanRoom& B = fp.rooms[static_cast<size_t>(oi)];
                    if (room.x1 == B.x0 || room.x0 == B.x1) {
                        const int len = std::min(room.z1, B.z1) - std::max(room.z0, B.z0);
                        const int sd = room.x1 == B.x0 ? 1 : 0;
                        if (len > best_len) {
                            best_len = len;
                            side = sd;
                        }
                    } else if (room.z1 == B.z0 || room.z0 == B.z1) {
                        const int len = std::min(room.x1, B.x1) - std::max(room.x0, B.x0);
                        const int sd = room.z1 == B.z0 ? 3 : 2;
                        if (len > best_len) {
                            best_len = len;
                            side = sd;
                        }
                    }
                }
            }
            if (side < 0) side = 2;
            const double wx = side == 0 ? fp.origin.x + room.x0 * s
                              : side == 1 ? fp.origin.x + room.x1 * s
                                          : 0.0;
            const double wz = side == 2 ? fp.origin.y + room.z0 * s
                              : side == 3 ? fp.origin.y + room.z1 * s
                                          : 0.0;
            glm::dvec2 out_n(0.0);
            double along = 0.0;
            if (side == 0) {
                out_n = {-1, 0};
                along = fp.origin.y + (room.z0 + room.z1) * s / 2.0;
            } else if (side == 1) {
                out_n = {1, 0};
                along = fp.origin.y + (room.z0 + room.z1) * s / 2.0;
            } else if (side == 2) {
                out_n = {0, -1};
                along = fp.origin.x + (room.x0 + room.x1) * s / 2.0;
            } else {
                out_n = {0, 1};
                along = fp.origin.x + (room.x0 + room.x1) * s / 2.0;
            }
            if (other >= 0) {
                const PlanRoom& B = fp.rooms[static_cast<size_t>(other)];
                if (side < 2) {
                    const double za = fp.origin.y + std::max(room.z0, B.z0) * s;
                    const double zb = fp.origin.y + std::min(room.z1, B.z1) * s;
                    along = (za + zb) / 2.0;
                } else {
                    const double xa = fp.origin.x + std::max(room.x0, B.x0) * s;
                    const double xb = fp.origin.x + std::min(room.x1, B.x1) * s;
                    along = (xa + xb) / 2.0;
                }
            }
            IrAnchor a;
            a.kind_code = kAnchorHearth;
            const glm::dvec2 wall_p =
                side < 2 ? glm::dvec2(wx, along) : glm::dvec2(along, wz);
            a.p = glm::dvec3(wall_p.x - out_n.x * 0.9, storey.y, wall_p.y - out_n.y * 0.9);
            a.dir = glm::dvec3(-out_n.x, 0.0, -out_n.y);
            a.space = space_id(r);
            ir.anchors.push_back(a);
        }
        // Bed slots (as B2, per storey).
        for (int r = 0; r < N; ++r) {
            const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
            if (room.fn_code != kFnBedroom) continue;
            int side = -1;
            for (int sd : {0, 1, 2, 3}) {
                bool exterior = false;
                if (sd == 0)
                    for (int j = room.z0; j < room.z1 && !exterior; ++j)
                        exterior =
                            !fp.inside(room.x0 - 1, j) || fp.at(room.x0 - 1, j) == kCellOutside;
                if (sd == 1)
                    for (int j = room.z0; j < room.z1 && !exterior; ++j)
                        exterior =
                            !fp.inside(room.x1, j) || fp.at(room.x1, j) == kCellOutside;
                if (sd == 2)
                    for (int i = room.x0; i < room.x1 && !exterior; ++i)
                        exterior =
                            !fp.inside(i, room.z0 - 1) || fp.at(i, room.z0 - 1) == kCellOutside;
                if (sd == 3)
                    for (int i = room.x0; i < room.x1 && !exterior; ++i)
                        exterior =
                            !fp.inside(i, room.z1) || fp.at(i, room.z1) == kCellOutside;
                if (exterior) {
                    side = sd;
                    break;
                }
            }
            if (side < 0) side = 0;
            IrAnchor a;
            a.kind_code = kAnchorBedSlot;
            glm::dvec2 in_n(0.0);
            glm::dvec2 base(0.0);
            const double zm = fp.origin.y + (room.z0 + room.z1) * s / 2.0;
            const double xm = fp.origin.x + (room.x0 + room.x1) * s / 2.0;
            if (side == 0) {
                in_n = {1, 0};
                base = {fp.origin.x + room.x0 * s, zm};
            } else if (side == 1) {
                in_n = {-1, 0};
                base = {fp.origin.x + room.x1 * s, zm};
            } else if (side == 2) {
                in_n = {0, 1};
                base = {xm, fp.origin.y + room.z0 * s};
            } else {
                in_n = {0, -1};
                base = {xm, fp.origin.y + room.z1 * s};
            }
            a.p = glm::dvec3(base.x + in_n.x * 1.0, storey.y, base.y + in_n.y * 1.0);
            a.dir = glm::dvec3(in_n.x, 0.0, in_n.y);
            a.space = space_id(r);
            ir.anchors.push_back(a);
        }

        // -- Report (per storey, prefixed) ------------------------------------
        for (const auto& [name, v] : pr.cost.terms) ir.report_cost[p + "_" + name] = v;
        for (const auto& [name, cnt] : pr.cost.hard_terms)
            ir.report_cost["hard_" + p + "_" + name] = static_cast<double>(cnt);
        hard_sum += pr.cost.hard;
        soft_sum += pr.cost.soft;
        for (const std::string& line : pr.anneal.log) ir.report_moves.push_back(p + ": " + line);
    }

    // -- Slabs (§7.8): per storey gap, the above field minus the wells. ------
    for (int k = 0; k + 1 < nst; ++k) {
        const IrStorey& below = ir.storeys[static_cast<size_t>(k)];
        if (below.slab_t <= 0.0) continue;
        // The roof void keeps its floor (§5.6); the stair well does not.
        std::vector<int32_t> m = bp.fields.mask[static_cast<size_t>(k + 1)];
        for (int32_t& c : m)
            if (c == kCellRoofVoid) c = kCellFree;
        IrSlab slab;
        slab.storey_below = below.id;
        slab.storey_above = ir.storeys[static_cast<size_t>(k + 1)].id;
        slab.y = below.y + below.h;
        slab.t = below.slab_t;
        for (const auto& r : decompose_cells(m, bp.fields.w, bp.fields.h))
            slab.rects.push_back({bp.fields.origin.x + r[0] * s, bp.fields.origin.y + r[1] * s,
                                  bp.fields.origin.x + r[2] * s, bp.fields.origin.y + r[3] * s});
        ir.slabs.push_back(std::move(slab));
    }

    // -- Stairs + the stair access edges (§9.1) --------------------------------
    for (const StairCore& st : bp.stairs) {
        IrStair is;
        is.id = st.id;
        is.type_code = kStairL;
        is.from = ir.storeys[static_cast<size_t>(st.from)].id;
        is.to = ir.storeys[static_cast<size_t>(st.to)].id;
        is.start = st.start;
        is.dir = st.dir;
        is.dir2 = st.dir2;
        is.width = st.width;
        is.risers = st.risers;
        is.rise = st.rise;
        is.going = st.going;
        is.risers_low = st.risers_low;
        is.landing_min = {st.landing.x0, st.landing.z0};
        is.landing_max = {st.landing.x1, st.landing.z1};
        for (const IrSlabRect& wr : st.well_rects) is.well.push_back(wr);
        ir.stairs.push_back(std::move(is));

        IrAccessEdge e;
        e.a = prefix_of(st.from) + "_" + st.shaft_room.id;
        e.via = "stair";
        e.ref = st.id;
        const FloorPlan& to = bp.storeys[static_cast<size_t>(st.to)].plan;
        std::set<int> arrivals;
        for (const int cell : st.arrival_cells) {
            const int32_t r = to.at(cell % to.w, cell / to.w);
            if (r >= 0) arrivals.insert(r);
        }
        for (const int r : arrivals) {
            e.b = prefix_of(st.to) + "_" + to.rooms[static_cast<size_t>(r)].id;
            ir.access.edges.push_back(e);
        }
    }

    ir.report_cost["hard"] = hard_sum;
    ir.report_cost["soft"] = soft_sum;
    (void)prog;
    return true;
}

}  // namespace building_topology_generator
