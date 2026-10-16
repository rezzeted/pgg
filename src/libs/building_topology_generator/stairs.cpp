// Stair core placement (stage G4, requirements §5.5, §9.1; algorithms §15):
// one L-stair per storey gap, bottom-up. Sizing is §5.5 (n = ceil(H/0.19),
// r = H/n, g = 0.25); placement searches anchor cell x orientation x riser
// split on the shared storey grid. The shaft becomes a pinned room on the
// from-storey; the well cells (the §14.2 headroom bands, cell-quantized) are
// stamped kCellVoid into the to-storey field so the growth tiles around them.
// See stairs.h for the canonical layout.

#include "pch.h"

#include "stairs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "codes.h"

namespace building_topology_generator {

namespace {

constexpr double kEps = 1e-6;

// +90 deg in plan (x, z) — same convention as the geometry side (unit.cpp).
glm::dvec2 plan_left(const glm::dvec2& d) { return {-d.y, d.x}; }

// The orientation map: canonical point (x along dir, z along dir2) -> world.
glm::dvec2 a_mul(const glm::dvec2& dir, const glm::dvec2& dir2, double x, double z) {
    return x * dir + z * dir2;
}

// Canonical rect -> world axis-aligned rect.
void map_rect(const glm::dvec2& dir, const glm::dvec2& dir2, const glm::dvec2& t, double cx0,
              double cz0, double cx1, double cz1, double& x0, double& z0, double& x1,
              double& z1) {
    const glm::dvec2 a = a_mul(dir, dir2, cx0, cz0) + t;
    const glm::dvec2 b = a_mul(dir, dir2, cx1, cz1) + t;
    x0 = std::min(a.x, b.x);
    x1 = std::max(a.x, b.x);
    z0 = std::min(a.y, b.y);
    z1 = std::max(a.y, b.y);
}

// Canonical cell (u, v) -> grid cell (the mapped cell corners land on cell
// borders, so the min corner identifies the cell).
glm::ivec2 map_cell(const glm::dvec2& dir, const glm::dvec2& dir2, const glm::dvec2& t,
                    const StoreyFields& f, double s, int u, int v) {
    const glm::dvec2 a = a_mul(dir, dir2, u * s, v * s) + t;
    const glm::dvec2 b = a_mul(dir, dir2, (u + 1) * s, (v + 1) * s) + t;
    return {static_cast<int>(std::lround((std::min(a.x, b.x) - f.origin.x) / s)),
            static_cast<int>(std::lround((std::min(a.y, b.y) - f.origin.y) / s))};
}

// The to-storey free field must stay 4-connected once the well cells are
// carved out (a disconnected floor has unreachable rooms; I3 would fail).
bool connected_after_voids(const std::vector<int32_t>& mask, int w, int h,
                           const std::vector<int>& voids) {
    std::vector<char> blocked(static_cast<size_t>(w) * h, 0);
    for (int c : voids) blocked[static_cast<size_t>(c)] = 1;
    int total = 0, start = -1;
    for (int idx = 0; idx < w * h; ++idx)
        if (mask[static_cast<size_t>(idx)] == kCellFree && !blocked[static_cast<size_t>(idx)]) {
            ++total;
            if (start < 0) start = idx;
        }
    if (total == 0) return false;
    std::vector<int> stack{start};
    std::vector<char> seen(static_cast<size_t>(w) * h, 0);
    seen[static_cast<size_t>(start)] = 1;
    int seen_n = 0;
    while (!stack.empty()) {
        const int c = stack.back();
        stack.pop_back();
        ++seen_n;
        const int ci = c % w, cj = c / w;
        const int di[4] = {-1, 1, 0, 0};
        const int dj[4] = {0, 0, -1, 1};
        for (int q = 0; q < 4; ++q) {
            const int ni = ci + di[q], nj = cj + dj[q];
            if (ni < 0 || nj < 0 || ni >= w || nj >= h) continue;
            const int n = nj * w + ni;
            if (mask[static_cast<size_t>(n)] == kCellFree &&
                !blocked[static_cast<size_t>(n)] && !seen[static_cast<size_t>(n)]) {
                seen[static_cast<size_t>(n)] = 1;
                stack.push_back(n);
            }
        }
    }
    return seen_n == total;
}

// True when `mask` holds a DEAD piece near the changed cells: a free rect
// piece too thin to host any room (min side < 3 cells) whose cells cannot
// even find a kWingCoverCells-square cover within the union of itself and
// its free edge-adjacent neighbors — never a room, never a wing (I1 kills
// the storey: a 1-row remnant sandwiched between the L cut and the well is
// the classic case). Pieces away from the change are the field's own; the
// stair cannot fix them and is not charged for them.
bool dead_piece_near(const std::vector<int32_t>& mask, int w, int h,
                     const std::vector<int>& changed) {
    std::vector<std::array<int, 4>> pieces = decompose_cells(mask, w, h);
    merge_rect_pairs(pieces);
    for (size_t pi = 0; pi < pieces.size(); ++pi) {
        const std::array<int, 4>& p = pieces[pi];
        if (std::min(p[2] - p[0], p[3] - p[1]) >= kWingCoverCells) continue;  // self-covering
        bool near = false;
        for (int c : changed) {
            const int ci = c % w, cj = c / w;
            if (ci >= p[0] - 1 && ci <= p[2] && cj >= p[1] - 1 && cj <= p[3]) {
                near = true;
                break;
            }
        }
        if (!near) continue;
        std::vector<std::array<int, 4>> parts{p};
        for (size_t qi = 0; qi < pieces.size(); ++qi)
            if (qi != pi && rects_edge_adjacent(p, pieces[qi]))
                parts.push_back(pieces[qi]);
        if (!rects_covering_ok(parts, kWingCoverCells, kWingCoverCells)) return true;
    }
    return false;
}

}  // namespace

void size_stair(double h, int& risers, double& rise, double& going) {
    // §5.5: n = ceil(H / 0.19) keeps r <= 0.19; r > 0.19*(n-1)/n by the
    // minimality of n, so with g = 0.25 Blondel 2r+g lands in (0.598, 0.63]
    // for every full-storey H (n >= 15 for H >= 2.85).
    risers = std::max(4, static_cast<int>(std::ceil(h / 0.19 - 1e-9)));
    rise = h / risers;
    going = 0.25;
}

StairWellShape stair_well_shape(int risers, int risers_low, double rise, double going,
                                double slab_t, const IrParams& p) {
    StairWellShape sh;
    const double s = p.cell;
    const double width = 2.0 * s;
    const double l1 = (risers_low - 1) * going;
    const double l2 = (risers - risers_low - 1) * going;
    const int m1 = std::max(1, static_cast<int>(std::ceil(l1 / s - kEps)));
    const int m2 = std::max(1, static_cast<int>(std::ceil(l2 / s - kEps)));
    sh.W = m1 + 3;
    sh.D = m2 + 3;
    // §5.5: treads from step i0 on need the slab opening above them.
    const int i0 =
        static_cast<int>(std::floor((risers * rise - slab_t - p.h_walk()) / rise + kEps)) + 1;
    // Cell-quantized bands: a rect covers the cells whose interior intersects
    // it (an edge-only touch does not count, matching the check.cpp
    // tolerance), computed here in the aligned canonical frame, so plain
    // floor/ceil per axis is exact.
    auto add_rect = [&](double cx0, double cz0, double cx1, double cz1) {
        if (cx1 - cx0 < 1e-9 || cz1 - cz0 < 1e-9) return;
        const int u0 = static_cast<int>(std::floor(cx0 / s + 1e-6));
        const int u1 = static_cast<int>(std::ceil(cx1 / s - 1e-6)) - 1;
        const int v0 = static_cast<int>(std::floor(cz0 / s + 1e-6));
        const int v1 = static_cast<int>(std::ceil(cz1 / s - 1e-6)) - 1;
        for (int v = v0; v <= v1; ++v)
            for (int u = u0; u <= u1; ++u) sh.well.emplace_back(u, v);
    };
    // §14.2: march 1 from step i0 on, the landing, march 2.
    add_rect(s + std::max(0.0, (i0 - 1) * going), 0.0, s + l1, width);
    add_rect(s + l1, 0.0, s + l1 + width, width);
    add_rect(s + l1, width, s + l1 + width, width + l2);
    std::sort(sh.well.begin(), sh.well.end(),
              [](const glm::ivec2& a, const glm::ivec2& b) {
                  return a.y != b.y ? a.y < b.y : a.x < b.x;
              });
    sh.well.erase(std::unique(sh.well.begin(), sh.well.end()), sh.well.end());
    // Arrival: the row past march 2 at the march-2 columns.
    const int u_lo = static_cast<int>(std::floor((s + l1) / s + 1e-6));
    const int u_hi = static_cast<int>(std::ceil((s + l1 + width) / s - 1e-6)) - 1;
    for (int u = u_lo; u <= u_hi; ++u) sh.arrival.emplace_back(u, m2 + 2);
    return sh;
}

bool attic_walk_hosts_stair(const std::vector<char>& walk, int w, int h, double h_gap,
                            double slab_t, const IrParams& p) {
    if (walk.size() != static_cast<size_t>(w) * h) return false;
    int risers = 0;
    double rise = 0.0, going = 0.0;
    size_stair(h_gap, risers, rise, going);
    static const glm::dvec2 kDirs[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (int rl = 2; rl <= risers - 2; ++rl) {
        const StairWellShape sh = stair_well_shape(risers, rl, rise, going, slab_t, p);
        for (int di = 0; di < 4; ++di)
            for (int turn = -1; turn <= 1; turn += 2) {
                const glm::dvec2 dir = kDirs[di];
                const glm::dvec2 dir2 = static_cast<double>(turn) * plan_left(dir);
                const int wx = std::abs(dir.x) > 0.5 ? sh.W : sh.D;
                const int wz = std::abs(dir.x) > 0.5 ? sh.D : sh.W;
                if (wx > w || wz > h) continue;
                // Canonical cell (u, v) -> grid cell at anchor (px, pz): the
                // same mapping map_cell applies on the aligned grid.
                auto map_i = [&](int u, int v) {
                    return std::abs(dir.x) > 0.5 ? (dir.x > 0.5 ? u : sh.W - 1 - u)
                                                 : (dir2.x > 0.5 ? v : sh.D - 1 - v);
                };
                auto map_j = [&](int u, int v) {
                    return std::abs(dir.y) > 0.5 ? (dir.y > 0.5 ? u : sh.W - 1 - u)
                                                 : (dir2.y > 0.5 ? v : sh.D - 1 - v);
                };
                for (int pz = 0; pz + wz <= h; ++pz)
                    for (int px = 0; px + wx <= w; ++px) {
                        bool ok = true;
                        for (const glm::ivec2& c : sh.well)
                            if (!walk[static_cast<size_t>(pz + map_j(c.x, c.y)) * w +
                                      px + map_i(c.x, c.y)]) {
                                ok = false;
                                break;
                            }
                        if (ok)
                            for (const glm::ivec2& c : sh.arrival)
                                if (!walk[static_cast<size_t>(pz + map_j(c.x, c.y)) * w +
                                          px + map_i(c.x, c.y)]) {
                                    ok = false;
                                    break;
                                }
                        if (ok) return true;
                    }
            }
    }
    return false;
}

bool place_stairs(const Ir& ir, StoreyFields& fields, const PlanParams& params, Rng& rng,
                  std::vector<StairCore>& out, std::string& err) {
    err.clear();
    const double s = params.ir.cell;
    const double width = 2.0 * s;  // one flight = two grid rows (§9.1)
    const int n_storeys = static_cast<int>(ir.storeys.size());
    static const glm::dvec2 kDirs[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

    for (int k = 0; k + 1 < n_storeys; ++k) {
        const IrStorey& from = ir.storeys[static_cast<size_t>(k)];
        const IrStorey& to = ir.storeys[static_cast<size_t>(k + 1)];
        if (from.kind_code == kStoreyAttic) {
            err = "place_stairs: the attic storey '" + from.id + "' has no exit";
            return false;
        }
        const double h_gap = to.y - from.y;
        if (h_gap < params.ir.h_walk() + from.slab_t) {
            err = "place_stairs: the storey gap at '" + from.id + "' is too small";
            return false;
        }
        int risers = 0;
        double rise = 0.0, going = 0.0;
        size_stair(h_gap, risers, rise, going);
        const bool to_attic = to.kind_code == kStoreyAttic;
        const std::vector<int32_t>& mask_from = fields.mask[static_cast<size_t>(k)];
        const std::vector<int32_t>& mask_to = fields.mask[static_cast<size_t>(k + 1)];
        const std::vector<char>& walk_to = fields.walk[static_cast<size_t>(k + 1)];

        // The placement attractor: the facade center for the first core, then
        // the shaft of the core below (stacking is preferred, not required).
        // Exception: when the chain tops out in an attic, the first core aims
        // at the centroid of the attic walk mask instead — the top stair's
        // well must land inside that mask (§5.6), so a base placed elsewhere
        // forces the last core mid-field and shatters the top full storey.
        glm::dvec2 target{fields.w / 2.0, static_cast<double>(fields.h)};
        if (!out.empty()) {
            const PlanRoom& below = out.back().shaft_room;
            target = {(below.x0 + below.x1) / 2.0, (below.z0 + below.z1) / 2.0};
        } else {
            const size_t top = static_cast<size_t>(n_storeys - 1);
            if (n_storeys > 2 && ir.storeys[top].kind_code == kStoreyAttic &&
                !fields.walk[top].empty()) {
                const std::vector<char>& walk = fields.walk[top];
                double sx = 0.0, sz = 0.0;
                int n = 0;
                for (int j = 0; j < fields.h; ++j)
                    for (int i = 0; i < fields.w; ++i)
                        if (walk[static_cast<size_t>(j) * fields.w + i]) {
                            sx += i + 0.5;
                            sz += j + 0.5;
                            ++n;
                        }
                if (n > 0) target = {sx / n, sz / n};
            }
        }

        bool found = false;
        double best_score = 0.0;
        StairCore best;
        // Tileability baseline: the field may already be fragmented on its
        // own (an L notch carves a wing-only column no shaft placement can
        // avoid) — charge each candidate only the pieces/strips IT adds.
        const auto base_from = tiled_piece_stats(mask_from, fields.w, fields.h);
        const auto base_to = tiled_piece_stats(mask_to, fields.w, fields.h);
        int best_m1 = 0, best_w = 0, best_d = 0, best_wx = 0, best_wz = 0;
        int best_px = 0, best_pz = 0;
        glm::dvec2 best_t{0.0};
        int dbg_min_pieces_to = 1000, dbg_pieces_to = -1, dbg_pieces_from = -1;
        int dbg_best_pieces_to = -1, dbg_best_pieces_from = -1;
        int dbg_n_shaft = 0, dbg_n_well = 0, dbg_n_disc = 0, dbg_n_feas = 0, dbg_n_nostrip = 0;
        int dbg_n_dead = 0;
        struct DbgCand {
            int rl, di, turn, px, pz, wx, wz;
            double score;
            int pieces_from, pieces_to, strips_from, strips_to, edge;
        };
        std::vector<DbgCand> dbg_top;
        auto dbg_push = [&](const DbgCand& c) {
            if (!std::getenv("BTG_DEBUG_DUMP")) return;
            dbg_top.push_back(c);
            std::sort(dbg_top.begin(), dbg_top.end(),
                      [](const DbgCand& a, const DbgCand& b) { return a.score < b.score; });
            if (dbg_top.size() > 5) dbg_top.pop_back();
        };

        for (int rl = 2; rl <= risers - 2; ++rl) {
            const double l1 = (rl - 1) * going;
            const StairWellShape shape =
                stair_well_shape(risers, rl, rise, going, from.slab_t, params.ir);
            const int W = shape.W, D = shape.D;
            // Skip only when no orientation fits the field (the axes swap
            // for dir = +-z).
            if ((W > fields.w || D > fields.h) && (D > fields.w || W > fields.h)) continue;
            const double lx0 = s + l1, lx1 = s + l1 + width;  // the landing rect
            const double split_pen = 0.01 * std::abs(2 * rl - risers);

            for (int di = 0; di < 4; ++di)
                for (int turn = -1; turn <= 1; turn += 2) {
                    const glm::dvec2 dir = kDirs[di];
                    const glm::dvec2 dir2 = static_cast<double>(turn) * plan_left(dir);
                    // World cell extents of the canonical W x D shaft box:
                    // the axes swap for dir = +-z.
                    const int wx = std::abs(dir.x) > 0.5 ? W : D;
                    const int wz = std::abs(dir.x) > 0.5 ? D : W;
                    if (wx > fields.w || wz > fields.h) continue;
                    // Per-axis min of A*c over the shaft bbox corners; the
                    // translation aligns it with the anchor cell corner.
                    const glm::dvec2 c00 = a_mul(dir, dir2, 0.0, 0.0);
                    const glm::dvec2 c11 = a_mul(dir, dir2, W * s, D * s);
                    const glm::dvec2 min_a{std::min(c00.x, c11.x), std::min(c00.y, c11.y)};

                    for (int pz = 0; pz + wz <= fields.h; ++pz)
                        for (int px = 0; px + wx <= fields.w; ++px) {
                            const glm::dvec2 t =
                                fields.origin + s * glm::dvec2(px, pz) - min_a;
                            // The shaft must sit on free floor of the from-storey.
                            bool ok = true;
                            for (int v = 0; v < D && ok; ++v)
                                for (int u = 0; u < W && ok; ++u) {
                                    const glm::ivec2 c = map_cell(dir, dir2, t, fields, s, u, v);
                                    if (mask_from[static_cast<size_t>(c.y) * fields.w +
                                                  c.x] != kCellFree)
                                        ok = false;
                                }
                            if (!ok) {
                                ++dbg_n_shaft;
                                continue;
                            }
                            // Well cells: the §14.2 bands + the landing (the
                            // canonical shape, mapped onto the storey grid).
                            std::vector<int> voids;
                            for (const glm::ivec2& cc : shape.well) {
                                const glm::ivec2 c =
                                    map_cell(dir, dir2, t, fields, s, cc.x, cc.y);
                                voids.push_back(c.y * fields.w + c.x);
                            }
                            std::sort(voids.begin(), voids.end());
                            voids.erase(std::unique(voids.begin(), voids.end()), voids.end());
                            // Arrival cells: the row past march 2 at the march-2 columns.
                            std::vector<int> arrival;
                            for (const glm::ivec2& cc : shape.arrival) {
                                const glm::ivec2 c =
                                    map_cell(dir, dir2, t, fields, s, cc.x, cc.y);
                                arrival.push_back(c.y * fields.w + c.x);
                            }
                            std::sort(arrival.begin(), arrival.end());
                            arrival.erase(std::unique(arrival.begin(), arrival.end()),
                                          arrival.end());
                            // The well and the arrival must land on free floor of the
                            // to-storey; into an attic the walked parts (the whole
                            // well — the walker's head pokes above the to-floor on
                            // the high steps — plus the arrival) stay inside the
                            // h_walk mask (§5.6).
                            ok = true;
                            for (int c : voids)
                                if (mask_to[static_cast<size_t>(c)] != kCellFree ||
                                    (to_attic && !walk_to[static_cast<size_t>(c)])) {
                                    ok = false;
                                    break;
                                }
                            for (int c : arrival)
                                if (mask_to[static_cast<size_t>(c)] != kCellFree ||
                                    (to_attic && !walk_to[static_cast<size_t>(c)])) {
                                    ok = false;
                                    break;
                                }
                            if (!ok) {
                                ++dbg_n_well;
                                continue;
                            }
                            // Score: attractor distance, compactness, wall hugging,
                            // a deterministic split preference, retry jitter (§11).
                            const glm::dvec2 pad_w = a_mul(dir, dir2, s / 2, D * s / 2) + t;
                            const glm::dvec2 pad_c{(pad_w.x - fields.origin.x) / s,
                                                   (pad_w.y - fields.origin.y) / s};
                            double score = std::abs(pad_c.x - target.x) +
                                           std::abs(pad_c.y - target.y);
                            score += 0.25 * (W * D) + split_pen;
                            auto free_at = [&](int ci, int cj) {
                                if (ci < 0 || cj < 0 || ci >= fields.w || cj >= fields.h)
                                    return false;
                                return mask_from[static_cast<size_t>(cj) * fields.w + ci] ==
                                       kCellFree;
                            };
                            int edge = 0;
                            bool west = true, east = true, south = true, north = true;
                            for (int v = pz; v < pz + wz; ++v) {
                                if (free_at(px - 1, v)) west = false;
                                if (free_at(px + wx, v)) east = false;
                            }
                            for (int u = px; u < px + wx; ++u) {
                                if (free_at(u, pz - 1)) south = false;
                                if (free_at(u, pz + wz)) north = false;
                            }
                            edge = (west ? 1 : 0) + (east ? 1 : 0) + (south ? 1 : 0) +
                                   (north ? 1 : 0);
                            score -= 0.75 * edge;
                            // Tileability (§7.5 I10): a mid-field shaft or well
                            // shatters the storey into more rectangle pieces
                            // than the storey has rooms, and the G5 repair
                            // cannot recover — prefer few-piece placements.
                            // The attic hosts few rooms and its walk field is
                            // already trimmed, so fragmentation there is
                            // almost always fatal — weigh it double.
                            std::pair<int, int> st_from, st_to;
                            {
                                std::vector<int32_t> mf = mask_from;
                                std::vector<int> shaft_cells;
                                for (int v = 0; v < D; ++v)
                                    for (int u = 0; u < W; ++u) {
                                        const glm::ivec2 c =
                                            map_cell(dir, dir2, t, fields, s, u, v);
                                        mf[static_cast<size_t>(c.y) * fields.w + c.x] =
                                            kCellOutside;
                                        shaft_cells.push_back(c.y * fields.w + c.x);
                                    }
                                std::vector<int32_t> mt = mask_to;
                                for (int c : voids)
                                    mt[static_cast<size_t>(c)] = kCellVoid;
                                // Dead-pocket feasibility (hard): a shaft or
                                // well that strands a piece no room can ever
                                // claim (not even as a wing) makes the storey
                                // untileable (I1) — reject the candidate.
                                if (dead_piece_near(mf, fields.w, fields.h, shaft_cells) ||
                                    dead_piece_near(mt, fields.w, fields.h, voids)) {
                                    ++dbg_n_dead;
                                    continue;
                                }
                                st_from = tiled_piece_stats(mf, fields.w, fields.h);
                                st_to = tiled_piece_stats(mt, fields.w, fields.h);
                                dbg_pieces_from = st_from.first;
                                dbg_pieces_to = st_to.first;
                                if (dbg_pieces_to < dbg_min_pieces_to)
                                    dbg_min_pieces_to = dbg_pieces_to;
                                // An un-hostable strip (< 3 cells: even a
                                // corridor needs 1.0 m clear) wings onto a
                                // neighbor only when that piece is single-room
                                // AND the strip runs along it (I10 covering) —
                                // penalize hard. Both terms charge only the
                                // delta over the untouched field (its own
                                // fragmentation is not the shaft's fault).
                                score += 4.0 * std::max(0, st_from.first - base_from.first) +
                                         (to_attic ? 8.0 : 4.0) *
                                             std::max(0, st_to.first - base_to.first) +
                                         25.0 * (std::max(0, st_from.second -
                                                                base_from.second) +
                                                 std::max(0, st_to.second - base_to.second));
                            }
                            score += rng.f64() * 0.5;
                            if (found && score >= best_score) continue;
                            // Expensive last: the to-storey must stay connected.
                            if (!connected_after_voids(mask_to, fields.w, fields.h, voids)) {
                                ++dbg_n_disc;
                                continue;
                            }
                            found = true;
                            ++dbg_n_feas;
                            if (st_from.second == 0 && st_to.second == 0) ++dbg_n_nostrip;
                            dbg_push({rl, di, turn, px, pz, wx, wz, score, st_from.first,
                                      st_to.first, st_from.second, st_to.second, edge});
                            best_score = score;
                            dbg_best_pieces_to = dbg_pieces_to;
                            dbg_best_pieces_from = dbg_pieces_from;
                            best = StairCore{};
                            best.id = "stair" + std::to_string(k + 1);
                            best.from = k;
                            best.to = k + 1;
                            best.risers = risers;
                            best.risers_low = rl;
                            best.rise = rise;
                            best.going = going;
                            best.width = width;
                            best.start = a_mul(dir, dir2, s, width / 2) + t;
                            best.dir = dir;
                            best.dir2 = dir2;
                            map_rect(dir, dir2, t, lx0, 0.0, lx1, width, best.landing.x0,
                                     best.landing.z0, best.landing.x1, best.landing.z1);
                            best.void_cells = voids;
                            best.arrival_cells = arrival;
                            best_m1 = shape.W - 3;  // march-1 columns (door_ok below)
                            best_w = W;    // canonical extents (door_ok below)
                            best_d = D;
                            best_wx = wx;  // world cell extents (shaft rect)
                            best_wz = wz;
                            best_px = px;
                            best_pz = pz;
                            best_t = t;
                        }
                }
        }
        if (!found) {
            err = "place_stairs: no placement for the stair from '" + from.id + "' to '" +
                  to.id + "'";
            return false;
        }
        if (std::getenv("BTG_DEBUG_DUMP")) {
            std::fprintf(stderr,
                         "stair %s: pieces from=%d to=%d (min to=%d, raw to=%d, walk to=%d), "
                         "shaft at (%d,%d) %dx%d; cands: shaft-blocked %d, well-blocked %d, "
                         "dead-pocket %d, disconnected %d, feasible %d (no-strip %d)\n",
                         best.id.c_str(), dbg_best_pieces_from, dbg_best_pieces_to,
                         dbg_min_pieces_to, tiled_piece_count(mask_to, fields.w, fields.h),
                         [&] {
                             std::vector<int32_t> mw(fields.w * fields.h, kCellOutside);
                             for (int c = 0; c < fields.w * fields.h; ++c)
                                 if (mask_to[static_cast<size_t>(c)] == kCellFree &&
                                     (walk_to.empty() || walk_to[static_cast<size_t>(c)]))
                                     mw[static_cast<size_t>(c)] = kCellFree;
                             return tiled_piece_count(mw, fields.w, fields.h);
                         }(),
                         best_px, best_pz, best_wx, best_wz, dbg_n_shaft, dbg_n_well,
                         dbg_n_dead, dbg_n_disc, dbg_n_feas, dbg_n_nostrip);
            auto dump_mask = [&](const char* title, const std::vector<int32_t>& m) {
                std::string d = std::string(title) + " (. blocked, ? free):\n";
                for (int j = fields.h - 1; j >= 0; --j) {
                    d += "  ";
                    for (int i = 0; i < fields.w; ++i)
                        d += m[static_cast<size_t>(j) * fields.w + i] == kCellFree ? '?' : '.';
                    d += "\n";
                }
                std::fprintf(stderr, "%s", d.c_str());
            };
            dump_mask("from field", mask_from);
            dump_mask("to field", mask_to);
            for (const auto& cand : dbg_top)
                std::fprintf(stderr,
                             "  cand rl=%d dir=%d turn=%d at (%d,%d) %dx%d: score %.2f "
                             "(pieces %d/%d strips %d/%d edge %d)\n",
                             cand.rl, cand.di, cand.turn, cand.px, cand.pz, cand.wx, cand.wz,
                             cand.score, cand.pieces_from, cand.pieces_to, cand.strips_from,
                             cand.strips_to, cand.edge);
            if (to_attic) {
                std::vector<char> at(fields.w * fields.h, 0);
                for (int c : best.void_cells) at[static_cast<size_t>(c)] = '#';
                for (int c : best.arrival_cells) at[static_cast<size_t>(c)] = 'A';
                std::string d = "attic field (. blocked, w walk, ? knee/free, # well, A arrival):\n";
                for (int j = fields.h - 1; j >= 0; --j) {
                    d += "  ";
                    for (int i = 0; i < fields.w; ++i) {
                        const size_t ci = static_cast<size_t>(j) * fields.w + i;
                        if (at[ci]) {
                            d += at[ci];
                            continue;
                        }
                        const int32_t m = mask_to[ci];
                        if (m != kCellFree)
                            d += '.';
                        else if (!walk_to.empty() && walk_to[ci])
                            d += 'w';
                        else
                            d += '?';
                    }
                    d += "\n";
                }
                std::fprintf(stderr, "%s", d.c_str());
            }
        }

        // Well rects: decompose the void cells into maximal rects, in meters.
        {
            std::vector<int32_t> wm(static_cast<size_t>(fields.w) * fields.h, kCellOutside);
            for (int c : best.void_cells) wm[static_cast<size_t>(c)] = 0;
            for (const std::array<int, 4>& r : decompose_cells(wm, fields.w, fields.h)) {
                IrSlabRect rr;
                rr.x0 = fields.origin.x + r[0] * s;
                rr.z0 = fields.origin.y + r[1] * s;
                rr.x1 = fields.origin.x + r[2] * s;
                rr.z1 = fields.origin.y + r[3] * s;
                best.well_rects.push_back(rr);
            }
        }
        // The shaft room: pinned, door-ok only on the full-headroom floor
        // (approach pad, the notch beside march 1, the arrival row, §9.1).
        {
            PlanRoom shaft;
            shaft.id = "stairwell";
            shaft.base_id = "stairwell";
            shaft.fn_code = kFnCorridor;
            shaft.zone_code = kZoneService;
            shaft.area_min = 0.0;
            shaft.area_max = 1e9;  // the area term stays neutral (§8.2)
            shaft.target = best_wx * best_wz * s * s;
            shaft.min_dim = fn_defaults(kFnCorridor).min_dim;
            shaft.habitable = false;
            shaft.pinned = true;
            shaft.x0 = best_px;
            shaft.z0 = best_pz;
            shaft.x1 = best_px + best_wx;
            shaft.z1 = best_pz + best_wz;
            shaft.door_ok.assign(static_cast<size_t>(best_wx) * best_wz, 0);
            for (int v = 0; v < best_d; ++v)
                for (int u = 0; u < best_w; ++u) {
                    const bool ok =
                        u == 0 || v == best_d - 1 || (v >= 2 && u <= best_m1);
                    if (!ok) continue;
                    const glm::ivec2 c =
                        map_cell(best.dir, best.dir2, best_t, fields, s, u, v);
                    shaft.door_ok[static_cast<size_t>(c.y - best_pz) * best_wx +
                                  (c.x - best_px)] = 1;
                }
            best.shaft_room = std::move(shaft);
        }
        // Carve the well into the to-storey field; later cores see it.
        for (int c : best.void_cells)
            fields.mask[static_cast<size_t>(k + 1)][static_cast<size_t>(c)] = kCellVoid;
        out.push_back(std::move(best));
    }
    return true;
}

}  // namespace building_topology_generator
