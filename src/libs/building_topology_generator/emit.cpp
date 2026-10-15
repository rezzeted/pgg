// G8 emit (B2). See emit.h.

#include "pch.h"

#include "emit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "codes.h"
#include "roof.h"  // point_in_poly

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
        for (int j = room.z0; j < room.z1; ++j)
            for (int i = room.x0; i < room.x1; ++i) sp.cells.emplace_back(i, j);
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
        sp.outline = {{xa, za}, {xa, zb}, {xb, zb}, {xb, za}};
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

}  // namespace building_topology_generator
