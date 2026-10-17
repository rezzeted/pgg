// Chimney stack placement (stage G4b). See chimneys.h.

#include "pch.h"

#include "chimneys.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "codes.h"
#include "roof.h"

namespace building_topology_generator {

namespace {

constexpr int kArchCentral = 1;
constexpr int kArchGable = 2;
constexpr int kArchFree = 3;

constexpr double kGablePitch = 89.999;  // as roof.cpp
constexpr double kEps = 1e-6;

double dist_pt_seg(const glm::dvec2& p, const glm::dvec2& a, const glm::dvec2& b) {
    const glm::dvec2 ab = b - a;
    const double l2 = glm::dot(ab, ab);
    const double t = l2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    return glm::length(p - (a + ab * t));
}

// Per-volume roof model + the volume's base index in Ir::roof.planes (the
// order massing_to_ir appended them).
struct VolCtx {
    const IrVolume* vol = nullptr;
    RoofModel model;
    int plane_base = 0;
};

bool build_vol_ctxs(const Ir& ir, std::vector<VolCtx>& out, std::string& err) {
    out.clear();
    int base = 0;
    for (const IrVolume& v : ir.volumes) {
        VolCtx c;
        c.vol = &v;
        if (!build_roof_model(v, c.model, err)) {
            err = "place_chimneys: volume " + v.id + ": " + err;
            return false;
        }
        c.plane_base = base;
        base += static_cast<int>(c.model.planes.size());
        out.push_back(std::move(c));
    }
    return true;
}

// Debug-only reject tally (BTG_DEBUG_DUMP): why candidates die, per scan.
struct RejectStats {
    size_t bounds = 0, volume = 0, roof = 0, valley = 0, arch = 0, busy = 0,
           shaft = 0, arrival = 0, disconnect = 0, dead = 0, reslice = 0,
           seeds = 0, viable = 0, corr = 0, tried = 0;
    void reset() { *this = RejectStats{}; }
};
RejectStats g_rej;
// BTG_TRACE_STACK="x,z;x,z": trace one candidate's gate rejections.
std::vector<glm::ivec2> g_trace_cells;
bool g_trace_on = false;
bool rej(size_t& c) {
    ++c;
    if (g_trace_on) {
        static const char* names[] = {"bounds",  "volume", "roof",   "valley", "arch",
                                      "busy",    "shaft",  "arrival", "disc",  "dead",
                                      "reslice", "seeds",  "viable",  "corr",  "tried"};
        const size_t idx = static_cast<size_t>(&c - &g_rej.bounds);
        std::fprintf(stderr, "trace: rej %s\n",
                     idx < sizeof(names) / sizeof(names[0]) ? names[idx] : "?");
    }
    return false;
}

// The global Ir::roof.planes index of the sloped skeleton face containing
// (x, z); -1 when the point is under no slope (or under a face whose plane
// was degenerate). Same face scan as roof_y_at, so the two agree.
int exit_plane_at(const VolCtx& c, double x, double z) {
    const RoofModel& m = c.model;
    const size_t n = m.sweep_outline.size();
    for (const geom2d::SkeletonFace& f : m.sk.faces) {
        const int e = f.edge;
        if (e < 0 || static_cast<size_t>(e) >= n) continue;
        if (m.pitch_deg[static_cast<size_t>(e)] >= kGablePitch) continue;
        std::vector<glm::dvec2> poly;
        poly.reserve(f.nodes.size());
        for (const int32_t idx : f.nodes)
            poly.push_back(glm::dvec2(m.sk.nodes[static_cast<size_t>(idx)].p));
        if (!point_in_poly(poly, x, z)) continue;
        for (size_t pi = 0; pi < m.planes.size(); ++pi)
            if (m.planes[pi].edge == e) return c.plane_base + static_cast<int>(pi);
        return -1;  // the face emitted no plane (a degenerate normal)
    }
    return -1;
}

// True when a stair core occupies the cell on storey k (the pinned shaft on
// the from-storey; the arrival is plain floor and may neighbor a hearth).
bool shaft_covers(const std::vector<StairCore>& stairs, int k, int i, int j) {
    for (const StairCore& st : stairs) {
        if (st.from != k) continue;
        const PlanRoom& r = st.shaft_room;
        if (i >= r.x0 && i < r.x1 && j >= r.z0 && j < r.z1) return true;
    }
    return false;
}

// Free 4-neighbors of `cells` on storey k (the G5 seed set of a hearth room).
std::vector<glm::ivec2> seed_cells(const StoreyFields& fields, int k,
                                   const std::vector<StairCore>& stairs,
                                   const std::vector<glm::ivec2>& cells) {
    const std::vector<int32_t>& mask = fields.mask[static_cast<size_t>(k)];
    const int w = fields.w, h = fields.h;
    std::vector<glm::ivec2> out;
    auto push = [&](int i, int j) {
        if (i < 0 || i >= w || j < 0 || j >= h) return;
        if (mask[static_cast<size_t>(j) * w + i] != kCellFree) return;
        if (shaft_covers(stairs, k, i, j)) return;
        for (const glm::ivec2& c : cells)
            if (c.x == i && c.y == j) return;  // the stack's own cell
        for (const glm::ivec2& c : out)
            if (c.x == i && c.y == j) return;
        out.emplace_back(i, j);
    };
    for (const glm::ivec2& c : cells) {
        push(c.x - 1, c.y);
        push(c.x + 1, c.y);
        push(c.x, c.y - 1);
        push(c.x, c.y + 1);
    }
    return out;
}

// Greedy match of the group's rooms (the largest min_dim first) onto distinct
// seed cells whose CONTAINING rect piece nets at least the room's min_dim —
// mirrors the G5 repair's piece binding (one room, one piece; a guillotine
// slice spans the piece's full side): a room whose seed sits in a thinner
// piece is re-homed by the repair and loses its stack contact.
bool seed_match(const std::vector<double>& piece_net, int w,
                const std::vector<glm::ivec2>& seeds, const std::vector<double>& min_dims) {
    std::vector<double> md = min_dims;
    std::sort(md.rbegin(), md.rend());
    std::vector<char> used(seeds.size(), 0);
    for (const double m : md) {
        int pick = -1;
        for (size_t q = 0; q < seeds.size(); ++q)
            if (!used[q] && piece_net[static_cast<size_t>(seeds[q].y) * w +
                                      static_cast<size_t>(seeds[q].x)] >= m - kEps) {
                pick = static_cast<int>(q);
                break;
            }
        if (pick < 0) return false;
        used[static_cast<size_t>(pick)] = 1;
    }
    return true;
}

// Seeded guillotine feasibility of one piece: every room's slice must contain
// its seed cell — the G5 repair binds a hearth room to the stack's piece, and
// a slice missing the seed cell loses the stack contact (hard.hearth). The
// cut rule mirrors rect_set_feasible (net min_dim per side); the pins only
// restrict the cut order: a cut on an axis is valid when the two sides' pins
// are strictly separable along it. rooms indexes (mds, pins).
bool seeded_rect_rec(const std::vector<int32_t>& margins, int w, int h,
                     const IrParams& ir, int x0, int z0, int x1, int z1,
                     const std::vector<double>& mds,
                     const std::vector<glm::ivec2>& pins, const std::vector<int>& rooms) {
    if (rooms.empty()) return true;
    double t_x0, t_x1, t_z0, t_z1;
    field_side_margins(margins, w, h, ir, x0, z0, x1, z1, t_x0, t_x1, t_z0, t_z1);
    if (rooms.size() == 1) {
        const double net_w = (x1 - x0) * ir.cell - t_x0 - t_x1;
        const double net_h = (z1 - z0) * ir.cell - t_z0 - t_z1;
        return std::min(net_w, net_h) >= mds[static_cast<size_t>(rooms[0])] - kEps;
    }
    for (int axis = 0; axis < 2; ++axis) {
        const bool vert = axis == 0;
        std::vector<int> rs = rooms;
        std::sort(rs.begin(), rs.end(), [&](int a, int b) {
            return vert ? pins[static_cast<size_t>(a)].x < pins[static_cast<size_t>(b)].x
                        : pins[static_cast<size_t>(a)].y < pins[static_cast<size_t>(b)].y;
        });
        // The low side of a cut is a prefix of the pin order; valid only when
        // the pins are strictly separable there (pin[p] < pin[p+1]).
        for (size_t p = 0; p + 1 < rs.size(); ++p) {
            const int pin_lo = vert ? pins[static_cast<size_t>(rs[p])].x
                                    : pins[static_cast<size_t>(rs[p])].y;
            const int pin_hi = vert ? pins[static_cast<size_t>(rs[p + 1])].x
                                    : pins[static_cast<size_t>(rs[p + 1])].y;
            if (pin_lo >= pin_hi) continue;
            const double t_out_lo = vert ? t_x0 : t_z0;
            const double t_out_hi = vert ? t_x1 : t_z1;
            auto side_cells = [&](const std::vector<int>& side, double t_out) {
                int m = 1;
                for (const int r : side) {
                    const double need =
                        mds[static_cast<size_t>(r)] + t_out + ir.t_partition / 2;
                    m = std::max(m,
                                 static_cast<int>(std::ceil(need / ir.cell - kEps)));
                }
                return m;
            };
            const std::vector<int> lo_rooms(rs.begin(), rs.begin() + p + 1);
            const std::vector<int> hi_rooms(rs.begin() + p + 1, rs.end());
            const int lo = vert ? x0 : z0, hi = vert ? x1 : z1;
            const int m_lo = side_cells(lo_rooms, t_out_lo);
            const int m_hi = side_cells(hi_rooms, t_out_hi);
            // The cut c in cells: the low side [lo, c) must contain every low
            // pin (c > pin_lo), the high side every high pin (c <= pin_hi).
            const int c_lo = std::max(lo + m_lo, pin_lo + 1);
            const int c_hi = std::min(hi - m_hi, pin_hi);
            if (c_lo > c_hi) continue;
            // Recurse at the two extremes: the low side prefers c_hi, the
            // high side c_lo.
            for (const int c : {c_lo, c_hi}) {
                const int lx1 = vert ? c : x1, lz1 = vert ? z1 : c;
                const int hx0 = vert ? c : x0, hz0 = vert ? z0 : c;
                if (seeded_rect_rec(margins, w, h, ir, x0, z0, lx1, lz1, mds, pins,
                                    lo_rooms) &&
                    seeded_rect_rec(margins, w, h, ir, hx0, hz0, x1, z1, mds, pins,
                                    hi_rooms))
                    return true;
            }
        }
    }
    return false;
}

// Pair frontage gate (B4 acceptance): the group's rooms must end up 4-touching
// the stack. Each room takes a distinct seed cell (its future stack contact);
// the rooms landing in one piece must stay seeded-guillotine-feasible there —
// two rooms CAN share one stack side when the piece splits into two
// min_dim-legal slices each containing its contact cell (the back-to-back
// pair), but a boundary-hugging 1x2 stack whose contact cells sit 1 m apart
// can host only one min_dim room. Groups are at most a pair in v1 (a join may
// ask for three); the seeds are few, so a plain backtracking match is enough.
bool pair_frontage_ok(const std::vector<int>& piece_id,
                      const std::vector<std::array<int, 4>>& piece_rects,
                      const std::vector<int32_t>& margins, int w, int h,
                      const IrParams& ir, const std::vector<glm::ivec2>& seeds,
                      const std::vector<double>& min_dims) {
    std::vector<size_t> ord(min_dims.size());
    for (size_t q = 0; q < ord.size(); ++q) ord[q] = q;
    std::sort(ord.begin(), ord.end(),
              [&](size_t a, size_t b) { return min_dims[a] > min_dims[b]; });
    std::vector<char> used_seed(seeds.size(), 0);
    std::vector<glm::ivec2> pin_of(min_dims.size());
    std::vector<std::vector<int>> by_piece(piece_rects.size());
    const auto piece_ok = [&](int p) {
        std::vector<double> mds;
        std::vector<glm::ivec2> pins;
        std::vector<int> rooms;
        for (const int r : by_piece[static_cast<size_t>(p)]) {
            mds.push_back(min_dims[static_cast<size_t>(r)]);
            pins.push_back(pin_of[static_cast<size_t>(r)]);
            rooms.push_back(static_cast<int>(mds.size() - 1));
        }
        const std::array<int, 4>& rc = piece_rects[static_cast<size_t>(p)];
        return seeded_rect_rec(margins, w, h, ir, rc[0], rc[1], rc[2], rc[3], mds, pins,
                               rooms);
    };
    const auto go = [&](auto&& self, size_t ri) -> bool {
        if (ri == ord.size()) return true;
        const size_t room = ord[ri];
        for (size_t si = 0; si < seeds.size(); ++si) {
            if (used_seed[si]) continue;
            const int p =
                piece_id[static_cast<size_t>(seeds[si].y) * w + seeds[si].x];
            if (p < 0) continue;
            used_seed[si] = 1;
            pin_of[room] = seeds[si];
            by_piece[static_cast<size_t>(p)].push_back(static_cast<int>(room));
            // The piece with the new room must stay feasible (prune); the
            // other pieces were checked at their own assignments.
            if (piece_ok(p) && self(self, ri + 1)) return true;
            by_piece[static_cast<size_t>(p)].pop_back();
            used_seed[si] = 0;
        }
        return false;
    };
    return go(go, 0);
}

// The storey-k0 seed environment of an already-placed stack (the join path):
// piece nets and seed cells as the growth will see them — the field with the
// pinned shafts stamped out and the stack's own cells as the core.
void stack_seed_env(const StoreyFields& fields, const std::vector<StairCore>& stairs,
                    const PlanParams& params, int k0,
                    const std::vector<glm::ivec2>& cells, std::vector<double>& piece_net,
                    std::vector<int>& piece_id, std::vector<double>& piece_net_x,
                    std::vector<double>& piece_net_z,
                    std::vector<std::array<int, 4>>& piece_rects,
                    std::vector<int32_t>& margins_out, std::vector<glm::ivec2>& seeds) {
    const int w = fields.w, h = fields.h;
    std::vector<int32_t> m2 = fields.mask[static_cast<size_t>(k0)];
    std::vector<int32_t> margins = m2;
    for (const StairCore& st : stairs) {
        if (st.from != k0) continue;
        const PlanRoom& r = st.shaft_room;
        for (int j = r.z0; j < r.z1; ++j)
            for (int i = r.x0; i < r.x1; ++i)
                m2[static_cast<size_t>(j) * w + i] = kCellCore;
    }
    for (const glm::ivec2& c : cells) {
        m2[static_cast<size_t>(c.y) * w + c.x] = kCellCore;
        margins[static_cast<size_t>(c.y) * w + c.x] = kCellCore;
    }
    field_piece_ids(m2, margins, w, h, params.ir, piece_id, piece_net_x, piece_net_z,
                    piece_rects);
    piece_net.assign(static_cast<size_t>(w) * h, 0.0);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int p = piece_id[static_cast<size_t>(j) * w + i];
            if (p >= 0)
                piece_net[static_cast<size_t>(j) * w + i] =
                    std::min(piece_net_x[static_cast<size_t>(p)],
                             piece_net_z[static_cast<size_t>(p)]);
        }
    margins_out = std::move(margins);
    seeds = seed_cells(fields, k0, stairs, cells);
}

struct Cand {
    std::vector<glm::ivec2> cells;
    glm::dvec2 center{0.0};  // building frame, meters
    double top_y = 0.0;
    int roof_plane = -1;
    int top_storey = 0;
    double score = 0.0;
};

// The §5.7 exit feasibility + the score of one cell set. arch gates the
// position class on the hearth storey k0 (central: the 1.5 m ridge band, no
// exterior contact; gable: a boundary cell against a ~90 deg outline edge;
// free: anything). need_seeds: how many free neighbor cells the group's rooms
// need for the G5 seeding (1 for a solo hearth, 2 for a pair); min_dims: the
// group rooms' min_dim values for the seed-viability gate (a room seeded on
// a cell whose best all-free square nets under its min_dim is trapped in a
// wing-only strip). storey_rooms: per-storey (min_dim, target) of every room
// the G5 repair will have to host (required + hearth optionals + the
// auto-corridor) — the re-slice gate rejects stacks that leave no guillotine
// assignment on any crossed storey (the repair would fail hard on min_dim).
// corridor_auto: per-storey flag — the pipeline will inject an auto-corridor
// seeded at the shaft's door-ok sides (never relaxed); the candidate must
// leave such a storey at least one seed cell in a min_dim-legal piece.
bool eval_candidate(const Ir& ir, const StoreyFields& fields,
                    const std::vector<StairCore>& stairs, const PlanParams& params,
                    const std::vector<VolCtx>& ctxs, const std::vector<glm::ivec2>& cells,
                    int k0, int need_seeds, const std::vector<double>& min_dims,
                    const std::vector<std::vector<std::pair<double, double>>>& storey_rooms,
                    const std::vector<char>& corridor_auto,
                    const std::vector<glm::ivec2>& reserved_seeds, int arch, Rng& rng,
                    Cand& out) {
    const int w = fields.w, h = fields.h;
    const double s = params.ir.cell;
    // BTG_TRACE_STACK="x,z;x,z" traces one candidate through the gates.
    if (!g_trace_cells.empty()) {
        g_trace_on = normalize_stack_cells(g_trace_cells) == normalize_stack_cells(cells);
        if (g_trace_on) std::fprintf(stderr, "trace: candidate enters eval\n");
    }
    int i0 = w, j0 = h, i1 = -1, j1 = -1;
    for (const glm::ivec2& c : cells) {
        if (c.x < 0 || c.x >= w || c.y < 0 || c.y >= h) return rej(g_rej.bounds);
        // Sitting on an earlier stack's frontage kills that stack's seed
        // cell — the frontage is reserved, seed cells and stack cells alike.
        if (std::find(reserved_seeds.begin(), reserved_seeds.end(), c) !=
            reserved_seeds.end())
            return rej(g_rej.seeds);
        i0 = std::min(i0, c.x);
        j0 = std::min(j0, c.y);
        i1 = std::max(i1, c.x + 1);
        j1 = std::max(j1, c.y + 1);
    }
    // The footprint corners pin the volume (the field edge sits t_ext/2
    // inside the outline, so a stack at the boundary is still inside; a set
    // straddling a volume boundary is rejected).
    const double fx0 = fields.origin.x + i0 * s, fx1 = fields.origin.x + i1 * s;
    const double fz0 = fields.origin.y + j0 * s, fz1 = fields.origin.y + j1 * s;
    const glm::dvec2 corners[4] = {{fx0, fz0}, {fx0, fz1}, {fx1, fz1}, {fx1, fz0}};
    int vi = -1;
    for (size_t q = 0; q < ir.volumes.size(); ++q) {
        bool all = true;
        for (const glm::dvec2& p : corners)
            all = all && point_in_poly(ir.volumes[q].outline, p.x, p.y);
        if (all) {
            if (vi >= 0) return rej(g_rej.volume);  // overlapping volumes: ambiguous
            vi = static_cast<int>(q);
        }
    }
    if (vi < 0) return rej(g_rej.volume);
    const VolCtx& ctx = ctxs[static_cast<size_t>(vi)];
    const glm::dvec2 center{(fx0 + fx1) * 0.5, (fz0 + fz1) * 0.5};

    // The exit (§5.7): the slope face over the center, the ridge distance,
    // the valley/hip margin from every footprint corner, the required top.
    double exit_y = 0.0;
    if (!roof_y_at(ctx.model, center.x, center.y, exit_y)) return rej(g_rej.roof);
    const int plane = exit_plane_at(ctx, center.x, center.y);
    if (plane < 0) return rej(g_rej.roof);
    double ridge_d = 0.0, ridge_y = 0.0;
    if (!nearest_ridge(ctx.model, center.x, center.y, ridge_d, ridge_y))
        return rej(g_rej.roof);
    for (const glm::dvec2& p : corners) {
        for (const IrRoofEdge& e : ctx.model.valleys)
            if (dist_pt_seg(p, {e.a.x, e.a.z}, {e.b.x, e.b.z}) <
                params.ir.valley_margin - 1e-9)
                return rej(g_rej.valley);
        for (const IrRoofEdge& e : ctx.model.hips)
            if (dist_pt_seg(p, {e.a.x, e.a.z}, {e.b.x, e.b.z}) <
                params.ir.valley_margin - 1e-9)
                return rej(g_rej.valley);
    }
    const double top_y =
        chimney_top_required(ridge_d, ridge_y, exit_y, params.ir.h_above_roof);

    // The archetype gate (the position class on the hearth storey's field).
    const std::vector<int32_t>& mask0 = fields.mask[static_cast<size_t>(k0)];
    auto outside_neighbor = [&](const glm::ivec2& c) {
        static const int di[4] = {-1, 1, 0, 0};
        static const int dj[4] = {0, 0, -1, 1};
        for (int q = 0; q < 4; ++q) {
            const int ni = c.x + di[q], nj = c.y + dj[q];
            if (ni < 0 || ni >= w || nj < 0 || nj >= h) return true;
            if (mask0[static_cast<size_t>(nj) * w + ni] == kCellOutside) return true;
        }
        return false;
    };
    if (arch == kArchCentral) {
        if (ridge_d > 1.5 + 1e-9) return rej(g_rej.arch);  // the ridge band (§5.7)
        for (const glm::ivec2& c : cells)
            if (outside_neighbor(c)) return rej(g_rej.arch);  // not at an exterior wall
    } else if (arch == kArchGable) {
        bool gable = false;
        for (const glm::ivec2& c : cells) {
            if (gable) break;
            if (!outside_neighbor(c)) continue;
            // The outline edge nearest the cell center must be a gable (90°).
            const glm::dvec2 cc{fields.origin.x + (c.x + 0.5) * s,
                                fields.origin.y + (c.y + 0.5) * s};
            const size_t ne = ctx.vol->outline.size();
            double best_d = 1e30;
            int be = -1;
            for (size_t e = 0; e < ne; ++e) {
                const double d =
                    dist_pt_seg(cc, ctx.vol->outline[e], ctx.vol->outline[(e + 1) % ne]);
                if (d < best_d) {
                    best_d = d;
                    be = static_cast<int>(e);
                }
            }
            if (be >= 0 && be < static_cast<int>(ctx.vol->roof.pitch_by_edge.size()) &&
                ctx.vol->roof.pitch_by_edge[static_cast<size_t>(be)] >= 89.0)
                gable = true;
        }
        if (!gable) return rej(g_rej.arch);
    }

    // Per-storey feasibility, from the ground (support) to the volume top.
    // The field is evaluated as the growth sees it: the pinned stair shafts
    // of the storey are stamped out too (a shaft is kCellFree in the mask but
    // the G5 repair tiles around it — without it a neck pinched between the
    // shaft and the candidate slips past dead_piece_near).
    const int k_top = ctx.vol->storey1;
    std::vector<int> flat;
    flat.reserve(cells.size());
    for (const glm::ivec2& c : cells) flat.push_back(c.y * w + c.x);
    double frag = 0.0;
    std::vector<int32_t> m2_k0, margins_k0;  // captured on the hearth storey
    for (int k = 0; k <= k_top; ++k) {
        const bool attic = ir.storeys[static_cast<size_t>(k)].kind_code == kStoreyAttic;
        const std::vector<int32_t>& mask = fields.mask[static_cast<size_t>(k)];
        for (int f : flat) {
            const int32_t lbl = mask[static_cast<size_t>(f)];
            if (lbl != kCellFree && !(attic && lbl == kCellRoofVoid))
                return rej(g_rej.busy);
            if (shaft_covers(stairs, k, f % w, f / w)) return rej(g_rej.shaft);
        }
        // The arrival row is plain floor, but a stack over the stair's open
        // edge would wall off the exit — keep it clear.
        for (const StairCore& st : stairs)
            if (st.to == k)
                for (const int a : st.arrival_cells)
                    for (const int f : flat)
                        if (a == f) return rej(g_rej.arrival);
        // The growth-time field: the mask minus the pinned shafts.
        std::vector<int32_t> base = mask;
        std::vector<int> blocked = flat;
        for (const StairCore& st : stairs) {
            if (st.from != k) continue;
            const PlanRoom& r = st.shaft_room;
            for (int j = r.z0; j < r.z1; ++j)
                for (int i = r.x0; i < r.x1; ++i) {
                    base[static_cast<size_t>(j) * w + i] = kCellCore;
                    blocked.push_back(j * w + i);
                }
        }
        if (!field_connected_without(base, w, h, blocked)) return rej(g_rej.disconnect);
        std::vector<int32_t> m2 = base;
        for (const int f : flat) m2[static_cast<size_t>(f)] = kCellCore;
        if (dead_piece_near(m2, w, h, flat)) return rej(g_rej.dead);
        // Margins as the growth sees them: the shaft cells stay kCellFree
        // (a partition wall shaves the piece), only the candidate is core.
        std::vector<int32_t> margins_k = mask;
        for (const int f : flat) margins_k[static_cast<size_t>(f)] = kCellCore;
        // The re-slice gate: the storey must stay guillotine-feasible for its
        // rooms — a mid-band stack that strips the field under some room's
        // min_dim makes the G5 repair fail hard (no assignment exists).
        if (k < static_cast<int>(storey_rooms.size()) &&
            !field_reslice_feasible(m2, margins_k, w, h, params.ir,
                                    storey_rooms[static_cast<size_t>(k)]))
            return rej(g_rej.reslice);
        // The auto-corridor gate: the pipeline seeds the injected corridor at
        // the shaft's door-ok sides (seed_next_to, never relaxed) — the
        // candidate must leave at least one such seed cell in a piece netting
        // the corridor's min_dim, else the storey fails at seeding outright.
        if (k < static_cast<int>(corridor_auto.size()) &&
            corridor_auto[static_cast<size_t>(k)]) {
            const std::vector<glm::ivec2> cseeds =
                corridor_seed_cells(m2, w, h, stairs, k);
            const double cmd = fn_defaults(kFnCorridor).min_dim;
            std::vector<int> cids;
            std::vector<double> cnx, cnz;
            std::vector<std::array<int, 4>> crects;
            field_piece_ids(m2, margins_k, w, h, params.ir, cids, cnx, cnz, crects);
            bool any = false;
            for (const glm::ivec2& c : cseeds) {
                const int p = cids[static_cast<size_t>(c.y) * w + c.x];
                if (p >= 0 && std::min(cnx[static_cast<size_t>(p)],
                                       cnz[static_cast<size_t>(p)]) >= cmd - kEps) {
                    any = true;
                    break;
                }
            }
            if (!any) return rej(g_rej.corr);
        }
        if (k == k0) {
            m2_k0 = m2;
            margins_k0 = std::move(margins_k);
        }
        const auto before = tiled_piece_stats(base, w, h);
        const auto after = tiled_piece_stats(m2, w, h);
        frag += 4.0 * std::max(0, after.first - before.first) +
                2.0 * std::max(0, after.second - before.second);
    }

    // The hearth storey must still offer seed cells for the group's rooms.
    // Cells claimed by an earlier stack's frontage (its free 4-neighbors) are
    // reserved: two stacks whose seed neighborhoods overlap send both rooms
    // after the same cells, and the G5 seeding then fails one of them.
    std::vector<glm::ivec2> seeds = seed_cells(fields, k0, stairs, cells);
    if (!reserved_seeds.empty())
        seeds.erase(std::remove_if(seeds.begin(), seeds.end(),
                                   [&](const glm::ivec2& c) {
                                       return std::find(reserved_seeds.begin(),
                                                        reserved_seeds.end(),
                                                        c) != reserved_seeds.end();
                                   }),
                    seeds.end());
    if (static_cast<int>(seeds.size()) < need_seeds) return rej(g_rej.seeds);

    // Seed viability (B4 acceptance): mirror the G5 repair's piece binding —
    // a room seeded in a piece whose net min side is under its min_dim gets
    // re-homed by the repair and loses the stack contact. Match the group's
    // rooms (the largest min_dim first) onto distinct viable seed cells; no
    // match -> the stack placement is rejected.
    std::vector<double> piece_net;
    std::vector<int> piece_id;
    std::vector<double> piece_net_x, piece_net_z;
    std::vector<std::array<int, 4>> piece_rects;
    if (!min_dims.empty()) {
        if (m2_k0.empty()) return rej(g_rej.viable);  // k0 outside the volume span
        field_piece_ids(m2_k0, margins_k0, w, h, params.ir, piece_id, piece_net_x,
                        piece_net_z, piece_rects);
        piece_net.assign(static_cast<size_t>(w) * h, 0.0);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const int p = piece_id[static_cast<size_t>(j) * w + i];
                if (p >= 0)
                    piece_net[static_cast<size_t>(j) * w + i] =
                        std::min(piece_net_x[static_cast<size_t>(p)],
                                 piece_net_z[static_cast<size_t>(p)]);
            }
        if (!seed_match(piece_net, w, seeds, min_dims)) return rej(g_rej.viable);
    }

