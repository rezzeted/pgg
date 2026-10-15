// G5 initialization: cell field from the massing, seed desirability fields
// (algorithms §6.1), rectangle strip growth (§6.2 RECT), deficit fill, and a
// guillotine re-slice as the rectangle-tiling repair. B2: one storey, no
// cores, no corridors (§18 п. 2).

#include "pch.h"

#include "plan.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "roof.h"  // point_in_poly

namespace building_topology_generator {

namespace {

constexpr double kEps = 1e-9;

}  // namespace

void FloorPlan::rasterize() {
    cells = outside;
    for (size_t r = 0; r < rooms.size(); ++r)
        for (int j = rooms[r].z0; j < rooms[r].z1; ++j)
            for (int i = rooms[r].x0; i < rooms[r].x1; ++i) at(i, j) = static_cast<int32_t>(r);
}

bool FloorPlan::check_rectangle_tiling(std::string& err) const {
    // `seen` starts as the outside mask; rooms stamp over it. What stays
    // kCellFree was never claimed; a stamp over kCellOutside or another room
    // is an error.
    std::vector<int32_t> seen(outside);
    for (size_t r = 0; r < rooms.size(); ++r) {
        const PlanRoom& room = rooms[r];
        if (room.x0 < 0 || room.z0 < 0 || room.x1 > w || room.z1 > h ||
            room.x0 >= room.x1 || room.z0 >= room.z1) {
            err = "room " + room.id + " has a degenerate or out-of-field rect";
            return false;
        }
        for (int j = room.z0; j < room.z1; ++j)
            for (int i = room.x0; i < room.x1; ++i) {
                int32_t& c = seen[static_cast<size_t>(j) * w + i];
                if (c == kCellOutside) {
                    err = "room " + room.id + " covers an outside cell (" +
                          std::to_string(i) + ", " + std::to_string(j) + ")";
                    return false;
                }
                if (c != kCellFree) {
                    err = "cell (" + std::to_string(i) + ", " + std::to_string(j) +
                          ") claimed by both " + rooms[static_cast<size_t>(c)].id + " and " +
                          room.id;
                    return false;
                }
                c = static_cast<int32_t>(r);
            }
    }
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if (seen[static_cast<size_t>(j) * w + i] == kCellFree) {
                err = "cell (" + std::to_string(i) + ", " + std::to_string(j) + ") is free";
                return false;
            }
    return true;
}

int FloorPlan::find_room(const std::string& id) const {
    for (size_t r = 0; r < rooms.size(); ++r)
        if (rooms[r].id == id) return static_cast<int>(r);
    return -1;
}

void FloorPlan::room_dims_net(int r, const IrParams& p, double& w_m, double& d_m) const {
    const PlanRoom& room = rooms[static_cast<size_t>(r)];
    const double s = p.cell;
    // Wall half-thickness per side, scanned over the whole side: the
    // thickest wall along the side wins (an exterior piece dominates a
    // mixed side). Requires a rasterized `cells` grid.
    auto neighbor_t = [&](int i, int j) {
        const int32_t n = inside(i, j) ? at(i, j) : kCellOutside;
        return (n == kCellOutside ? p.t_exterior : p.t_partition) / 2;
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
    w_m = std::max(0.0, (room.x1 - room.x0) * s - t_x0 - t_x1);
    d_m = std::max(0.0, (room.z1 - room.z0) * s - t_z0 - t_z1);
}

double FloorPlan::room_area_net(int r, const IrParams& p) const {
    double w_m, d_m;
    room_dims_net(r, p, w_m, d_m);
    return w_m * d_m;
}

bool storey_field(const MassingCandidate& cand, const IrParams& params, int& w_out,
                  int& h_out, glm::dvec2& origin, std::vector<int32_t>& cells,
                  std::string& err) {
    for (const MassingVolume& v : cand.volumes) {
        if (v.storeys != 1 || v.attic) {
            err = "B2: the plan stage covers single-storey volumes without attics "
                  "(multi-storey and attics arrive with B3)";
            return false;
        }
    }
    const double s = params.cell;
    const double t = params.t_exterior;
    // Union bbox of the volume outlines (outer wall face, §5.2).
    double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;
    for (const MassingVolume& v : cand.volumes)
        for (const glm::dvec2& p : v.outline) {
            x0 = std::min(x0, p.x);
            z0 = std::min(z0, p.y);
            x1 = std::max(x1, p.x);
            z1 = std::max(z1, p.y);
        }
    // The axis-frame field starts half an exterior wall inside the outline.
    const glm::dvec2 field0(x0 + t / 2, z0 + t / 2);
    const int w = static_cast<int>(std::lround((x1 - x0 - t) / s));
    const int h = static_cast<int>(std::lround((z1 - z0 - t) / s));
    if (w <= 0 || h <= 0) {
        err = "the massing field is empty";
        return false;
    }
    origin = field0;
    w_out = w;
    h_out = h;
    cells.assign(static_cast<size_t>(w) * h, kCellOutside);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            // The cell center in the building frame; the outline is t/2
            // outside the cell field, so the center test is exact (t/2 < s/2).
            const double px = field0.x + (i + 0.5) * s;
            const double pz = field0.y + (j + 0.5) * s;
            for (const MassingVolume& v : cand.volumes) {
                if (point_in_poly(v.outline, px, pz)) {
                    cells[static_cast<size_t>(j) * w + i] = kCellFree;
                    break;
                }
            }
        }
    return true;
}

namespace {

// ---------------------------------------------------------------------------
// Seeds (algorithms §6.1)
// ---------------------------------------------------------------------------

struct SeedField {
    // Weights of the desirability field F_r(c); the doc names the terms, the
    // values are tuned by the B2 metrics (algorithms §14).
    double a_ext = 3.0;    // habitable room at an exterior wall
    double a_ent = 2.0;    // entrance room near the facade center
    double a_priv = 1.5;   // private room far from the entrance
    double a_near = 1.0;   // close to linked rooms' seeds
    double a_crowd = 1.0;  // away from placed seeds
};

// Facade-center entrance point in cell coords (the front facade is z = h).
glm::dvec2 entrance_point(const FloorPlan& fp) {
    return {fp.w / 2.0, static_cast<double>(fp.h)};
}

// Link pull weight by kind: door/near attract, no_door repels.
double link_pull(int kind) {
    switch (kind) {
        case kLinkDoor: return 1.0;
        case kLinkNear: return 0.7;
        case kLinkNoDoor: return -0.8;
        default: return 0.0;
    }
}

// `placed` holds only already-seeded rooms (degenerate rects are skipped by
// the caller).
double seed_desirability(const PlanRoom& room, int i, int j, const FloorPlan& fp,
                         const std::vector<const PlanRoom*>& placed,
                         const std::vector<std::pair<std::pair<int, int>, double>>& linked,
                         const SeedField& f, bool front_entrance) {
    const double s = 0.5;  // only ratios matter here
    double score = 0.0;
    // Exterior wall contact (any side).
    const bool at_wall = (i == 0 || j == 0 || i == fp.w - 1 || j == fp.h - 1);
    if (room.habitable && at_wall) score += f.a_ext;
    const glm::dvec2 ent = entrance_point(fp);
    const double d_ent = std::abs(i + 0.5 - ent.x) + std::abs(j + 0.5 - ent.y);
    if (room.entrance_front && front_entrance)
        score += f.a_ent * (fp.w + fp.h - d_ent) / (fp.w + fp.h) * 2.0;
    if (room.zone_code == kZonePrivate)
        score += f.a_priv * d_ent / (fp.w + fp.h);
    for (const auto& [cell, pull] : linked) {
        const double d = std::abs(i - cell.first) + std::abs(j - cell.second);
        score += pull * f.a_near / (1.0 + d * s);
    }
    for (const PlanRoom* q : placed) {
        const double qx = (q->x0 + q->x1) / 2.0, qz = (q->z0 + q->z1) / 2.0;
        const double d = std::abs(i + 0.5 - qx) + std::abs(j + 0.5 - qz);
        score -= f.a_crowd / (1.0 + d * s);
    }
    return score;
}

// ---------------------------------------------------------------------------
// Field decomposition + guillotine: rectangle-tiling repair and fallback.
// ---------------------------------------------------------------------------

// Decompose the inside cells of the mask into disjoint maximal rectangles
// (greedy: the first free cell extends right while free, then down while the
// whole row is free). Covers every inside cell exactly once.
std::vector<std::array<int, 4>> decompose_field(const std::vector<int32_t>& mask, int w,
                                                int h) {
    std::vector<char> taken(static_cast<size_t>(w) * h, 0);
    std::vector<std::array<int, 4>> rects;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            if (taken[static_cast<size_t>(j) * w + i] ||
                mask[static_cast<size_t>(j) * w + i] == kCellOutside)
                continue;
            int x1 = i;
            while (x1 + 1 < w && !taken[static_cast<size_t>(j) * w + x1 + 1] &&
                   mask[static_cast<size_t>(j) * w + x1 + 1] != kCellOutside)
                ++x1;
            int z1 = j;
            for (bool ok = true; ok && z1 + 1 < h;) {
                for (int x = i; x <= x1; ++x)
                    if (taken[static_cast<size_t>(z1 + 1) * w + x] ||
                        mask[static_cast<size_t>(z1 + 1) * w + x] == kCellOutside) {
                        ok = false;
                        break;
                    }
                if (ok) ++z1;
            }
            for (int z = j; z <= z1; ++z)
                for (int x = i; x <= x1; ++x) taken[static_cast<size_t>(z) * w + x] = 1;
            rects.push_back({i, j, x1 + 1, z1 + 1});
        }
    return rects;
}

// Recursively split the rect [x0,x1)x[z0,z1) among rooms[lo, hi) weighted by
// their area targets. Deterministic: split direction by the longer side, the
// room subset by a median split along the split axis (rooms are pre-sorted
// by their centroid projection).
bool guillotine(FloorPlan& fp, std::vector<int>& ord, int lo, int hi, int x0, int z0,
                int x1, int z1, std::string& err) {
    if (hi - lo == 1) {
        PlanRoom& r = fp.rooms[static_cast<size_t>(ord[static_cast<size_t>(lo)])];
        r.x0 = x0;
        r.z0 = z0;
        r.x1 = x1;
        r.z1 = z1;
        return true;
    }
    const int w = x1 - x0, h = z1 - z0;
    if (w < 2 && h < 2) {
        err = "guillotine: a 1-cell rect cannot host " + std::to_string(hi - lo) + " rooms";
        return false;
    }
    // Split along x (left | right) when the rect is wide enough; a 1-wide
    // rect forces a horizontal cut.
    const bool vertical = (w >= 2 && w >= h) || h < 2;
    std::sort(ord.begin() + lo, ord.begin() + hi, [&](int a, int b) {
        const PlanRoom& A = fp.rooms[static_cast<size_t>(a)];
        const PlanRoom& B = fp.rooms[static_cast<size_t>(b)];
        const double ca = vertical ? (A.x0 + A.x1) / 2.0 : (A.z0 + A.z1) / 2.0;
        const double cb = vertical ? (B.x0 + B.x1) / 2.0 : (B.z0 + B.z1) / 2.0;
        if (ca != cb) return ca < cb;
        return A.id < B.id;
    });
    double total = 0.0;
    for (int k = lo; k < hi; ++k) total += fp.rooms[static_cast<size_t>(ord[k])].target;
    // Split position: cumulative target share, at least one room per side and
    // one cell per side.
    double acc = 0.0;
    int cut_rooms = lo;
    for (int k = lo; k < hi - 1; ++k) {
        acc += fp.rooms[static_cast<size_t>(ord[k])].target;
        if (acc >= total / 2) {
            cut_rooms = k + 1;
            break;
        }
    }
    if (cut_rooms <= lo) cut_rooms = lo + 1;
    double left_share = 0.0;
    for (int k = lo; k < cut_rooms; ++k)
        left_share += fp.rooms[static_cast<size_t>(ord[k])].target;
    const double frac = total > 0.0 ? left_share / total : 0.5;
    if (vertical) {
        const int span = std::max(1, static_cast<int>(std::lround(w * frac)));
        const int xc = std::clamp(x0 + span, x0 + 1, x1 - 1);
        return guillotine(fp, ord, lo, cut_rooms, x0, z0, xc, z1, err) &&
               guillotine(fp, ord, cut_rooms, hi, xc, z0, x1, z1, err);
    }
    const int span = std::max(1, static_cast<int>(std::lround(h * frac)));
    const int zc = std::clamp(z0 + span, z0 + 1, z1 - 1);
    return guillotine(fp, ord, lo, cut_rooms, x0, z0, x1, zc, err) &&
           guillotine(fp, ord, cut_rooms, hi, x0, zc, x1, z1, err);
}

// Re-slice the whole storey: decompose the field, attach rooms to rects by
// centroid, donate rooms to orphan rects, guillotine each rect.
bool repair_reslice(FloorPlan& fp, std::string& err) {
    const std::vector<std::array<int, 4>> rects = decompose_field(fp.outside, fp.w, fp.h);
    if (rects.empty()) {
        err = "repair: the field is empty";
        return false;
    }
    std::vector<std::vector<int>> rect_rooms(rects.size());
    auto rect_of = [&](double cx, double cz) {
        for (size_t k = 0; k < rects.size(); ++k) {
            const auto& r = rects[k];
            if (cx >= r[0] && cx < r[2] && cz >= r[1] && cz < r[3]) return static_cast<int>(k);
        }
        return -1;
    };
    for (size_t ri = 0; ri < fp.rooms.size(); ++ri) {
        const PlanRoom& room = fp.rooms[ri];
        const int k = rect_of((room.x0 + room.x1) / 2.0, (room.z0 + room.z1) / 2.0);
        if (k >= 0) rect_rooms[static_cast<size_t>(k)].push_back(static_cast<int>(ri));
    }
    // Orphan rects (no room centroid landed inside) get the nearest room
    // donated by the most crowded rect.
    for (size_t k = 0; k < rects.size(); ++k) {
        if (!rect_rooms[k].empty()) continue;
        int donor = -1;
        for (size_t d = 0; d < rects.size(); ++d)
            if (rect_rooms[d].size() > 1 &&
                (donor < 0 || rect_rooms[d].size() > rect_rooms[static_cast<size_t>(donor)].size()))
                donor = static_cast<int>(d);
        if (donor < 0) {
            err = "repair: more field rects than rooms (degenerate footprint)";
            return false;
        }
        const double cx = (rects[k][0] + rects[k][2]) / 2.0;
        const double cz = (rects[k][1] + rects[k][3]) / 2.0;
        auto& pool = rect_rooms[static_cast<size_t>(donor)];
        size_t best = 0;
        double best_d = 1e30;
        for (size_t q = 0; q < pool.size(); ++q) {
            const PlanRoom& room = fp.rooms[static_cast<size_t>(pool[q])];
            const double dx = (room.x0 + room.x1) / 2.0 - cx;
            const double dz = (room.z0 + room.z1) / 2.0 - cz;
            const double d = dx * dx + dz * dz;
            if (d < best_d) {
                best_d = d;
                best = q;
            }
        }
        rect_rooms[k].push_back(pool[best]);
        pool.erase(pool.begin() + static_cast<long>(best));
    }
    for (size_t k = 0; k < rects.size(); ++k)
        if (!guillotine(fp, rect_rooms[k], 0, static_cast<int>(rect_rooms[k].size()),
                        rects[k][0], rects[k][1], rects[k][2], rects[k][3], err))
            return false;
    return true;
}

}  // namespace