    // Score: the §9.2 height term (the excess over the local roof minimum —
    // the critic pulling the stack toward the ridge), the fragmentation
    // penalty, a flanking bonus (the pair wants two sides to sit on), jitter.
    const double excess = top_y - (exit_y + params.ir.h_above_roof);
    auto side_free = [&](int i, int j) {
        return i >= 0 && i < w && j >= 0 && j < h &&
               mask0[static_cast<size_t>(j) * w + i] == kCellFree &&
               !shaft_covers(stairs, k0, i, j);
    };
    bool sx0 = false, sx1 = false, sz0 = false, sz1 = false;
    for (int j = j0; j < j1; ++j) {
        sx0 = sx0 || side_free(i0 - 1, j);
        sx1 = sx1 || side_free(i1, j);
    }
    for (int i = i0; i < i1; ++i) {
        sz0 = sz0 || side_free(i, j0 - 1);
        sz1 = sz1 || side_free(i, j1);
    }
    const int sides = (sx0 ? 1 : 0) + (sx1 ? 1 : 0) + (sz0 ? 1 : 0) + (sz1 ? 1 : 0);

    // Frontage gate (B4 acceptance): each hearth room of a pair must end up
    // 4-touching the stack. Rooms may share one stack side when their piece
    // splits along its long side into strips that each clear min_dim (the
    // back-to-back pair on the hall|parlour boundary); a position whose
    // pieces cannot host every room is structurally forced into hard.hearth —
    // reject it here instead of shipping the failure to the driver retries.
    // Solo hearths need none of this: the seed gate above already covers them.
    if (need_seeds >= 2 && !piece_net.empty()) {
        if (!pair_frontage_ok(piece_id, piece_rects, margins_k0, w, h, params.ir, seeds,
                              min_dims))
            return rej(g_rej.viable);
    }