FloorPlan grow_floor(const ExpandedProgram& prog, const MassingCandidate& cand,
                     const PlanParams& params, Rng& rng, std::string& err) {
    FloorPlan fp;
    // Building frame (§5.1): the origin is the center of the footprint union,
    // the same shift massing_to_ir applies to ir.volumes — the plan, the IR
    // spaces/walls/nodes and the volumes then share one frame.
    MassingCandidate centered = cand;
    {
        double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;
        for (const MassingVolume& v : centered.volumes)
            for (const glm::dvec2& p : v.outline) {
                x0 = std::min(x0, p.x);
                z0 = std::min(z0, p.y);
                x1 = std::max(x1, p.x);
                z1 = std::max(z1, p.y);
            }
        const glm::dvec2 c{(x0 + x1) / 2, (z0 + z1) / 2};
        for (MassingVolume& v : centered.volumes)
            for (glm::dvec2& p : v.outline) p -= c;
    }
    if (!storey_field(centered, params.ir, fp.w, fp.h, fp.origin, fp.outside, err)) return fp;
    // Growth works on its own grid copy; fp.cells is rebuilt by rasterize().
    std::vector<int32_t> occ = fp.outside;

    // Rooms: required instances + the optional instances the massing estimate
    // admitted (G3-lite for one storey, algorithms §4: greedy while the
    // circulation-adjusted spare lasts).
    double free_area = 0.0;
    for (int32_t c : fp.outside)
        if (c == kCellFree) free_area += params.ir.cell * params.ir.cell;
    free_area *= (1.0 - 0.08);  // kappa_partitions, as in G2
    double used = 0.0;
    std::vector<const RoomInstance*> chosen;
    for (const RoomInstance& r : prog.rooms) {
        if (!r.optional) {
            chosen.push_back(&r);
            used += r.target;
            continue;
        }
        const double spare = (free_area - used) / (1.0 + params.comms_share);
        if (spare >= r.target) {
            chosen.push_back(&r);
            used += r.target;
        }
    }

    for (const RoomInstance* r : chosen) {
        PlanRoom room;
        room.id = r->id;
        room.base_id = r->base_id;
        room.fn_code = r->fn_code;
        room.zone_code = r->zone_code;
        room.area_min = r->area_min;
        room.area_max = r->area_max;
        room.target = r->target;
        room.min_dim = fn_defaults(r->fn_code).min_dim;
        room.habitable = fn_defaults(r->fn_code).habitable;
        room.optional = r->optional;
        room.hearth_code = r->hearth_code;
        room.entrance_front =
            std::find(prog.entrance_into.begin(), prog.entrance_into.end(), r->id) !=
            prog.entrance_into.end();
        room.entrance_back =
            std::find(prog.back_door_into.begin(), prog.back_door_into.end(), r->id) !=
            prog.back_door_into.end();
        fp.rooms.push_back(std::move(room));
    }
    if (fp.rooms.empty()) {
        err = "the program has no rooms";
        return fp;
    }

    // -- Seeds -------------------------------------------------------------
    // Order (§6.1): entrance room first, then by descending target area
    // (hearth rooms lead once cores exist — B4; stair heads — B3).
    std::vector<int> order(fp.rooms.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const PlanRoom& A = fp.rooms[static_cast<size_t>(a)];
        const PlanRoom& B = fp.rooms[static_cast<size_t>(b)];
        if (A.entrance_front != B.entrance_front) return A.entrance_front;
        return A.target > B.target;
    });

    const SeedField field_w;
    std::vector<const PlanRoom*> placed;
    // Room id -> (seed cell, pull) of already-seeded linked rooms. Links are
    // instance-resolved by G1, so ids match exactly.
    std::vector<std::pair<std::string, std::pair<int, int>>> seeds;
    auto linked_seed_cells = [&](const std::string& id) {
        std::vector<std::pair<std::pair<int, int>, double>> out;
        for (const ProgramLink& l : prog.links) {
            const std::string* other = nullptr;
            if (l.a == id)
                other = &l.b;
            else if (l.b == id)
                other = &l.a;
            if (!other) continue;
            for (const auto& [sid, cell] : seeds)
                if (sid == *other) out.emplace_back(cell, link_pull(l.kind));
        }
        return out;
    };

    for (int oi : order) {
        PlanRoom& room = fp.rooms[static_cast<size_t>(oi)];
        const auto linked = linked_seed_cells(room.id);
        // Entrance rooms seed on their facade row first (front = high j,
        // back = low j); the constraint softens to a plain preference when
        // the row is fully outside (L footprints).
        int best_i = -1, best_j = -1;
        double best = -1e30;
        for (int pass = 0; pass < 2 && best_i < 0; ++pass)
            for (int j = 0; j < fp.h; ++j)
                for (int i = 0; i < fp.w; ++i) {
                    if (occ[static_cast<size_t>(j) * fp.w + i] != kCellFree) continue;
                    if (pass == 0) {
                        if (room.entrance_front && j != fp.h - 1) continue;
                        if (room.entrance_back && j != 0) continue;
                    }
                    double sc =
                        seed_desirability(room, i, j, fp, placed, linked, field_w,
                                          room.entrance_front);
                    sc += rng.f64() * 0.1;  // small tie noise (§6.1)
                    if (sc > best) {
                        best = sc;
                        best_i = i;
                        best_j = j;
                    }
                }
        if (best_i < 0) {
            err = "no free cell for room " + room.id;
            return fp;
        }
        room.x0 = best_i;
        room.z0 = best_j;
        room.x1 = best_i + 1;
        room.z1 = best_j + 1;
        occ[static_cast<size_t>(best_j) * fp.w + best_i] = static_cast<int32_t>(oi);
        seeds.emplace_back(room.id, std::pair{best_i, best_j});
        placed.push_back(&room);
    }

    // -- Rectangle growth (§6.2 RECT phase) --------------------------------
    // A room grows by a full side strip (keeps the rectangle). The room order
    // is weighted by the remaining deficit share (§6.2).
    const double s2 = params.ir.cell * params.ir.cell;
    auto deficit = [&](const PlanRoom& r) {
        return std::max(0.0, r.target - r.area_cells() * s2);
    };
    // Side strip is free? Strip = one cell deep, full side length.
    auto strip_free = [&](const PlanRoom& r, int side) {
        // side: 0 = -x, 1 = +x, 2 = -z, 3 = +z
        if (side == 0) {
            if (r.x0 == 0) return false;
            for (int j = r.z0; j < r.z1; ++j)
                if (occ[static_cast<size_t>(j) * fp.w + r.x0 - 1] != kCellFree) return false;
            return true;
        }
        if (side == 1) {
            if (r.x1 == fp.w) return false;
            for (int j = r.z0; j < r.z1; ++j)
                if (occ[static_cast<size_t>(j) * fp.w + r.x1] != kCellFree) return false;
            return true;
        }
        if (side == 2) {
            if (r.z0 == 0) return false;
            for (int i = r.x0; i < r.x1; ++i)
                if (occ[static_cast<size_t>(r.z0 - 1) * fp.w + i] != kCellFree) return false;
            return true;
        }
        if (r.z1 == fp.h) return false;
        for (int i = r.x0; i < r.x1; ++i)
            if (occ[static_cast<size_t>(r.z1) * fp.w + i] != kCellFree) return false;
        return true;
    };
    auto apply_strip = [&](PlanRoom& r, int side, int ri) {
        if (side == 0) {
            --r.x0;
            for (int j = r.z0; j < r.z1; ++j)
                occ[static_cast<size_t>(j) * fp.w + r.x0] = static_cast<int32_t>(ri);
        } else if (side == 1) {
            for (int j = r.z0; j < r.z1; ++j)
                occ[static_cast<size_t>(j) * fp.w + r.x1] = static_cast<int32_t>(ri);
            ++r.x1;
        } else if (side == 2) {
            --r.z0;
            for (int i = r.x0; i < r.x1; ++i)
                occ[static_cast<size_t>(r.z0) * fp.w + i] = static_cast<int32_t>(ri);
        } else {
            for (int i = r.x0; i < r.x1; ++i)
                occ[static_cast<size_t>(r.z1) * fp.w + i] = static_cast<int32_t>(ri);
            ++r.z1;
        }
    };
    // Room proportions during growth stay under this ratio (§6.2
    // "пропорции в пределах"; the form term polices the rest).
    constexpr double kGrowRatio = 2.0;
    auto ratio_ok = [&](const PlanRoom& r, int side) {
        int rw = r.x1 - r.x0, rh = r.z1 - r.z0;
        if (side == 0 || side == 1) ++rw;
        else ++rh;
        const double ratio = static_cast<double>(std::max(rw, rh)) / std::min(rw, rh);
        return ratio <= kGrowRatio + kEps;
    };

    std::vector<bool> active(fp.rooms.size(), true);
    for (int guard = 0; guard < 4096; ++guard) {
        // Weighted pick among active rooms by deficit share (§6.2).
        double weights[64];
        int idx[64];
        int n = 0;
        for (size_t r = 0; r < fp.rooms.size() && n < 64; ++r) {
            if (!active[r]) continue;
            const PlanRoom& room = fp.rooms[r];
            if (room.area_cells() * s2 >= room.area_max) {
                active[r] = false;
                continue;
            }
            weights[n] = deficit(room) / std::max(1.0, room.target) + 0.05;
            idx[n] = static_cast<int>(r);
            ++n;
        }
        if (n == 0) break;
        const int ri = idx[rng.pick_weighted(weights, n)];
        PlanRoom& room = fp.rooms[static_cast<size_t>(ri)];
        // Rank sides: exterior first for habitable rooms (§6.2), entrance
        // rooms toward their facade; small tie noise.
        double side_score[4];
        for (int side = 0; side < 4; ++side) {
            double sc = 0.0;
            if (!strip_free(room, side) || !ratio_ok(room, side)) {
                sc = -1e30;
            } else {
                PlanRoom grown = room;
                if (side == 0) --grown.x0;
                if (side == 1) ++grown.x1;
                if (side == 2) --grown.z0;
                if (side == 3) ++grown.z1;
                if (grown.area_cells() * s2 > room.area_max + kEps) {
                    sc = -1e30;
                } else {
                    const bool touches_outside =
                        (side == 0 && grown.x0 == 0) || (side == 1 && grown.x1 == fp.w) ||
                        (side == 2 && grown.z0 == 0) || (side == 3 && grown.z1 == fp.h);
                    if (room.habitable && touches_outside) sc += 2.0;
                    if (room.entrance_front && side == 3) sc += 3.0;
                    if (room.entrance_back && side == 2) sc += 3.0;
                    sc += rng.f64() * 0.05;
                }
            }
            side_score[side] = sc;
        }
        int best_side = -1;
        double best_sc = -1e29;
        for (int side = 0; side < 4; ++side)
            if (side_score[side] > best_sc) {
                best_sc = side_score[side];
                best_side = side;
            }
        if (best_side < 0) {
            active[static_cast<size_t>(ri)] = false;
            continue;
        }
        apply_strip(room, best_side, ri);
    }

    // -- FILL (§6.2): absorb free cells rectangularly -----------------------
    // A room absorbs a free cell only with the whole bbox-expansion band,
    // which keeps the rectangle. Leftovers go unconditionally (the area
    // overrun is a soft term the anneal polices). Repeat until no free cell
    // is absorbable.
    for (bool changed = true; changed;) {
        changed = false;
        for (int j = 0; j < fp.h; ++j)
            for (int i = 0; i < fp.w; ++i) {
                if (occ[static_cast<size_t>(j) * fp.w + i] != kCellFree) continue;
                // Candidate rooms: 4 neighbors, best deficit first (§6.2:
                // "соседней комнате с наибольшим дефицитом").
                int neigh[4] = {-1, -1, -1, -1};
                if (i > 0) neigh[0] = occ[static_cast<size_t>(j) * fp.w + i - 1];
                if (i + 1 < fp.w) neigh[1] = occ[static_cast<size_t>(j) * fp.w + i + 1];
                if (j > 0) neigh[2] = occ[static_cast<size_t>(j - 1) * fp.w + i];
                if (j + 1 < fp.h) neigh[3] = occ[static_cast<size_t>(j + 1) * fp.w + i];
                int best_r = -1;
                double best_def = -1e30;
                for (int ni : neigh) {
                    if (ni < 0) continue;
                    PlanRoom& room = fp.rooms[static_cast<size_t>(ni)];
                    const int nx0 = std::min(room.x0, i), nx1 = std::max(room.x1, i + 1);
                    const int nz0 = std::min(room.z0, j), nz1 = std::max(room.z1, j + 1);
                    // The expansion band must be entirely free.
                    bool ok = true;
                    for (int zj = nz0; zj < nz1 && ok; ++zj)
                        for (int xi = nx0; xi < nx1; ++xi) {
                            const int32_t c = occ[static_cast<size_t>(zj) * fp.w + xi];
                            if (c != static_cast<int32_t>(ni) && c != kCellFree) {
                                ok = false;
                                break;
                            }
                        }
                    if (!ok) continue;
                    const double d = deficit(room);
                    if (d > best_def) {
                        best_def = d;
                        best_r = ni;
                    }
                }
                if (best_r >= 0) {
                    PlanRoom& room = fp.rooms[static_cast<size_t>(best_r)];
                    const int nx0 = std::min(room.x0, i), nx1 = std::max(room.x1, i + 1);
                    const int nz0 = std::min(room.z0, j), nz1 = std::max(room.z1, j + 1);
                    for (int zj = nz0; zj < nz1; ++zj)
                        for (int xi = nx0; xi < nx1; ++xi)
                            occ[static_cast<size_t>(zj) * fp.w + xi] =
                                static_cast<int32_t>(best_r);
                    room.x0 = nx0;
                    room.x1 = nx1;
                    room.z0 = nz0;
                    room.z1 = nz1;
                    changed = true;
                }
            }
    }

    // -- Repair: re-slice if stubborn free cells remain ---------------------
    bool complete = true;
    for (int32_t c : occ)
        if (c == kCellFree) {
            complete = false;
            break;
        }
    if (!complete && !repair_reslice(fp, err)) return fp;

    // Rebuild the final grid from the outside mask + room rects.
    fp.rasterize();
    if (std::string terr; !fp.check_rectangle_tiling(terr)) {
        err = "growth produced an invalid tiling: " + terr;
        return fp;
    }
    return fp;
}

}  // namespace building_topology_generator