    out.cells = cells;
    out.center = center;
    out.top_y = top_y;
    out.roof_plane = plane;
    out.top_storey = k_top;
    out.score = excess + frag - 0.3 * std::min(2, sides) + rng.f64() * 0.25;
    return true;
}

}  // namespace

std::vector<glm::ivec2> normalize_stack_cells(std::vector<glm::ivec2> cells) {
    std::sort(cells.begin(), cells.end(), [](const glm::ivec2& a, const glm::ivec2& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    return cells;
}

bool place_chimneys(const Ir& ir, StoreyFields& fields, const std::vector<StairCore>& stairs,
                    const ExpandedProgram& prog, const std::vector<int>& storey_of,
                    const PlanParams& params, Rng& rng,
                    const std::vector<std::vector<glm::ivec2>>& tried,
                    std::vector<ChimneyCore>& out, std::string& err) {
    out.clear();
    err.clear();
    // BTG_TRACE_STACK="x,z;x,z": parse the traced candidate once per call.
    g_trace_cells.clear();
    if (const char* ts = std::getenv("BTG_TRACE_STACK")) {
        const std::string s = ts;
        size_t p = 0;
        int x = 0, z = 0;
        while (p < s.size() && std::sscanf(s.c_str() + p, "%d,%d", &x, &z) == 2) {
            g_trace_cells.emplace_back(x, z);
            p = s.find(';', p);
            if (p == std::string::npos) break;
            ++p;
        }
    }
    std::map<int, std::vector<int>> by_storey;
    for (size_t qi = 0; qi < prog.rooms.size(); ++qi)
        if (prog.rooms[qi].hearth_code != 0 && qi < storey_of.size() && storey_of[qi] >= 0)
            by_storey[storey_of[qi]].push_back(static_cast<int>(qi));
    if (by_storey.empty()) return true;

    std::vector<VolCtx> ctxs;
    if (!build_vol_ctxs(ir, ctxs, err)) return false;

    // The re-slice gate's room sets, two tiers: FULL — every room G3 assigned
    // to the storey (the repair must host whatever the growth admits); MIN —
    // the rooms that may never be dropped (required + hearth-carrying
    // optionals). Both add the auto-corridor the pipeline injects on every
    // stair-touched storey without one (pipeline.cpp). The candidate scan
    // tries FULL first; when no position can host every optional, MIN keeps
    // the stack feasible and the pipeline's drop fallback sheds the rest.
    const int n_storeys = static_cast<int>(ir.storeys.size());
    std::vector<std::vector<std::pair<double, double>>> rooms_full(
        static_cast<size_t>(n_storeys)),
        rooms_min(static_cast<size_t>(n_storeys));
    std::vector<char> corridor_present(static_cast<size_t>(n_storeys), 0);
    for (size_t qi = 0; qi < prog.rooms.size(); ++qi) {
        const RoomInstance& r = prog.rooms[qi];
        const int k = qi < storey_of.size() ? storey_of[qi] : -1;
        if (k < 0 || k >= n_storeys) continue;
        if (r.fn_code == kFnCorridor) corridor_present[static_cast<size_t>(k)] = 1;
        const double md = fn_defaults(r.fn_code).min_dim;
        rooms_full[static_cast<size_t>(k)].emplace_back(md, r.target);
        if (!(r.optional && r.hearth_code == 0))
            rooms_min[static_cast<size_t>(k)].emplace_back(md, r.target);
    }
    for (int k = 0; k < n_storeys; ++k) {
        bool touched = false;
        for (const StairCore& st : stairs)
            touched = touched || st.from == k || st.to == k;
        if (touched && !corridor_present[static_cast<size_t>(k)]) {
            const double md = fn_defaults(kFnCorridor).min_dim;
            rooms_full[static_cast<size_t>(k)].emplace_back(md, 3.0);
            rooms_min[static_cast<size_t>(k)].emplace_back(md, 3.0);
        }
    }
    // The storeys where the pipeline will inject an auto-corridor seeded at
    // the shaft's door-ok sides (a shaft starts there, no explicit corridor):
    // eval_candidate's corridor gate keeps such a storey's seed set viable.
    std::vector<char> corridor_auto(static_cast<size_t>(n_storeys), 0);
    for (int k = 0; k < n_storeys; ++k) {
        if (corridor_present[static_cast<size_t>(k)]) continue;
        for (const StairCore& st : stairs)
            if (st.from == k) {
                corridor_auto[static_cast<size_t>(k)] = 1;
                break;
            }
    }

    const int want = prog.style.chimney == "gable"
                         ? kArchGable
                         : prog.style.chimney == "free" ? kArchFree : kArchCentral;
    std::vector<int> chain{want};
    for (const int a : {kArchCentral, kArchGable, kArchFree})
        if (a != want) chain.push_back(a);

    const int w = fields.w, h = fields.h;
    // Frontage reservation: the free 4-neighbor cells of every placed stack,
    // per storey. A later stack may not rely on them (two stacks sharing seed
    // cells send both hearth rooms after the same frontage, and the G5
    // seeding then fails one room outright).
    std::vector<std::vector<glm::ivec2>> claimed_seeds(static_cast<size_t>(n_storeys));
    for (const auto& [k0, rooms] : by_storey) {
        // Same-storey groups (§9.2): pairs share a 1x2 stack (back to back),
        // an odd hearth stands alone (1x1).
        std::vector<std::vector<int>> groups;
        for (size_t g = 0; g < rooms.size(); g += 2) {
            groups.push_back({rooms[g]});
            if (g + 1 < rooms.size()) groups.back().push_back(rooms[g + 1]);
        }
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            // A pair that found no hugging position splits into two solo
            // stacks (soft: "each stack beyond one") rather than failing the
            // storey — the frontage gate only passes pairs both rooms can
            // actually touch, and some footprints admit no such position.
            for (bool split = false;;) {
                const std::vector<int>& group = groups[gi];
                const int need = static_cast<int>(group.size());
                std::vector<double> min_dims;
                for (const int qi : group)
                    min_dims.push_back(
                        fn_defaults(prog.rooms[static_cast<size_t>(qi)].fn_code).min_dim);

                // "Друг над другом": join an existing passing stack (v1 keeps
                // the section — §5.7 caps sections at 1x2 anyway). The joining
                // group reuses the seed gates of a fresh scan — distinct
                // viable seed cells per room, and for a pair the frontage
                // gate — otherwise the join re-creates the very configuration
                // the pair scan rejected (a split sibling re-pairing onto its
                // twin's fresh stack).
                ChimneyCore* join = nullptr;
                for (ChimneyCore& cc : out) {
                    if (cc.storey1 < k0) continue;  // does not reach this storey
                    std::vector<double> piece_net, piece_net_x, piece_net_z;
                    std::vector<int> piece_id;
                    std::vector<std::array<int, 4>> piece_rects;
                    std::vector<int32_t> margins_env;
                    std::vector<glm::ivec2> seeds;
                    stack_seed_env(fields, stairs, params, k0, cc.cells, piece_net,
                                   piece_id, piece_net_x, piece_net_z, piece_rects,
                                   margins_env, seeds);
                    // The merged stack must seed ALL its rooms — the group's
                    // own plus the ones already hanging on the join target.
                    std::vector<double> joint_dims = min_dims;
                    for (const int qi : cc.hearths)
                        joint_dims.push_back(
                            fn_defaults(prog.rooms[static_cast<size_t>(qi)].fn_code).min_dim);
                    const int joint_need =
                        static_cast<int>(cc.hearths.size()) + need;
                    if (static_cast<int>(seeds.size()) < joint_need) continue;
                    if (!seed_match(piece_net, w, seeds, joint_dims)) continue;
                    if (joint_need >= 2 &&
                        !pair_frontage_ok(piece_id, piece_rects, margins_env, w, h,
                                          params.ir, seeds, joint_dims))
                        continue;
                    if (!join || cc.hearths.size() < join->hearths.size()) join = &cc;
                }
                if (join) {
                    join->hearths.insert(join->hearths.end(), group.begin(), group.end());
                    break;
                }

                Cand best;
                bool found = false;
                int used_arch = 0;
                auto is_tried = [&](const std::vector<glm::ivec2>& cells) {
                    const std::vector<glm::ivec2> key = normalize_stack_cells(cells);
                    return std::find(tried.begin(), tried.end(), key) != tried.end();
                };
                auto scan =
                    [&](const std::vector<std::vector<std::pair<double, double>>>& srooms) {
                        for (const int arch : chain) {
                            g_rej.reset();
                            size_t passed = 0;
                            std::vector<Cand> top3;  // debug-only leader board
                            auto track = [&](const Cand& c) {
                                if (!std::getenv("BTG_DEBUG_DUMP")) return;
                                top3.push_back(c);
                                std::sort(top3.begin(), top3.end(),
                                          [](const Cand& a, const Cand& b) {
                                              return a.score < b.score;
                                          });
                                if (top3.size() > 3) top3.pop_back();
                            };
                            for (int j = 0; j < h; ++j)
                                for (int i = 0; i < w; ++i) {
                                    if (need == 1) {
                                        Cand c;
                                        if (eval_candidate(ir, fields, stairs, params, ctxs,
                                                           {{i, j}}, k0, need, min_dims, srooms,
                                                           corridor_auto,
                                                           claimed_seeds[static_cast<size_t>(k0)],
                                                           arch, rng, c)) {
                                            if (is_tried(c.cells)) {
                                                ++g_rej.tried;
                                                continue;
                                            }
                                            ++passed;
                                            track(c);
                                            if (!found || c.score < best.score) {
                                                best = std::move(c);
                                                found = true;
                                                used_arch = arch;
                                            }
                                        }
                                    } else {
                                        for (const auto& set :
                                             {std::vector<glm::ivec2>{{i, j}, {i + 1, j}},
                                              std::vector<glm::ivec2>{{i, j}, {i, j + 1}}}) {
                                            Cand c;
                                            if (eval_candidate(ir, fields, stairs, params,
                                                               ctxs, set, k0, need, min_dims,
                                                               srooms, corridor_auto,
                                                               claimed_seeds[static_cast<size_t>(k0)],
                                                               arch, rng, c)) {
                                                if (is_tried(c.cells)) {
                                                    ++g_rej.tried;
                                                    continue;
                                                }
                                                ++passed;
                                                track(c);
                                                if (!found || c.score < best.score) {
                                                    best = std::move(c);
                                                    found = true;
                                                    used_arch = arch;
                                                }
                                            }
                                        }
                                    }
                                }
                            if (std::getenv("BTG_DEBUG_DUMP"))
                                std::fprintf(
                                    stderr,
                                    "chimney scan arch=%d: passed=%zu rej{bounds=%zu vol=%zu "
                                    "roof=%zu valley=%zu arch=%zu busy=%zu shaft=%zu arr=%zu "
                                    "disc=%zu dead=%zu reslice=%zu seeds=%zu viable=%zu "
                                    "corr=%zu tried=%zu}\n",
                                    arch, passed, g_rej.bounds, g_rej.volume, g_rej.roof,
                                    g_rej.valley, g_rej.arch, g_rej.busy, g_rej.shaft,
                                    g_rej.arrival, g_rej.disconnect, g_rej.dead,
                                    g_rej.reslice, g_rej.seeds, g_rej.viable, g_rej.corr,
                                    g_rej.tried);
                            if (std::getenv("BTG_DEBUG_DUMP"))
                                for (const Cand& c : top3) {
                                    std::string cs;
                                    for (const glm::ivec2& cell : c.cells)
                                        cs += " (" + std::to_string(cell.x) + "," +
                                              std::to_string(cell.y) + ")";
                                    std::fprintf(stderr, "  top arch=%d score=%.2f cells:%s\n",
                                                 arch, c.score, cs.c_str());
                                }
                            if (found) break;  // the archetype class outranks the score
                        }
                    };
                scan(rooms_full);
                if (!found) scan(rooms_min);  // the drop fallback sheds the rest
                if (!found && !split && group.size() == 2) {
                    const int first = group[0], second = group[1];  // group aliases groups[gi]
                    groups[gi] = {first};
                    groups.insert(groups.begin() + static_cast<long>(gi) + 1, {second});
                    split = true;
                    continue;
                }
                if (!found) {
                    err =
                        "place_chimneys: no valid stack for a hearth group on storey '" +
                        ir.storeys[static_cast<size_t>(k0)].id + "' (" +
                        std::to_string(need) +
                        " hearth(s)): every cell set fails the §5.7 exit rule, the storey "
                        "fields or the seed set — loosen the roof, the chimney style or the "
                        "program";
                    return false;
                }
                for (int k = 0; k <= best.top_storey; ++k) {
                    for (const glm::ivec2& c : best.cells)
                        fields.mask[static_cast<size_t>(k)]
                                   [static_cast<size_t>(c.y * w + c.x)] = kCellCore;
                    // Reserve the stack's frontage on every crossed storey:
                    // a later stack's gate may not rely on these cells.
                    std::vector<glm::ivec2> cl =
                        seed_cells(fields, k, stairs, best.cells);
                    auto& dst = claimed_seeds[static_cast<size_t>(k)];
                    for (const glm::ivec2& c : cl)
                        if (std::find(dst.begin(), dst.end(), c) == dst.end())
                            dst.push_back(c);
                }
                ChimneyCore cc;
                cc.id = "chimney" + std::to_string(out.size() + 1);
                cc.archetype = used_arch;
                cc.cells = best.cells;
                cc.storey0 = 0;  // the masonry rises from the ground (support)
                cc.storey1 = best.top_storey;
                cc.hearths = group;
                cc.top = {best.center.x, best.top_y, best.center.y};
                cc.roof_plane = best.roof_plane;
                out.push_back(std::move(cc));
                if (std::getenv("BTG_DEBUG_DUMP")) {
                    const ChimneyCore& c0 = out.back();
                    std::string cs;
                    for (const glm::ivec2& c : c0.cells)
                        cs += " (" + std::to_string(c.x) + "," + std::to_string(c.y) + ")";
                    std::fprintf(stderr, "placed %s arch=%d hearths=%zu score=%.2f cells:%s\n",
                                 c0.id.c_str(), c0.archetype, c0.hearths.size(), best.score,
                                 cs.c_str());
                }
                break;
            }
        }
    }
    return true;
}

std::vector<glm::ivec2> hearth_seed_cells(const ChimneyCore& core, const StoreyFields& fields,
                                          int k, const std::vector<StairCore>& stairs) {
    return seed_cells(fields, k, stairs, core.cells);
}

}  // namespace building_topology_generator
