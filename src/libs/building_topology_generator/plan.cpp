// G5 initialization: cell field from the massing, seed desirability fields
// (algorithms §6.1), rectangle strip growth (§6.2 RECT), deficit fill, and a
// guillotine re-slice as the rectangle-tiling repair. B2: one storey, no
// cores, no corridors (§18 п. 2).

#include "pch.h"

#include "plan.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>

#include "roof.h"  // point_in_poly, build_roof_model, ceiling_y_at

#include "codes.h"

namespace building_topology_generator {

namespace {

constexpr double kEps = 1e-9;

}  // namespace

// Half-thickness the boundary against `label` shaves off a room (§5.2):
// outside -> an exterior wall, roofvoid -> a knee wall, void -> a railing
// (no wall body), core -> the stack face (§6: no wall either), a room -> a
// partition.
double side_half_t(int32_t label, const IrParams& p) {
    if (label == kCellOutside) return p.t_exterior / 2;
    if (label == kCellRoofVoid) return p.t_knee / 2;
    if (label == kCellVoid) return 0.0;
    if (label == kCellCore) return 0.0;
    return p.t_partition / 2;
}

void field_side_margins(const std::vector<int32_t>& mask, int w, int h,
                        const IrParams& ir, int x0, int z0, int x1, int z1,
                        double& t_x0, double& t_x1, double& t_z0, double& t_z1) {
    auto out_t = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= w || j >= h) return ir.t_exterior / 2;
        return side_half_t(mask[static_cast<size_t>(j) * w + i], ir);
    };
    t_x0 = t_x1 = t_z0 = t_z1 = 0.0;
    for (int j = z0; j < z1; ++j) {
        t_x0 = std::max(t_x0, out_t(x0 - 1, j));
        t_x1 = std::max(t_x1, out_t(x1, j));
    }
    for (int i = x0; i < x1; ++i) {
        t_z0 = std::max(t_z0, out_t(i, z0 - 1));
        t_z1 = std::max(t_z1, out_t(i, z1));
    }
}

void field_piece_net(const std::vector<int32_t>& occ,
                     const std::vector<int32_t>& margins_mask, int w, int h,
                     const IrParams& ir, std::vector<double>& piece_net) {
    piece_net.assign(static_cast<size_t>(w) * h, 0.0);
    // Integral image of the non-free cells for O(1) all-free queries.
    const int iw = w + 1;
    std::vector<int> blocked(static_cast<size_t>(iw) * (h + 1), 0);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int b = occ[static_cast<size_t>(j) * w + i] != kCellFree ? 1 : 0;
            blocked[static_cast<size_t>(j + 1) * iw + i + 1] =
                b + blocked[static_cast<size_t>(j) * iw + i + 1] +
                blocked[static_cast<size_t>(j + 1) * iw + i] -
                blocked[static_cast<size_t>(j) * iw + i];
        }
    auto blocked_in = [&](int x0, int z0, int x1, int z1) {
        return blocked[static_cast<size_t>(z1) * iw + x1] -
               blocked[static_cast<size_t>(z0) * iw + x1] -
               blocked[static_cast<size_t>(z1) * iw + x0] +
               blocked[static_cast<size_t>(z0) * iw + x0];
    };
    // The max net side over all-free squares (any feasible rect contains such
    // a square on its short side, and any such square is itself a feasible
    // rect, so the test is exact).
    for (int z0 = 0; z0 < h; ++z0)
        for (int x0 = 0; x0 < w; ++x0)
            for (int l = 1; z0 + l <= h && x0 + l <= w; ++l) {
                if (blocked_in(x0, z0, x0 + l, z0 + l) > 0) break;
                double t_x0, t_x1, t_z0, t_z1;
                field_side_margins(margins_mask, w, h, ir, x0, z0, x0 + l, z0 + l, t_x0,
                                   t_x1, t_z0, t_z1);
                const double net =
                    std::min(l * ir.cell - t_x0 - t_x1, l * ir.cell - t_z0 - t_z1);
                for (int j = z0; j < z0 + l; ++j)
                    for (int i = x0; i < x0 + l; ++i)
                        piece_net[static_cast<size_t>(j) * w + i] =
                            std::max(piece_net[static_cast<size_t>(j) * w + i], net);
            }
}

void FloorPlan::rasterize() {
    cells = outside;
    for (size_t r = 0; r < rooms.size(); ++r) {
        const PlanRoom& room = rooms[r];
        for (int j = room.z0; j < room.z1; ++j)
            for (int i = room.x0; i < room.x1; ++i) at(i, j) = static_cast<int32_t>(r);
        for (const std::array<int, 4>& wng : room.wings)
            for (int j = wng[1]; j < wng[3]; ++j)
                for (int i = wng[0]; i < wng[2]; ++i) at(i, j) = static_cast<int32_t>(r);
    }
}

bool FloorPlan::check_rectangle_tiling(std::string& err) const {
    // `seen` starts as the outside mask; rooms stamp over it. What stays
    // kCellFree was never claimed; a stamp over a masked cell (outside /
    // void / roof-void) or another room is an error.
    std::vector<int32_t> seen(outside);
    auto stamp = [&](size_t r, int x0, int z0, int x1, int z1) {
        const PlanRoom& room = rooms[r];
        if (x0 < 0 || z0 < 0 || x1 > w || z1 > h || x0 >= x1 || z0 >= z1) {
            err = "room " + room.id + " has a degenerate or out-of-field rect";
            return false;
        }
        for (int j = z0; j < z1; ++j)
            for (int i = x0; i < x1; ++i) {
                int32_t& c = seen[static_cast<size_t>(j) * w + i];
                if (c < 0 && c != kCellFree) {
                    err = "room " + room.id + " covers a masked cell (" + std::to_string(i) +
                          ", " + std::to_string(j) + ")";
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
        return true;
    };
    for (size_t r = 0; r < rooms.size(); ++r) {
        const PlanRoom& room = rooms[r];
        if (!stamp(r, room.x0, room.z0, room.x1, room.z1)) return false;
        for (const std::array<int, 4>& wng : room.wings)
            if (!stamp(r, wng[0], wng[1], wng[2], wng[3])) return false;
    }
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if (seen[static_cast<size_t>(j) * w + i] == kCellFree) {
                err = "cell (" + std::to_string(i) + ", " + std::to_string(j) + ") is free";
                return false;
            }
    // I10 (phase L): every room's cell set is 4-connected (a wing detached by
    // an editing bug would silently double a room).
    for (size_t r = 0; r < rooms.size(); ++r) {
        const PlanRoom& room = rooms[r];
        const int total = room.area_cells();
        int start = -1;
        for (size_t c = 0; c < seen.size() && start < 0; ++c)
            if (seen[c] == static_cast<int32_t>(r)) start = static_cast<int>(c);
        if (start < 0) {
            err = "room " + room.id + " covers no cells";
            return false;
        }
        std::vector<int> stack(1, start);
        std::vector<char> vis(seen.size(), 0);
        vis[static_cast<size_t>(start)] = 1;
        int reached = 0;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            ++reached;
            const int ci = c % w, cj = c / w;
            const int di[4] = {-1, 1, 0, 0};
            const int dj[4] = {0, 0, -1, 1};
            for (int q = 0; q < 4; ++q) {
                const int ni = ci + di[q], nj = cj + dj[q];
                if (!inside(ni, nj)) continue;
                const size_t nc = static_cast<size_t>(nj) * w + ni;
                if (!vis[nc] && seen[nc] == static_cast<int32_t>(r)) {
                    vis[nc] = 1;
                    stack.push_back(static_cast<int>(nc));
                }
            }
        }
        if (reached != total) {
            err = "room " + room.id + " is not 4-connected (" + std::to_string(reached) +
                  " of " + std::to_string(total) + " cells)";
            return false;
        }
    }
    return true;
}

int FloorPlan::find_room(const std::string& id) const {
    for (size_t r = 0; r < rooms.size(); ++r)
        if (rooms[r].id == id) return static_cast<int>(r);
    return -1;
}

bool room_covering_ok(const FloorPlan& fp, int r, int m_cells) {
    const PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
    const int32_t want = static_cast<int32_t>(r);
    // For each of the room's cells, test any m x m square covering it; the
    // main rect carries the full tier, the wings the passage tier (plan.h).
    for (int j = 0; j < fp.h; ++j)
        for (int i = 0; i < fp.w; ++i) {
            if (fp.at(i, j) != want) continue;
            const bool in_main =
                i >= room.x0 && i < room.x1 && j >= room.z0 && j < room.z1;
            const int m = in_main ? m_cells : std::min(m_cells, kWingCoverCells);
            if (m <= 1) continue;  // a cell covers itself
            bool covered = false;
            for (int sj = j - m + 1; sj <= j && !covered; ++sj)
                for (int si = i - m + 1; si <= i && !covered; ++si) {
                    bool ok = true;
                    for (int qj = sj; qj < sj + m && ok; ++qj)
                        for (int qi = si; qi < si + m && ok; ++qi)
                            if (!fp.inside(qi, qj) || fp.at(qi, qj) != want) ok = false;
                    covered = ok;
                }
            if (!covered) return false;
        }
    return true;
}

void FloorPlan::room_dims_net(int r, const IrParams& p, double& w_m, double& d_m) const {
    const PlanRoom& room = rooms[static_cast<size_t>(r)];
    const double s = p.cell;
    // Wall half-thickness per side, scanned over the whole side: the
    // thickest wall along the side wins (an exterior piece dominates a
    // mixed side). Requires a rasterized `cells` grid.
    auto neighbor_t = [&](int i, int j) {
        const int32_t n = inside(i, j) ? at(i, j) : kCellOutside;
        return side_half_t(n, p);
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
    const PlanRoom& room = rooms[static_cast<size_t>(r)];
    if (!useful.empty() || room.l_shaped()) {
        // Cell-based: sum the room's cells (attic §5.6: only the cells whose
        // worst corner clears h_walk count), minus the wall allowance along
        // the room boundary. Identical to the bbox scan for plain rects; the
        // only correct form for winged rooms.
        const double s = p.cell;
        double area = 0.0;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                if (at(i, j) != static_cast<int32_t>(r)) continue;
                if (!useful.empty() && !useful[static_cast<size_t>(j) * w + i]) continue;
                area += s * s;
                const int di[4] = {-1, 1, 0, 0};
                const int dj[4] = {0, 0, -1, 1};
                for (int q = 0; q < 4; ++q) {
                    const int32_t n = room_at(i + di[q], j + dj[q]);
                    if (n != static_cast<int32_t>(r)) area -= side_half_t(n, p) * s;
                }
            }
        return std::max(0.0, area);
    }
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

bool storey_fields(const Ir& ir, StoreyFields& out, std::string& err) {
    if (ir.volumes.empty() || ir.storeys.empty()) {
        err = "storey_fields: the IR has no volumes/storeys";
        return false;
    }
    const IrParams& params = ir.params;
    const double s = params.cell;
    const double t = params.t_exterior;
    // The grid frame comes from the union bbox of all volumes (the ground
    // footprint; §5.1). Upper storeys only re-mark the cells.
    double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;
    for (const IrVolume& v : ir.volumes)
        for (const glm::dvec2& p : v.outline) {
            x0 = std::min(x0, p.x);
            z0 = std::min(z0, p.y);
            x1 = std::max(x1, p.x);
            z1 = std::max(z1, p.y);
        }
    const glm::dvec2 field0(x0 + t / 2, z0 + t / 2);
    const int w = static_cast<int>(std::lround((x1 - x0 - t) / s));
    const int h = static_cast<int>(std::lround((z1 - z0 - t) / s));
    if (w <= 0 || h <= 0) {
        err = "storey_fields: the massing field is empty";
        return false;
    }
    out.w = w;
    out.h = h;
    out.origin = field0;

    // Roof model per volume (the attic masks need the true ceiling field).
    std::vector<RoofModel> models(ir.volumes.size());
    for (size_t vi = 0; vi < ir.volumes.size(); ++vi)
        if (!build_roof_model(ir.volumes[vi], models[vi], err)) {
            err = "storey_fields: volume " + ir.volumes[vi].id + ": " + err;
            return false;
        }

    const int n_storeys = static_cast<int>(ir.storeys.size());
    out.mask.assign(static_cast<size_t>(n_storeys), {});
    out.walk.assign(static_cast<size_t>(n_storeys), {});
    for (int k = 0; k < n_storeys; ++k) {
        const IrStorey& st = ir.storeys[static_cast<size_t>(k)];
        const bool is_attic = st.kind_code == kStoreyAttic;
        auto& mask = out.mask[static_cast<size_t>(k)];
        auto& walk = out.walk[static_cast<size_t>(k)];
        mask.assign(static_cast<size_t>(w) * h, kCellOutside);
        if (is_attic) walk.assign(static_cast<size_t>(w) * h, 0);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const double px = field0.x + (i + 0.5) * s;
                const double pz = field0.y + (j + 0.5) * s;
                size_t cov = ir.volumes.size();
                for (size_t vi = 0; vi < ir.volumes.size(); ++vi) {
                    const IrVolume& v = ir.volumes[vi];
                    if (k < v.storey0 || k > v.storey1) continue;
                    if (point_in_poly(v.outline, px, pz)) {
                        cov = vi;
                        break;
                    }
                }
                if (cov >= ir.volumes.size()) continue;
                if (!is_attic) {
                    mask[static_cast<size_t>(j) * w + i] = kCellFree;
                    continue;
                }
                // Attic (§5.6): the worst cell corner sets the class — below
                // h_knee the cell is roof-void (floor, no room), at/above
                // h_walk it counts into the useful area.
                bool knee_ok = true, walk_ok = true;
                for (int ci = 0; ci <= 1; ++ci)
                    for (int cj = 0; cj <= 1; ++cj) {
                        double cy = 0.0;
                        const double cx = field0.x + (i + ci) * s;
                        const double cz = field0.y + (j + cj) * s;
                        const bool has = ceiling_y_at(models[cov], cx, cz, cy);
                        if (!has || cy < st.y + params.h_knee) knee_ok = false;
                        if (!has || cy < st.y + params.h_walk()) walk_ok = false;
                    }
                mask[static_cast<size_t>(j) * w + i] = knee_ok ? kCellFree : kCellRoofVoid;
                walk[static_cast<size_t>(j) * w + i] = walk_ok ? 1 : 0;
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
// the caller). `ent` is the entrance/arrival attractor in cell coords.
double seed_desirability(const PlanRoom& room, int i, int j, const FloorPlan& fp,
                         const std::vector<const PlanRoom*>& placed,
                         const std::vector<std::pair<std::pair<int, int>, double>>& linked,
                         const SeedField& f, bool front_entrance, const glm::dvec2& ent) {
    const double s = 0.5;  // only ratios matter here
    double score = 0.0;
    // Exterior wall contact (any side).
    const bool at_wall = (i == 0 || j == 0 || i == fp.w - 1 || j == fp.h - 1);
    if (room.habitable && at_wall) score += f.a_ext;
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
// whole row is free). Covers every inside cell exactly once. A cell is
// tileable iff it is inside the field: kCellFree or a room index (outside /
// void / roof-void labels and pinned rooms are blocked).
bool tileable_cell(int32_t c) { return c >= 0 || c == kCellFree; }

std::vector<std::array<int, 4>> decompose_field(const std::vector<int32_t>& mask, int w,
                                                int h) {
    std::vector<char> taken(static_cast<size_t>(w) * h, 0);
    std::vector<std::array<int, 4>> rects;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            if (taken[static_cast<size_t>(j) * w + i] ||
                !tileable_cell(mask[static_cast<size_t>(j) * w + i]))
                continue;
            int x1 = i;
            while (x1 + 1 < w && !taken[static_cast<size_t>(j) * w + x1 + 1] &&
                   tileable_cell(mask[static_cast<size_t>(j) * w + x1 + 1]))
                ++x1;
            int z1 = j;
            for (bool ok = true; ok && z1 + 1 < h;) {
                for (int x = i; x <= x1; ++x)
                    if (taken[static_cast<size_t>(z1 + 1) * w + x] ||
                        !tileable_cell(mask[static_cast<size_t>(z1 + 1) * w + x])) {
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

// Shared state of one field_reslice_feasible call / one repair-side set
// check: the storey field, the room set ((min_dim, target)) and the memo of
// the recursive piece feasibility.
struct ResliceCtx {
    const std::vector<int32_t>* margins = nullptr;
    int w = 0, h = 0;
    IrParams ir;
    std::vector<std::pair<double, double>> rooms;  // (min_dim, target)
    std::map<std::pair<std::array<int, 4>, int>, char> memo;
};

// Recursive feasibility of hosting the room set (bitmask over cx.rooms) in
// the rect: mirrors guillotine()'s net min_dim cut rule, with free
// bipartitions and a free split span (the growth-time centroid order and the
// target share are unknowable pre-growth — any subset may take the low side).
bool rect_set_feasible(ResliceCtx& cx, int x0, int z0, int x1, int z1, int set) {
    const int n = std::popcount(static_cast<unsigned>(set));
    if (n == 0) return true;  // orphan piece: a future wing
    double t_x0, t_x1, t_z0, t_z1;
    field_side_margins(*cx.margins, cx.w, cx.h, cx.ir, x0, z0, x1, z1, t_x0, t_x1, t_z0,
                       t_z1);
    if (n == 1) {
        const double md =
            cx.rooms[static_cast<size_t>(std::countr_zero(static_cast<unsigned>(set)))].first;
        const double net_w = (x1 - x0) * cx.ir.cell - t_x0 - t_x1;
        const double net_h = (z1 - z0) * cx.ir.cell - t_z0 - t_z1;
        return std::min(net_w, net_h) >= md - kEps;
    }
    const int w = x1 - x0, h = z1 - z0;
    if (w < 2 && h < 2) return false;
    const auto key = std::make_pair(std::array<int, 4>{x0, z0, x1, z1}, set);
    const auto it = cx.memo.find(key);
    if (it != cx.memo.end()) return it->second != 0;
    const int lowest = set & -set;  // complement symmetry: the lowest bit stays low
    double total = 0.0;
    for (int r = 0; r < static_cast<int>(cx.rooms.size()); ++r)
        if (set & (1 << r)) total += cx.rooms[static_cast<size_t>(r)].second;
    auto try_axis = [&](bool vertical) {
        const int axis = vertical ? w : h;
        if (axis < 2) return false;
        const double t_out_a = vertical ? t_x0 : t_z0;  // the low side's allowance
        const double t_out_b = vertical ? t_x1 : t_z1;
        auto min_cells_net = [&](int r, double t_out) {
            const double need =
                cx.rooms[static_cast<size_t>(r)].first + t_out + cx.ir.t_partition / 2;
            return std::max(1, static_cast<int>(std::ceil(need / cx.ir.cell - kEps)));
        };
        for (int a = (set - 1) & set; a > 0; a = (a - 1) & set) {
            if (!(a & lowest)) continue;
            const int b = set ^ a;
            int m_a = 1, m_b = 1;
            double share = 0.0;
            for (int r = 0; r < static_cast<int>(cx.rooms.size()); ++r) {
                if (a & (1 << r)) {
                    m_a = std::max(m_a, min_cells_net(r, t_out_a));
                    share += cx.rooms[static_cast<size_t>(r)].second;
                }
                if (b & (1 << r)) m_b = std::max(m_b, min_cells_net(r, t_out_b));
            }
            if (axis < m_a + m_b) continue;
            const int guess = total > 0.0
                                  ? std::clamp(static_cast<int>(std::lround(axis * share / total)),
                                               m_a, axis - m_b)
                                  : (m_a + axis - m_b) / 2;
            for (const int span : {guess, m_a, axis - m_b}) {
                if (span < m_a || span > axis - m_b) continue;
                const int xc = vertical ? x0 + span : x1;
                const int zc = vertical ? z1 : z0 + span;
                if (rect_set_feasible(cx, x0, z0, xc, zc, a) &&
                    rect_set_feasible(cx, vertical ? x0 + span : x0,
                                      vertical ? z0 : z0 + span, x1, z1, b))
                    return true;
            }
        }
        return false;
    };
    // The preferred direction first, as in guillotine().
    const bool prefer_vertical = (w >= 2 && w >= h) || h < 2;
    const bool ok = try_axis(prefer_vertical) || try_axis(!prefer_vertical);
    cx.memo[key] = ok ? 1 : 0;
    return ok;
}

// Wall allowances of a field piece's four sides, scanned in fp.outside:
// beyond the field -> t_exterior/2, roof-void -> t_knee/2, anything else
// (a future room, the pinned shaft) -> t_partition/2. Matches room_dims_net
// on the final raster, so a piece that passes here stays min_dim-legal once
// the rooms are stamped.
void piece_side_margins(const FloorPlan& fp, const IrParams& ir, int x0, int z0, int x1,
                        int z1, double& t_x0, double& t_x1, double& t_z0, double& t_z1) {
    field_side_margins(fp.outside, fp.w, fp.h, ir, x0, z0, x1, z1, t_x0, t_x1, t_z0, t_z1);
}

// Recursively split the rect [x0,x1)x[z0,z1) among rooms[lo, hi) weighted by
// their area targets. Deterministic: split direction by the longer side, the
// room subset by a median split along the split axis (rooms are pre-sorted
// by their pin/centroid projection — a room with a pinned seed cell keys on
// the pin). Every side of a cut must leave every hosted room its NET min_dim
// — gross cells minus the wall allowances (the piece's envelope side plus the
// cut's partition) — and every pinned room a slice containing its pin cell; a
// thinner slice is a strip the anneal cannot legally escape (shift_legal
// keeps the neighbors above their own min_dim), so an infeasible split fails
// the slice and the growth retries. The preferred direction is tried first,
// then the other one.
bool guillotine(FloorPlan& fp, std::vector<int>& ord, int lo, int hi, int x0, int z0,
                int x1, int z1, const IrParams& ir, std::string& err) {
    const double cell = ir.cell;
    double t_x0, t_x1, t_z0, t_z1;
    piece_side_margins(fp, ir, x0, z0, x1, z1, t_x0, t_x1, t_z0, t_z1);
    if (hi - lo == 1) {
        PlanRoom& r = fp.rooms[static_cast<size_t>(ord[static_cast<size_t>(lo)])];
        // A pinned seed cell must survive in the room's slice (B4): the pin
        // is the stack/shaft contact — a slice missing it ends the plan with
        // a hard term the anneal cannot always repair.
        if (r.pin_cell.x >= 0 &&
            (r.pin_cell.x < x0 || r.pin_cell.x >= x1 || r.pin_cell.y < z0 ||
             r.pin_cell.y >= z1)) {
            err = "guillotine: the slice lost the pin of room " + r.id;
            return false;
        }
        const double net_w = (x1 - x0) * cell - t_x0 - t_x1;
        const double net_h = (z1 - z0) * cell - t_z0 - t_z1;
        if (std::min(net_w, net_h) < r.min_dim - 1e-6) {
            err = "guillotine: the piece is too narrow for room " + r.id + " (min_dim)";
            return false;
        }
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
    auto split = [&](bool vertical, std::string& serr) {
        // Sort along the axis: a pinned room keys on its pin (the stack/shaft
        // contact it must keep), the rest on their centroid.
        auto key_of = [&](int r) {
            const PlanRoom& R = fp.rooms[static_cast<size_t>(r)];
            if (R.pin_cell.x >= 0)
                return vertical ? R.pin_cell.x + 0.5 : R.pin_cell.y + 0.5;
            return vertical ? (R.x0 + R.x1) / 2.0 : (R.z0 + R.z1) / 2.0;
        };
        std::sort(ord.begin() + lo, ord.begin() + hi, [&](int a, int b) {
            const double ca = key_of(a);
            const double cb = key_of(b);
            if (ca != cb) return ca < cb;
            return fp.rooms[static_cast<size_t>(a)].id < fp.rooms[static_cast<size_t>(b)].id;
        });
        double total = 0.0;
        for (int k = lo; k < hi; ++k) total += fp.rooms[static_cast<size_t>(ord[k])].target;
        // Split position: cumulative target share, at least one room per
        // side; the span then clamps into the min_dim-feasible range. The
        // cell counts are net-aware: the outer side carries the piece's
        // scanned allowance, the cut side a partition.
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
        // Pin separability: every low-side pin must sit strictly below every
        // high-side pin on this axis (an equal projection — same column —
        // cannot separate here; the other axis may still work).
        int pin_lo_max = std::numeric_limits<int>::min();
        int pin_hi_min = std::numeric_limits<int>::max();
        for (int k = lo; k < cut_rooms; ++k) {
            const PlanRoom& R = fp.rooms[static_cast<size_t>(ord[k])];
            if (R.pin_cell.x >= 0)
                pin_lo_max = std::max(pin_lo_max, vertical ? R.pin_cell.x : R.pin_cell.y);
        }
        for (int k = cut_rooms; k < hi; ++k) {
            const PlanRoom& R = fp.rooms[static_cast<size_t>(ord[k])];
            if (R.pin_cell.x >= 0)
                pin_hi_min = std::min(pin_hi_min, vertical ? R.pin_cell.x : R.pin_cell.y);
        }
        if (pin_lo_max != std::numeric_limits<int>::min() &&
            pin_hi_min != std::numeric_limits<int>::max() && pin_lo_max >= pin_hi_min)
            return false;
        const double t_out_a = vertical ? t_x0 : t_z0;  // the low side's allowance
        const double t_out_b = vertical ? t_x1 : t_z1;  // the high side's
        auto min_cells_net = [&](int r, double t_out) {
            const double need =
                fp.rooms[static_cast<size_t>(r)].min_dim + t_out + ir.t_partition / 2;
            return std::max(1, static_cast<int>(std::ceil(need / cell - kEps)));
        };
        int m_a = 1, m_b = 1;
        for (int k = lo; k < cut_rooms; ++k)
            m_a = std::max(m_a, min_cells_net(ord[k], t_out_a));
        for (int k = cut_rooms; k < hi; ++k)
            m_b = std::max(m_b, min_cells_net(ord[k], t_out_b));
        const int axis = vertical ? w : h;
        if (axis < m_a + m_b) return false;  // infeasible along this axis
        // Pin containment: the low slice [base, base+span) must cover every
        // low-side pin (span > pin - base), the high slice [base+span, …)
        // every high-side pin (span <= pin - base).
        const int base = vertical ? x0 : z0;
        int span_lo = m_a, span_hi = axis - m_b;
        for (int k = lo; k < cut_rooms; ++k) {
            const PlanRoom& R = fp.rooms[static_cast<size_t>(ord[k])];
            if (R.pin_cell.x >= 0)
                span_lo = std::max(span_lo,
                                   (vertical ? R.pin_cell.x : R.pin_cell.y) - base + 1);
        }
        for (int k = cut_rooms; k < hi; ++k) {
            const PlanRoom& R = fp.rooms[static_cast<size_t>(ord[k])];
            if (R.pin_cell.x >= 0)
                span_hi = std::min(span_hi, (vertical ? R.pin_cell.x : R.pin_cell.y) - base);
        }
        if (span_lo > span_hi) return false;
        double left_share = 0.0;
        for (int k = lo; k < cut_rooms; ++k)
            left_share += fp.rooms[static_cast<size_t>(ord[k])].target;
        const double frac = total > 0.0 ? left_share / total : 0.5;
        const int span =
            std::clamp(static_cast<int>(std::lround(axis * frac)), span_lo, span_hi);
        if (vertical) {
            const int xc = x0 + span;
            return guillotine(fp, ord, lo, cut_rooms, x0, z0, xc, z1, ir, serr) &&
                   guillotine(fp, ord, cut_rooms, hi, xc, z0, x1, z1, ir, serr);
        }
        const int zc = z0 + span;
        return guillotine(fp, ord, lo, cut_rooms, x0, z0, x1, zc, ir, serr) &&
               guillotine(fp, ord, cut_rooms, hi, x0, zc, x1, z1, ir, serr);
    };
    const bool prefer_vertical = (w >= 2 && w >= h) || h < 2;
    if (split(prefer_vertical, err)) return true;
    if (split(!prefer_vertical, err)) return true;
    if (err.empty())
        err = "guillotine: " + std::to_string(hi - lo) + " rooms do not fit min_dim in a " +
              std::to_string(w) + "x" + std::to_string(h) + " piece";
    return false;
}

// Re-slice the whole storey: decompose the field, attach rooms to rects by
// centroid, donate rooms to orphan rects, guillotine each rect. Pinned rooms
// (the stair shaft) are excluded from the re-slice — their cells are blocked.
// An orphan rect with no donor room becomes a WING of an edge-adjacent
// single-room rect (phase L): the attachment is gated by the I10 covering
// test on the union {piece + its wings + candidate}, m from the room's
// min_dim. `ir` gives the cell size and the wall thicknesses.
bool repair_reslice(FloorPlan& fp, const IrParams& ir, std::string& err) {
    const double cell = ir.cell;
    // The re-slice redistributes every non-pinned cell, so wings from an
    // earlier absorb pass would double-claim cells with the new rects; the
    // wing fixpoint below re-creates whatever the new tiling needs.
    for (PlanRoom& room : fp.rooms) room.wings.clear();
    std::vector<int32_t> mask = fp.outside;
    for (const PlanRoom& room : fp.rooms) {
        if (!room.pinned) continue;
        for (int j = room.z0; j < room.z1; ++j)
            for (int i = room.x0; i < room.x1; ++i)
                mask[static_cast<size_t>(j) * fp.w + i] = kCellOutside;
    }
    std::vector<std::array<int, 4>> rects = decompose_field(mask, fp.w, fp.h);
    // Around a pinned shaft the maximal-run decomposition shatters the ring
    // into more rects than the storey has rooms; the union-merge restores the
    // tileable pieces.
    merge_rect_pairs(rects);
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
        if (room.pinned) continue;
        const int k = rect_of((room.x0 + room.x1) / 2.0, (room.z0 + room.z1) / 2.0);
        if (k >= 0) rect_rooms[static_cast<size_t>(k)].push_back(static_cast<int>(ri));
    }
    // Feasibility pass: the growth put each room where it wanted, not where it
    // fits. A guillotine slice always spans the piece's full side, so a room
    // can only live in a piece whose smaller NET side clears the room's
    // min_dim; a misfit room moves to the least crowded fitting piece
    // (tie: the nearest). Rooms nobody can host stay put — the guillotine
    // then fails honestly and the growth retries.
    auto piece_net_min = [&](size_t k) {
        double t_x0, t_x1, t_z0, t_z1;
        piece_side_margins(fp, ir, rects[k][0], rects[k][1], rects[k][2], rects[k][3],
                           t_x0, t_x1, t_z0, t_z1);
        return std::min((rects[k][2] - rects[k][0]) * cell - t_x0 - t_x1,
                        (rects[k][3] - rects[k][1]) * cell - t_z0 - t_z1);
    };
    for (bool moved = true; moved;) {
        moved = false;
        for (size_t k = 0; k < rects.size(); ++k) {
            const double net_min_k = piece_net_min(k);
            for (size_t q = 0; q < rect_rooms[k].size(); ++q) {
                const PlanRoom& room = fp.rooms[static_cast<size_t>(rect_rooms[k][q])];
                if (room.min_dim <= net_min_k + kEps) continue;
                const double rcx = (room.x0 + room.x1) / 2.0;
                const double rcz = (room.z0 + room.z1) / 2.0;
                int best_t = -1;
                size_t best_load = 1 << 30;
                double best_dist = 1e30;
                for (size_t t = 0; t < rects.size(); ++t) {
                    if (t == k || room.min_dim > piece_net_min(t) + kEps) continue;
                    const double dx = (rects[t][0] + rects[t][2]) / 2.0 - rcx;
                    const double dz = (rects[t][1] + rects[t][3]) / 2.0 - rcz;
                    const double dist = dx * dx + dz * dz;
                    if (rect_rooms[t].size() < best_load ||
                        (rect_rooms[t].size() == best_load && dist < best_dist - kEps)) {
                        best_t = static_cast<int>(t);
                        best_load = rect_rooms[t].size();
                        best_dist = dist;
                    }
                }
                if (best_t < 0) continue;
                rect_rooms[static_cast<size_t>(best_t)].push_back(rect_rooms[k][q]);
                rect_rooms[k].erase(rect_rooms[k].begin() + static_cast<long>(q));
                moved = true;
                break;
            }
            if (moved) break;
        }
    }
    // Hearth rooms (§9.2) must stay in a piece touching their chimney stack:
    // the centroid attachment knows nothing about the cores, and a hearth
    // room re-sliced away from its stack ends the plan with a hard hearth
    // term the anneal cannot always repair (a winged room is fixed). Move
    // each such room to the least crowded core-adjacent piece that fits its
    // min_dim (same preference as the misfit pass); a room nobody can host
    // stays put — the hearth hard term owns the failure.
    std::vector<char> core_adj(rects.size(), 0);
    for (size_t k = 0; k < rects.size(); ++k) {
        static const int di[4] = {-1, 1, 0, 0};
        static const int dj[4] = {0, 0, -1, 1};
        for (int j = rects[k][1]; j < rects[k][3] && !core_adj[k]; ++j)
            for (int i = rects[k][0]; i < rects[k][2] && !core_adj[k]; ++i)
                for (int q = 0; q < 4; ++q) {
                    const int ni = i + di[q], nj = j + dj[q];
                    if (ni < 0 || nj < 0 || ni >= fp.w || nj >= fp.h) continue;
                    if (fp.outside[static_cast<size_t>(nj) * fp.w + ni] == kCellCore)
                        core_adj[k] = 1;
                }
    }
    // Pin-first attachment (B4): a room with a pinned seed cell (a hearth
    // room's stack contact, the auto-corridor's shaft door side) belongs to
    // the piece CONTAINING the pin — the centroid attachment may have put it
    // in a different (even core-adjacent) piece, where the guillotine cannot
    // give it a slice containing the pin and the plan ends hard. Move it
    // there when the piece fits its min_dim; the pin-aware guillotine and the
    // joint fixpoint then settle the crowding.
    for (size_t ri = 0; ri < fp.rooms.size(); ++ri) {
        const PlanRoom& room = fp.rooms[ri];
        if (room.pinned || room.pin_cell.x < 0) continue;
        const int pp = static_cast<int>(
            rect_of(room.pin_cell.x + 0.5, room.pin_cell.y + 0.5));
        if (pp < 0 || room.min_dim > piece_net_min(static_cast<size_t>(pp)) + kEps)
            continue;
        int home = -1;
        for (size_t k = 0; k < rects.size(); ++k)
            if (std::find(rect_rooms[k].begin(), rect_rooms[k].end(),
                          static_cast<int>(ri)) != rect_rooms[k].end()) {
                home = static_cast<int>(k);
                break;
            }
        if (home == pp) continue;
        if (home >= 0) {
            auto& src = rect_rooms[static_cast<size_t>(home)];
            src.erase(std::find(src.begin(), src.end(), static_cast<int>(ri)));
        }
        rect_rooms[static_cast<size_t>(pp)].push_back(static_cast<int>(ri));
    }
    for (size_t ri = 0; ri < fp.rooms.size(); ++ri) {
        const PlanRoom& room = fp.rooms[ri];
        if (room.pinned || room.hearth_code == 0) continue;
        int home = -1;
        for (size_t k = 0; k < rects.size(); ++k)
            if (std::find(rect_rooms[k].begin(), rect_rooms[k].end(),
                          static_cast<int>(ri)) != rect_rooms[k].end()) {
                home = static_cast<int>(k);
                break;
            }
        if (home < 0 || core_adj[static_cast<size_t>(home)]) continue;
        const double hx = (room.x0 + room.x1) / 2.0, hz = (room.z0 + room.z1) / 2.0;
        int best_t = -1;
        size_t best_load = 1 << 30;
        double best_dist = 1e30;
        for (size_t t = 0; t < rects.size(); ++t) {
            if (!core_adj[t] || room.min_dim > piece_net_min(t) + kEps) continue;
            const double dx = (rects[t][0] + rects[t][2]) / 2.0 - hx;
            const double dz = (rects[t][1] + rects[t][3]) / 2.0 - hz;
            const double dist = dx * dx + dz * dz;
            if (rect_rooms[t].size() < best_load ||
                (rect_rooms[t].size() == best_load && dist < best_dist - kEps)) {
                best_t = static_cast<int>(t);
                best_load = rect_rooms[t].size();
                best_dist = dist;
            }
        }
        if (best_t < 0) continue;
        auto& src = rect_rooms[static_cast<size_t>(home)];
        src.erase(std::find(src.begin(), src.end(), static_cast<int>(ri)));
        rect_rooms[static_cast<size_t>(best_t)].push_back(static_cast<int>(ri));
    }
    // Orphan rects (no room centroid landed inside): prefer the nearest room
    // donated by the most crowded rect (rooms stay rectangular); with no
    // donor the orphan becomes a wing of an edge-adjacent room after the
    // guillotine (phase L wing path below).
    std::vector<char> pending(rects.size(), 0);
    size_t orphans_left = 0;
    for (size_t k = 0; k < rects.size(); ++k)
        if (rect_rooms[k].empty()) {
            pending[k] = 1;
            ++orphans_left;
        }
    // Donation first: a room of a crowded rect moves into the orphan (rooms
    // stay rectangular). The donated room must be able to occupy the orphan
    // ALONE — its NET dims (gross minus the wall allowance of the piece
    // boundary, scanned in fp.outside: the envelope takes t_exterior/2, a
    // roof-void t_knee/2, a future room t_partition/2) have to clear min_dim.
    // A 2-row band above the stair shaft nets 0.88 m — donating the corridor
    // there would just move the infeasibility where the anneal cannot reach
    // it (the shaft is fixed, so no shift unit crosses the boundary).
    // Preference: the most crowded rect, then the nearest eligible centroid.
    for (size_t k = 0; k < rects.size(); ++k) {
        if (!pending[k]) continue;
        const int pw = rects[k][2] - rects[k][0], ph = rects[k][3] - rects[k][1];
        const double cx = (rects[k][0] + rects[k][2]) / 2.0;
        const double cz = (rects[k][1] + rects[k][3]) / 2.0;
        double t_x0, t_x1, t_z0, t_z1;
        piece_side_margins(fp, ir, rects[k][0], rects[k][1], rects[k][2], rects[k][3],
                           t_x0, t_x1, t_z0, t_z1);
        const double net_min =
            std::min(pw * cell - t_x0 - t_x1, ph * cell - t_z0 - t_z1);
        int best_d = -1, best_q = -1;
        size_t best_crowd = 1;
        double best_dist = 1e30;
        for (size_t d = 0; d < rects.size(); ++d) {
            if (rect_rooms[d].size() < 2) continue;
            for (size_t q = 0; q < rect_rooms[d].size(); ++q) {
                const PlanRoom& room = fp.rooms[static_cast<size_t>(rect_rooms[d][q])];
                if (room.pin_cell.x >= 0) continue;  // pins stay in their piece (B4)
                if (room.min_dim > net_min + kEps) continue;
                const double dx = (room.x0 + room.x1) / 2.0 - cx;
                const double dz = (room.z0 + room.z1) / 2.0 - cz;
                const double dist = dx * dx + dz * dz;
                if (rect_rooms[d].size() > best_crowd ||
                    (rect_rooms[d].size() == best_crowd && dist < best_dist - kEps)) {
                    best_d = static_cast<int>(d);
                    best_q = static_cast<int>(q);
                    best_crowd = rect_rooms[d].size();
                    best_dist = dist;
                }
            }
        }
        if (best_d < 0) continue;  // no eligible donor — the wing fixpoint tries
        auto& pool = rect_rooms[static_cast<size_t>(best_d)];
        rect_rooms[k].push_back(pool[static_cast<size_t>(best_q)]);
        pool.erase(pool.begin() + best_q);
        pending[k] = 0;
        --orphans_left;
    }
    // Guillotine the room-ful pieces FIRST: the wing pass below attaches
    // orphans to the rooms' FINAL rects, so a strip bordering a multi-room
    // piece (the band a stair shaft brackets against the field edge) still
    // finds its hosts — before the slice it could only wing onto a
    // single-room piece.
    // Joint-infeasibility fixpoint: the growth put the rooms where it wanted,
    // so two rooms that fit a piece individually may still be unable to share
    // it (the guillotine's m_a + m_b > axis). The earlier passes only move
    // INDIVIDUAL misfits — on a joint failure, re-home the hardest room of
    // the failed piece to the least crowded piece whose joint set stays
    // guillotine-feasible (rect_set_feasible mirrors the slice rule) and
    // re-slice. No target -> the honest failure stands (the growth retries).
    ResliceCtx rcx;
    rcx.margins = &fp.outside;
    rcx.w = fp.w;
    rcx.h = fp.h;
    rcx.ir = ir;
    for (const PlanRoom& room : fp.rooms)
        rcx.rooms.emplace_back(room.min_dim, room.target);
    if (std::getenv("BTG_DEBUG_DUMP")) {
        bool has_hearth = false;
        for (const PlanRoom& room : fp.rooms) has_hearth = has_hearth || room.hearth_code != 0;
        if (has_hearth) {
            std::string d = "repair pieces (storey " + std::to_string(fp.storey_index) + "):\n";
            for (size_t k = 0; k < rects.size(); ++k) {
                d += "  [" + std::to_string(rects[k][0]) + "," + std::to_string(rects[k][1]) +
                     "-" + std::to_string(rects[k][2]) + "," + std::to_string(rects[k][3]) + "]" +
                     (core_adj[k] ? " core_adj" : "") + " rooms:";
                for (const int r : rect_rooms[k]) {
                    const PlanRoom& rm = fp.rooms[static_cast<size_t>(r)];
                    d += " " + rm.id + (rm.hearth_code ? "(H)" : "") + "@(" +
                         std::to_string((rm.x0 + rm.x1) / 2) + "," +
                         std::to_string((rm.z0 + rm.z1) / 2) + ")";
                }
                d += "\n";
            }
            std::fprintf(stderr, "%s", d.c_str());
        }
    }
    const bool can_fix = fp.rooms.size() <= 24;  // the set bitmask is an int
    auto dump_failure = [&](const char* why, int failed) {
        if (!std::getenv("BTG_DEBUG_DUMP")) return;
        std::string d = std::string("repair joint-fixpoint failed (") + why + "): storey " +
                        std::to_string(fp.storey_index) + "\n";
        for (size_t k = 0; k < rects.size(); ++k) {
            d += "  piece [" + std::to_string(rects[k][0]) + "," + std::to_string(rects[k][1]) +
                 " - " + std::to_string(rects[k][2]) + "," + std::to_string(rects[k][3]) + "]" +
                 (static_cast<int>(k) == failed ? " FAILED" : "") +
                 (core_adj[k] ? " core_adj" : "") + " net_min=" +
                 std::to_string(piece_net_min(k)) + " rooms:";
            for (const int r : rect_rooms[k]) {
                const PlanRoom& rm = fp.rooms[static_cast<size_t>(r)];
                d += " " + rm.id + "(md=" + std::to_string(rm.min_dim) +
                     ",tg=" + std::to_string(rm.target) +
                     (rm.hearth_code ? ",hearth" : "") + ")";
            }
            d += "\n";
        }
        std::fprintf(stderr, "%s", d.c_str());
    };
    for (int guard = 0;; ++guard) {
        int failed = -1;
        for (size_t k = 0; k < rects.size(); ++k) {
            if (rect_rooms[k].empty()) continue;  // winged away below
            if (!guillotine(fp, rect_rooms[k], 0, static_cast<int>(rect_rooms[k].size()),
                            rects[k][0], rects[k][1], rects[k][2], rects[k][3], ir, err)) {
                failed = static_cast<int>(k);
                break;
            }
        }
        if (failed < 0) break;
        if (!can_fix || guard >= static_cast<int>(fp.rooms.size())) {
            dump_failure("guard exhausted", failed);
            return false;
        }
        auto& pool = rect_rooms[static_cast<size_t>(failed)];
        // The hardest room of the failed piece moves out — prefer one without
        // a pinned seed cell: a pinned room (a hearth room's stack contact,
        // the auto-corridor's shaft side, §9.2/§6.4) must keep its pin.
        size_t move_q = 0;
        {
            bool have_plain = false;
            for (const int r : pool)
                if (fp.rooms[static_cast<size_t>(r)].pin_cell.x < 0) have_plain = true;
            for (size_t q = 1; q < pool.size(); ++q) {
                const PlanRoom& cand = fp.rooms[static_cast<size_t>(pool[q])];
                const PlanRoom& cur = fp.rooms[static_cast<size_t>(pool[move_q])];
                if (have_plain && cand.pin_cell.x >= 0) continue;
                if ((have_plain && cur.pin_cell.x >= 0) || cand.min_dim > cur.min_dim)
                    move_q = q;
            }
        }
        const int move_r = pool[move_q];
        const PlanRoom& room = fp.rooms[static_cast<size_t>(move_r)];
        const double rcx_c = (room.x0 + room.x1) / 2.0, rcz_c = (room.z0 + room.z1) / 2.0;
        // A pinned room can only go to the piece containing its pin.
        const int pin_piece =
            room.pin_cell.x >= 0
                ? static_cast<int>(rect_of(room.pin_cell.x + 0.5, room.pin_cell.y + 0.5))
                : -1;
        int best_t = -1;
        size_t best_load = 1 << 30;
        double best_dist = 1e30;
        for (size_t t = 0; t < rects.size(); ++t) {
            if (static_cast<int>(t) == failed) continue;
            if (pin_piece >= 0 && static_cast<int>(t) != pin_piece) continue;
            int set = 0;
            for (const int r : rect_rooms[t]) set |= 1 << r;
            const int ns = set | (1 << move_r);
            if (!rect_set_feasible(rcx, rects[t][0], rects[t][1], rects[t][2], rects[t][3],
                                   ns))
                continue;
            const double dx = (rects[t][0] + rects[t][2]) / 2.0 - rcx_c;
            const double dz = (rects[t][1] + rects[t][3]) / 2.0 - rcz_c;
            const double dist = dx * dx + dz * dz;
            if (rect_rooms[t].size() < best_load ||
                (rect_rooms[t].size() == best_load && dist < best_dist - kEps)) {
                best_t = static_cast<int>(t);
                best_load = rect_rooms[t].size();
                best_dist = dist;
            }
        }
        if (best_t < 0) {
            dump_failure("no host for the joint set", failed);
            return false;  // nobody can host the joint set
        }
        rect_rooms[static_cast<size_t>(best_t)].push_back(move_r);
        pool.erase(pool.begin() + static_cast<long>(move_q));
        if (pool.empty()) {
            pending[static_cast<size_t>(failed)] = 1;  // a fresh orphan: the
            ++orphans_left;                            // wing path absorbs it
        }
    }
    // Wing path (phase L), fixpoint over the final room rects: an orphan joins
    // an edge-adjacent room passing the covering test (two tiers: the host's
    // main rect keeps its min_dim core, the wing needs only a passage square).
    // A strip bordering several rooms is segmented along its long axis — one
    // wing per bordering room (a whole-strip wing would bury one room's area
    // budget); cells bordering no room (over the pinned shaft) join the
    // nearest segment. Adjacency through an already-attached wing counts (a
    // strip the well cut off may border only an earlier wing), so deferred
    // orphans retry after every attach.
    auto borders_at = [&](const std::array<int, 4>& p, bool horiz, int c, size_t ri) {
        const PlanRoom& room = fp.rooms[ri];
        auto touches = [&](const std::array<int, 4>& r) {
            if (horiz) {
                if (c < r[0] || c >= r[2]) return false;
                return r[3] == p[1] || r[1] == p[3];  // the room below/above
            }
            if (c < r[1] || c >= r[3]) return false;
            return r[2] == p[0] || r[0] == p[2];  // the room left/right
        };
        if (touches({room.x0, room.z0, room.x1, room.z1})) return true;
        for (const std::array<int, 4>& wng : room.wings)
            if (touches(wng)) return true;
        return false;
    };
    for (bool progress = true; orphans_left > 0 && progress;) {
        progress = false;
        for (size_t k = 0; k < rects.size() && orphans_left > 0; ++k) {
            if (!pending[k]) continue;
            const std::array<int, 4>& p = rects[k];
            const bool horiz = (p[2] - p[0]) >= (p[3] - p[1]);
            const int c0 = horiz ? p[0] : p[1], c1 = horiz ? p[2] : p[3];
            // The bordering room per column along the long axis (lowest room
            // index wins a column shared by two rooms).
            std::vector<int> owner(static_cast<size_t>(c1 - c0), -1);
            for (int c = c0; c < c1; ++c)
                for (size_t ri = 0; ri < fp.rooms.size(); ++ri) {
                    if (fp.rooms[ri].pinned) continue;
                    if (borders_at(p, horiz, c, ri)) {
                        owner[static_cast<size_t>(c - c0)] = static_cast<int>(ri);
                        break;
                    }
                }
            // Columns bordering no room join the nearest owned column (the
            // left/below one wins a tie).
            int last = -1;
            for (size_t q = 0; q < owner.size(); ++q) {
                if (owner[q] >= 0)
                    last = owner[q];
                else if (last >= 0)
                    owner[q] = last;
            }
            int nxt = -1;
            for (size_t q = owner.size(); q-- > 0;) {
                if (owner[q] >= 0)
                    nxt = owner[q];
                else if (nxt >= 0)
                    owner[q] = nxt;
            }
            // Group consecutive columns by owner into segments; every segment
            // must pass the covering test against its host.
            bool all_ok = true;
            std::vector<std::pair<int, std::array<int, 4>>> segs;
            for (int c = c0; c < c1 && all_ok;) {
                int d = c + 1;
                while (d < c1 && owner[static_cast<size_t>(d - c0)] ==
                                      owner[static_cast<size_t>(c - c0)])
                    ++d;
                const int ri = owner[static_cast<size_t>(c - c0)];
                if (ri < 0) {
                    all_ok = false;  // no bordering room at all — defer
                    break;
                }
                std::array<int, 4> seg =
                    horiz ? std::array<int, 4>{c, p[1], d, p[3]}
                          : std::array<int, 4>{p[0], c, p[2], d};
                const PlanRoom& room = fp.rooms[static_cast<size_t>(ri)];
                const int m =
                    std::max(1, static_cast<int>(std::ceil(room.min_dim / cell - kEps)));
                std::vector<std::array<int, 4>> parts;
                parts.push_back({room.x0, room.z0, room.x1, room.z1});
                for (const std::array<int, 4>& wng : room.wings) parts.push_back(wng);
                parts.push_back(seg);
                if (!rects_covering_ok(parts, m, kWingCoverCells)) {
                    all_ok = false;
                    break;
                }
                segs.push_back({ri, seg});
                c = d;
            }
            if (!all_ok) {
                // Fallback: the whole piece wings onto one edge-adjacent host
                // (a corridor is the host of last resort, then the largest
                // RELATIVE remaining deficit, then the room id).
                segs.clear();
                int best_r = -1;
                bool best_corr = true;
                double best_def = -1e30;
                for (size_t ri = 0; ri < fp.rooms.size(); ++ri) {
                    const PlanRoom& room = fp.rooms[ri];
                    if (room.pinned) continue;
                    bool adjacent =
                        rects_edge_adjacent(p, {room.x0, room.z0, room.x1, room.z1});
                    for (const std::array<int, 4>& wng : room.wings)
                        adjacent = adjacent || rects_edge_adjacent(p, wng);
                    if (!adjacent) continue;
                    const int m =
                        std::max(1, static_cast<int>(std::ceil(room.min_dim / cell - kEps)));
                    std::vector<std::array<int, 4>> parts;
                    parts.push_back({room.x0, room.z0, room.x1, room.z1});
                    for (const std::array<int, 4>& wng : room.wings) parts.push_back(wng);
                    parts.push_back(p);
                    if (!rects_covering_ok(parts, m, kWingCoverCells)) continue;
                    const bool is_corr = room.fn_code == kFnCorridor;
                    const double def =
                        (room.target - room.area_cells() * cell * cell) /
                        std::max(1.0, room.target);
                    if (best_r < 0 || is_corr < best_corr ||
                        (is_corr == best_corr && def > best_def + kEps) ||
                        (is_corr == best_corr && std::abs(def - best_def) <= kEps &&
                         room.id < fp.rooms[static_cast<size_t>(best_r)].id)) {
                        best_r = static_cast<int>(ri);
                        best_corr = is_corr;
                        best_def = def;
                    }
                }
                if (best_r < 0) continue;  // maybe reachable after another attach
                segs.push_back({best_r, p});
            }
            for (const auto& [ri, seg] : segs)
                fp.rooms[static_cast<size_t>(ri)].wings.push_back(seg);
            pending[k] = 0;
            --orphans_left;
            progress = true;
        }
    }
    if (orphans_left > 0) {
        err = "repair: more field rects than rooms (degenerate footprint)";
        if (std::getenv("BTG_DEBUG_DUMP")) {
            std::string dump = "repair dump: storey_index " +
                               std::to_string(fp.storey_index) + ", " +
                               std::to_string(rects.size()) + " rects, rooms:";
            for (const PlanRoom& room : fp.rooms)
                dump += " " + room.id + (room.pinned ? "(pinned)" : "");
            dump += "\nfield (. outside, # pinned, ? free, A- room):\n";
            std::vector<int32_t> dbg = fp.outside;
            for (size_t rr = 0; rr < fp.rooms.size(); ++rr) {
                const PlanRoom& room = fp.rooms[rr];
                for (int j = room.z0; j < room.z1; ++j)
                    for (int i = room.x0; i < room.x1; ++i)
                        dbg[static_cast<size_t>(j) * fp.w + i] =
                            room.pinned ? -99 : static_cast<int32_t>(rr);
            }
            for (int j = fp.h - 1; j >= 0; --j) {
                dump += "  ";
                for (int i = 0; i < fp.w; ++i) {
                    const int32_t c = dbg[static_cast<size_t>(j) * fp.w + i];
                    if (c == -99)
                        dump += '#';
                    else if (c == kCellFree)
                        dump += '?';
                    else if (c < 0)
                        dump += '.';
                    else
                        dump += static_cast<char>('A' + c % 26);
                }
                dump += "\n";
            }
            std::fprintf(stderr, "%s", dump.c_str());
        }
        return false;
    }
    // The joint fixpoint's failed guillotine probes leave their message in
    // err — a successful repair must not leak a stale error (the caller
    // treats any non-empty err as a failed attempt).
    err.clear();
    return true;
}

}  // namespace

bool field_reslice_feasible(const std::vector<int32_t>& mask,
                            const std::vector<int32_t>& margins_mask, int w, int h,
                            const IrParams& ir,
                            const std::vector<std::pair<double, double>>& rooms) {
    if (rooms.empty()) return true;
    std::vector<std::array<int, 4>> pieces = decompose_cells(mask, w, h);
    merge_rect_pairs(pieces);
    ResliceCtx cx;
    cx.margins = &margins_mask;
    cx.w = w;
    cx.h = h;
    cx.ir = ir;
    cx.rooms = rooms;
    std::sort(cx.rooms.begin(), cx.rooms.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (cx.rooms.size() > 24) return false;  // the bitmask is an int
    // Assign the rooms (the hardest first) to the pieces; a piece's growing
    // set must stay guillotine-feasible at every step (memoized).
    std::vector<int> piece_set(pieces.size(), 0);
    auto assign = [&](auto&& self, int ri) -> bool {
        if (ri == static_cast<int>(cx.rooms.size())) return true;
        for (size_t pi = 0; pi < pieces.size(); ++pi) {
            const int ns = piece_set[pi] | (1 << ri);
            if (ns == piece_set[pi]) continue;
            const auto& r = pieces[pi];
            if (!rect_set_feasible(cx, r[0], r[1], r[2], r[3], ns)) continue;
            piece_set[pi] = ns;
            if (self(self, ri + 1)) return true;
            piece_set[pi] ^= (1 << ri);
        }
        return false;
    };
    return assign(assign, 0);
}

bool rects_edge_adjacent(const std::array<int, 4>& a, const std::array<int, 4>& b) {
    if (a[2] == b[0] || b[2] == a[0]) return std::min(a[3], b[3]) > std::max(a[1], b[1]);
    if (a[3] == b[1] || b[3] == a[1]) return std::min(a[2], b[2]) > std::max(a[0], b[0]);
    return false;
}

bool rects_covering_ok(const std::vector<std::array<int, 4>>& parts, int m_main, int m_wing) {
    if (m_main <= 1 && m_wing <= 1) return true;
    int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
    for (const std::array<int, 4>& p : parts) {
        x0 = std::min(x0, p[0]);
        z0 = std::min(z0, p[1]);
        x1 = std::max(x1, p[2]);
        z1 = std::max(z1, p[3]);
    }
    const int w = x1 - x0, h = z1 - z0;
    std::vector<char> in(static_cast<size_t>(w) * h, 0), main_m(static_cast<size_t>(w) * h, 0);
    for (size_t p = 0; p < parts.size(); ++p)
        for (int j = parts[p][1]; j < parts[p][3]; ++j)
            for (int i = parts[p][0]; i < parts[p][2]; ++i) {
                in[static_cast<size_t>(j - z0) * w + (i - x0)] = 1;
                if (p == 0) main_m[static_cast<size_t>(j - z0) * w + (i - x0)] = 1;
            }
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            if (!in[static_cast<size_t>(j) * w + i]) continue;
            const int m =
                main_m[static_cast<size_t>(j) * w + i] ? m_main : std::min(m_main, m_wing);
            if (m <= 1) continue;
            bool covered = false;
            for (int sj = j - m + 1; sj <= j && !covered; ++sj)
                for (int si = i - m + 1; si <= i && !covered; ++si) {
                    bool ok = true;
                    for (int qj = sj; qj < sj + m && ok; ++qj)
                        for (int qi = si; qi < si + m && ok; ++qi)
                            if (qi < 0 || qj < 0 || qi >= w || qj >= h ||
                                !in[static_cast<size_t>(qj) * w + qi])
                                ok = false;
                    covered = ok;
                }
            if (!covered) return false;
        }
    return true;
}

std::vector<std::array<int, 4>> decompose_cells(const std::vector<int32_t>& mask, int w,
                                                int h) {
    return decompose_field(mask, w, h);
}

bool field_connected_without(const std::vector<int32_t>& mask, int w, int h,
                             const std::vector<int>& blocked_list) {
    std::vector<char> blocked(static_cast<size_t>(w) * h, 0);
    for (int c : blocked_list) blocked[static_cast<size_t>(c)] = 1;
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

void field_piece_net_rect(const std::vector<int32_t>& occ,
                          const std::vector<int32_t>& margins_mask, int w, int h,
                          const IrParams& ir, std::vector<double>& piece_net_rect) {
    piece_net_rect.assign(static_cast<size_t>(w) * h, 0.0);
    std::vector<std::array<int, 4>> rects = decompose_field(occ, w, h);
    merge_rect_pairs(rects);
    for (const std::array<int, 4>& r : rects) {
        double t_x0, t_x1, t_z0, t_z1;
        field_side_margins(margins_mask, w, h, ir, r[0], r[1], r[2], r[3], t_x0, t_x1,
                           t_z0, t_z1);
        const double net_min = std::min((r[2] - r[0]) * ir.cell - t_x0 - t_x1,
                                        (r[3] - r[1]) * ir.cell - t_z0 - t_z1);
        for (int j = r[1]; j < r[3]; ++j)
            for (int i = r[0]; i < r[2]; ++i)
                piece_net_rect[static_cast<size_t>(j) * w + i] = net_min;
    }
}

void field_piece_ids(const std::vector<int32_t>& occ,
                     const std::vector<int32_t>& margins_mask, int w, int h,
                     const IrParams& ir, std::vector<int>& piece_id,
                     std::vector<double>& piece_net_x,
                     std::vector<double>& piece_net_z,
                     std::vector<std::array<int, 4>>& piece_rects) {
    piece_id.assign(static_cast<size_t>(w) * h, -1);
    piece_net_x.clear();
    piece_net_z.clear();
    piece_rects.clear();
    std::vector<std::array<int, 4>> rects = decompose_field(occ, w, h);
    merge_rect_pairs(rects);
    for (size_t pi = 0; pi < rects.size(); ++pi) {
        const std::array<int, 4>& r = rects[pi];
        double t_x0, t_x1, t_z0, t_z1;
        field_side_margins(margins_mask, w, h, ir, r[0], r[1], r[2], r[3], t_x0, t_x1,
                           t_z0, t_z1);
        piece_net_x.push_back((r[2] - r[0]) * ir.cell - t_x0 - t_x1);
        piece_net_z.push_back((r[3] - r[1]) * ir.cell - t_z0 - t_z1);
        piece_rects.push_back(r);
        for (int j = r[1]; j < r[3]; ++j)
            for (int i = r[0]; i < r[2]; ++i)
                piece_id[static_cast<size_t>(j) * w + i] = static_cast<int>(pi);
    }
}

void merge_rect_pairs(std::vector<std::array<int, 4>>& rects) {
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t a = 0; a < rects.size() && !merged; ++a)
            for (size_t b = a + 1; b < rects.size() && !merged; ++b) {
                const std::array<int, 4>& A = rects[a];
                const std::array<int, 4>& B = rects[b];
                if (A[0] == B[0] && A[2] == B[2] && (A[3] == B[1] || B[3] == A[1])) {
                    rects[a] = {A[0], std::min(A[1], B[1]), A[2], std::max(A[3], B[3])};
                    rects.erase(rects.begin() + static_cast<long>(b));
                    merged = true;
                } else if (A[1] == B[1] && A[3] == B[3] && (A[2] == B[0] || B[2] == A[0])) {
                    rects[a] = {std::min(A[0], B[0]), A[1], std::max(A[2], B[2]), A[3]};
                    rects.erase(rects.begin() + static_cast<long>(b));
                    merged = true;
                }
            }
    }
}

int tiled_piece_count(const std::vector<int32_t>& mask, int w, int h) {
    return tiled_piece_stats(mask, w, h).first;
}

std::pair<int, int> tiled_piece_stats(const std::vector<int32_t>& mask, int w, int h) {
    std::vector<std::array<int, 4>> rects = decompose_field(mask, w, h);
    merge_rect_pairs(rects);
    // A piece thinner than 3 cells cannot host even the narrowest function
    // (a corridor needs 1.0 m CLEAR: 2 cells gross leave 1.0 - t_partition
    // = 0.88 m) — it can only ever become a wing, so score it as a strip.
    int strips = 0;
    for (const std::array<int, 4>& r : rects)
        if (std::min(r[2] - r[0], r[3] - r[1]) < 3) ++strips;
    return {static_cast<int>(rects.size()), strips};
}

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
    GrowInput in;
    if (!storey_field(centered, params.ir, in.w, in.h, in.origin, in.outside, err)) return fp;
    in.storey_index = 0;
    return grow_floor_cells(in, prog, params, rng, err);
}

FloorPlan grow_floor_cells(const GrowInput& in, const ExpandedProgram& prog,
                           const PlanParams& params, Rng& rng, std::string& err) {
    err.clear();
    FloorPlan fp;
    fp.w = in.w;
    fp.h = in.h;
    fp.origin = in.origin;
    fp.storey_index = in.storey_index;
    fp.outside = in.outside;
    fp.useful = in.useful;
    // Growth works on its own grid copy; fp.cells is rebuilt by rasterize().
    std::vector<int32_t> occ = fp.outside;

    // Pinned rooms (the stair shaft) are placed verbatim before everything.
    // door_ok arrives precomputed from the shaft layout (pad + notch only).
    for (PlanRoom room : in.pinned) {
        room.pinned = true;
        const int ri = static_cast<int>(fp.rooms.size());
        for (int j = room.z0; j < room.z1; ++j)
            for (int i = room.x0; i < room.x1; ++i) {
                if (!fp.inside(i, j) || occ[static_cast<size_t>(j) * fp.w + i] != kCellFree) {
                    err = "pinned room " + room.id + " overlaps a non-free cell";
                    return fp;
                }
                occ[static_cast<size_t>(j) * fp.w + i] = static_cast<int32_t>(ri);
            }
        fp.rooms.push_back(std::move(room));
    }

    // Rooms: required instances + the optional instances the field estimate
    // admitted (algorithms §4: greedy while the circulation-adjusted spare
    // lasts).
    double free_area = 0.0;
    for (int32_t c : fp.outside)
        if (c == kCellFree) free_area += params.ir.cell * params.ir.cell;
    for (const PlanRoom& room : fp.rooms)
        free_area -= room.area_cells() * params.ir.cell * params.ir.cell;
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

    // Wing-only cells (I10): a cell can belong to a legal main rect only when
    // some all-free SQUARE whose net side clears the room's min_dim covers it
    // (any feasible rect contains such a square on its short side, and any
    // such square is itself a feasible rect, so the test is exact). Seed cells
    // failing it sit in strips that exist only as future wings (the column
    // above a shallow L notch: 2 gross cells net 0.97 m, short of the
    // corridor's 1.0 m); a room seeded there is trapped — it grows along the
    // strip, fails the net min_dim gate, and the repair cannot re-home it.
    // Maximal-rect pieces under-report here: a room rect may span several
    // pieces (the attic knee bands around a stair well), so the check scans
    // squares directly. The relax pass below keeps the honest "no free cell"
    // error when nothing hostable is left.
    std::vector<double> piece_net;
    field_piece_net(occ, fp.outside, fp.w, fp.h, params.ir, piece_net);
    // Rect-piece net for the seed_next_to rooms (B4): their seed constraint is
    // never relaxed, so it must predict the repair's piece binding — a room
    // seeded in a piece whose net min side is under its min_dim gets re-homed
    // by the repair and loses its stack/shaft contact (the square scan above
    // is too lenient on slivers: a square may extend into a neighbor piece).
    // Computed on the repair's model: the field with the pinned rooms blocked.
    std::vector<double> piece_net_rect;
    if (!in.seed_next_to.empty()) {
        std::vector<int32_t> decomp = fp.outside;
        for (const PlanRoom& room : fp.rooms)
            if (room.pinned)
                for (int j = room.z0; j < room.z1; ++j)
                    for (int i = room.x0; i < room.x1; ++i)
                        decomp[static_cast<size_t>(j) * fp.w + i] = kCellOutside;
        field_piece_net_rect(decomp, fp.outside, fp.w, fp.h, params.ir, piece_net_rect);
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
    // Order (§6.1): stack-bound rooms first (their stack-neighbor cells are
    // the scarcest resource, B4 §9.2) — among them the largest min_dim first,
    // mirroring the G4b gate's greedy match: a bigger room seeded late can
    // find every remaining seed cell too thin for it (the gate matched it to
    // a cell the smaller room then took). Then the entrance room, then by
    // descending target area. Pinned rooms are already placed; they only feed
    // the crowding term.
    std::vector<int> order;
    for (size_t i = 0; i < fp.rooms.size(); ++i)
        if (!fp.rooms[i].pinned) order.push_back(static_cast<int>(i));
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const PlanRoom& A = fp.rooms[static_cast<size_t>(a)];
        const PlanRoom& B = fp.rooms[static_cast<size_t>(b)];
        const bool sa = in.seed_next_to.count(A.id) != 0;
        const bool sb = in.seed_next_to.count(B.id) != 0;
        if (sa != sb) return sa;
        if (sa && A.min_dim != B.min_dim) return A.min_dim > B.min_dim;
        if (A.entrance_front != B.entrance_front) return A.entrance_front;
        return A.target > B.target;
    });

    // The attractor: the facade center on the ground storey, the stair
    // arrival on the upper ones (the circulation root, §7.1).
    const glm::dvec2 ent =
        in.entrance_cell.x >= 0.0 ? in.entrance_cell : entrance_point(fp);
    // Rooted at the street facade? The upper storeys root at the stair
    // arrival instead; there the facade-row seed restriction does not apply.
    const bool facade_root = in.entrance_cell.x < 0.0;

    const SeedField field_w;
    std::vector<const PlanRoom*> placed;
    // Room id -> (seed cell, pull) of already-seeded linked rooms. Links are
    // instance-resolved by G1, so ids match exactly. Pinned rooms (the stair
    // shaft) count as seeds so a linked room (the auto-corridor) is pulled
    // toward the shaft.
    std::vector<std::pair<std::string, std::pair<int, int>>> seeds;
    for (const PlanRoom& room : fp.rooms)
        if (room.pinned) {
            placed.push_back(&room);
            seeds.emplace_back(room.id,
                               std::pair{(room.x0 + room.x1) / 2, (room.z0 + room.z1) / 2});
        }
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
        // Seed feasibility (I10): the cell must be coverable by an m x m free
        // square, m from the room's min_dim — a niche that cannot hold the
        // square (a 2-row band above the stair shaft, a knee sliver) never
        // grows into a legal room.
        const int m_room =
            std::max(1, static_cast<int>(std::ceil(room.min_dim / params.ir.cell - kEps)));
        auto seedable = [&](int ci, int cj) {
            if (m_room <= 1) return true;
            for (int sj = cj - m_room + 1; sj <= cj; ++sj)
                for (int si = ci - m_room + 1; si <= ci; ++si) {
                    bool ok = true;
                    for (int qj = sj; qj < sj + m_room && ok; ++qj)
                        for (int qi = si; qi < si + m_room && ok; ++qi)
                            if (!fp.inside(qi, qj) ||
                                occ[static_cast<size_t>(qj) * fp.w + qi] != kCellFree)
                                ok = false;
                    if (ok) return true;
                }
            return false;
        };
        // Entrance rooms seed on their facade row first (front = high j,
        // back = low j) when the storey is rooted at the street facade; a
        // storey rooted at the stair arrival has no facade row constraint.
        // The constraint softens to a plain preference when the row is fully
        // outside (L footprints). Wing-only cells (the piece's NET smaller
        // side under the room's min_dim) are skipped on the first pass — a
        // seed there is trapped in a strip the anneal cannot leave; the relax
        // pass keeps the honest failure when nothing hostable is left.
        // B4: a hearth room is additionally pinned to its stack's neighbor
        // cells (seed_next_to) in every pass — never relaxed.
        const auto seed_only_it = in.seed_next_to.find(room.id);
        int best_i = -1, best_j = -1;
        double best = -1e30;
        for (int relax = 0; relax < 2 && best_i < 0; ++relax)
            for (int pass = 0; pass < 2 && best_i < 0; ++pass)
                for (int j = 0; j < fp.h; ++j)
                    for (int i = 0; i < fp.w; ++i) {
                        if (occ[static_cast<size_t>(j) * fp.w + i] != kCellFree) continue;
                        if (seed_only_it != in.seed_next_to.end()) {
                            bool listed = false;
                            for (const glm::ivec2& c : seed_only_it->second)
                                if (c.x == i && c.y == j) {
                                    listed = true;
                                    break;
                                }
                            if (!listed) continue;
                            // The piece must host the room on its own (the
                            // repair binds one room to one piece); never
                            // relaxed — same gate as the G4b chimney scan.
                            if (!piece_net_rect.empty() &&
                                piece_net_rect[static_cast<size_t>(j) * fp.w + i] <
                                    room.min_dim - kEps)
                                continue;
                        }
                        if (!seedable(i, j)) continue;
                        if (!relax && piece_net[static_cast<size_t>(j) * fp.w + i] <
                                          room.min_dim - kEps)
                            continue;
                        if (pass == 0 && facade_root) {
                            if (room.entrance_front && j != fp.h - 1) continue;
                            if (room.entrance_back && j != 0) continue;
                        }
                        double sc =
                            seed_desirability(room, i, j, fp, placed, linked, field_w,
                                              room.entrance_front, ent);
                        sc += rng.f64() * 0.1;  // small tie noise (§6.1)
                        if (sc > best) {
                            best = sc;
                            best_i = i;
                            best_j = j;
                        }
                    }
        if (best_i < 0) {
            if (std::getenv("BTG_DEBUG_DUMP")) {
                if (seed_only_it != in.seed_next_to.end()) {
                    std::string t;
                    for (const glm::ivec2& c : seed_only_it->second) {
                        const size_t q = static_cast<size_t>(c.y) * fp.w + c.x;
                        t += " (" + std::to_string(c.x) + "," + std::to_string(c.y) + ")";
                        if (occ[q] != kCellFree) {
                            t += " occ";
                            continue;
                        }
                        if (!seedable(c.x, c.y)) t += " !seedable";
                        if (!piece_net_rect.empty() &&
                            piece_net_rect[q] < room.min_dim - kEps)
                            t += " net=" + std::to_string(piece_net_rect[q]);
                    }
                    std::fprintf(stderr, "seed fail %s: seed_set:%s\n", room.id.c_str(),
                                 t.c_str());
                }
                int n_free = 0, n_seedable = 0, n_net = 0;
                double net_max = 0.0;
                for (int j = 0; j < fp.h; ++j)
                    for (int i = 0; i < fp.w; ++i) {
                        const size_t c = static_cast<size_t>(j) * fp.w + i;
                        if (occ[c] != kCellFree) continue;
                        ++n_free;
                        net_max = std::max(net_max, piece_net[c]);
                        if (!seedable(i, j)) continue;
                        ++n_seedable;
                        if (piece_net[c] >= room.min_dim - kEps) ++n_net;
                    }
                std::fprintf(stderr,
                             "seed fail %s (min_dim %.2f): free %d, seedable %d, "
                             "net-ok %d, net_max %.3f\n",
                             room.id.c_str(), room.min_dim, n_free, n_seedable, n_net,
                             net_max);
                std::string d = "occ (. blocked, ? free):\n";
                for (int j = fp.h - 1; j >= 0; --j) {
                    d += "  ";
                    for (int i = 0; i < fp.w; ++i)
                        d += occ[static_cast<size_t>(j) * fp.w + i] == kCellFree ? '?' : '.';
                    d += "\n";
                }
                std::fprintf(stderr, "%s", d.c_str());
            }
            err = seed_only_it != in.seed_next_to.end()
                      ? "no free cell for room " + room.id +
                            " next to its chimney stack (the G4b seed set is occupied)"
                      : "no free cell for room " + room.id;
            return fp;
        }
        if (std::getenv("BTG_DEBUG_DUMP"))
            std::fprintf(stderr, "seed %s at (%d, %d) m=%d\n", room.id.c_str(), best_i,
                         best_j, m_room);
        room.x0 = best_i;
        room.z0 = best_j;
        room.x1 = best_i + 1;
        room.z1 = best_j + 1;
        if (seed_only_it != in.seed_next_to.end()) room.pin_cell = {best_i, best_j};
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
    // "пропорции в пределах"; the form term polices the rest). A room too thin
    // for its min_dim must be allowed to thicken first — otherwise the guard
    // traps a strip forever (1xN -> 2xN violates the ratio). The threshold
    // counts the partition allowance: 2 gross cells net 1.0 - 0.12 = 0.88 m,
    // short of the corridor's 1.0 m, so the bypass runs until NET-feasible.
    constexpr double kGrowRatio = 2.0;
    auto ratio_ok = [&](const PlanRoom& r, int side) {
        int rw = r.x1 - r.x0, rh = r.z1 - r.z0;
        const int m = std::max(
            1, static_cast<int>(
                   std::ceil((r.min_dim + params.ir.t_partition) / params.ir.cell - kEps)));
        if (std::min(rw, rh) < m) {
            const bool widens_thin =
                (rw < rh) ? (side == 0 || side == 1) : (side == 2 || side == 3);
            if (widens_thin) return true;
        }
        if (side == 0 || side == 1) ++rw;
        else ++rh;
        const double ratio = static_cast<double>(std::max(rw, rh)) / std::min(rw, rh);
        return ratio <= kGrowRatio + kEps;
    };

    std::vector<bool> active(fp.rooms.size(), true);
    for (size_t r = 0; r < fp.rooms.size(); ++r)
        if (fp.rooms[r].pinned) active[r] = false;
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
                    if (ni < 0 || fp.rooms[static_cast<size_t>(ni)].pinned) continue;
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

    // -- Leftover free cells: absorb them as WINGS of edge-adjacent rooms ----
    // The FILL above is rectangle-bound (a whole bbox band must be free), so
    // on masked fields (the attic walk zone) free rectangles survive. A global
    // re-slice would shred the grown layout into bands too narrow for the
    // rooms (the guillotine min_dim base case), while a wing keeps the host's
    // rect and passes the same I10 covering gate the repair applies. Rolls
    // back when some piece has no host — the re-slice then decides on the
    // pre-absorption layout.
    auto absorb_free_wings = [&]() {
        std::vector<int32_t> fmask(static_cast<size_t>(fp.w) * fp.h, kCellOutside);
        bool any_free = false;
        for (size_t c = 0; c < occ.size(); ++c)
            if (occ[c] == kCellFree) {
                fmask[c] = kCellFree;
                any_free = true;
            }
        if (!any_free) return true;
        std::vector<std::array<int, 4>> pieces = decompose_field(fmask, fp.w, fp.h);
        merge_rect_pairs(pieces);
        const double cell2 = params.ir.cell * params.ir.cell;
        std::vector<char> pending(pieces.size(), 1);
        std::vector<std::pair<int, int>> absorbed;  // (room, piece) for rollback
        size_t left = pieces.size();
        for (bool progress = true; left > 0 && progress;) {
            progress = false;
            for (size_t p = 0; p < pieces.size() && left > 0; ++p) {
                if (!pending[p]) continue;
                int best_r = -1;
                bool best_corr = true;
                double best_def = -1e30;
                for (size_t r = 0; r < fp.rooms.size(); ++r) {
                    PlanRoom& room = fp.rooms[r];
                    if (room.pinned) continue;  // the shaft's shape is the stair's
                    bool adjacent = rects_edge_adjacent(
                        pieces[p], {room.x0, room.z0, room.x1, room.z1});
                    for (const std::array<int, 4>& wng : room.wings)
                        adjacent = adjacent || rects_edge_adjacent(pieces[p], wng);
                    if (!adjacent) continue;
                    const int m =
                        std::max(1, static_cast<int>(std::ceil(room.min_dim /
                                                               params.ir.cell - kEps)));
                    std::vector<std::array<int, 4>> parts;
                    parts.push_back({room.x0, room.z0, room.x1, room.z1});
                    for (const std::array<int, 4>& wng : room.wings) parts.push_back(wng);
                    parts.push_back(pieces[p]);
                    if (!rects_covering_ok(parts, m, kWingCoverCells)) continue;
                    // A corridor is the host of last resort (a fat corridor
                    // boxes its neighbors in — phase L); then the largest
                    // RELATIVE remaining deficit wins, then the room id.
                    const bool is_corr = room.fn_code == kFnCorridor;
                    const double def =
                        (room.target - room.area_cells() * cell2) /
                        std::max(1.0, room.target);
                    if (best_r < 0 || is_corr < best_corr ||
                        (is_corr == best_corr && def > best_def + kEps) ||
                        (is_corr == best_corr && std::abs(def - best_def) <= kEps &&
                         room.id < fp.rooms[static_cast<size_t>(best_r)].id)) {
                        best_r = static_cast<int>(r);
                        best_corr = is_corr;
                        best_def = def;
                    }
                }
                if (best_r < 0) continue;  // maybe reachable after another attach
                fp.rooms[static_cast<size_t>(best_r)].wings.push_back(pieces[p]);
                absorbed.emplace_back(best_r, static_cast<int>(p));
                pending[p] = 0;
                --left;
                progress = true;
            }
        }
        if (left > 0) {
            for (const auto& [r, p] : absorbed) {
                PlanRoom& room = fp.rooms[static_cast<size_t>(r)];
                room.wings.erase(std::find(room.wings.begin(), room.wings.end(),
                                           pieces[static_cast<size_t>(p)]));
            }
            return false;
        }
        if (std::getenv("BTG_DEBUG_DUMP"))
            for (const auto& [r, p] : absorbed) {
                const std::array<int, 4>& piece = pieces[static_cast<size_t>(p)];
                std::fprintf(stderr,
                             "absorb: piece [%d,%d - %d,%d] -> wing of %s\n", piece[0],
                             piece[1], piece[2], piece[3],
                             fp.rooms[static_cast<size_t>(r)].id.c_str());
            }
        for (const auto& [r, p] : absorbed) {
            const std::array<int, 4>& piece = pieces[static_cast<size_t>(p)];
            for (int j = piece[1]; j < piece[3]; ++j)
                for (int i = piece[0]; i < piece[2]; ++i)
                    occ[static_cast<size_t>(j) * fp.w + i] = static_cast<int32_t>(r);
        }
        return true;
    };

    // -- Repair gate ----------------------------------------------------------
    // A room whose NET dims fall under its min_dim cannot ship (I10): the
    // anneal MIGHT thicken it (a shift unit pulls the boundary while a
    // neighbor has slack), but next to a fixed room (the pinned shaft, a
    // winged L-room) or inside a narrow band no legal shift exists — so the
    // growth re-slices. The re-slice is net-aware end to end (the guillotine
    // charges the wall allowances), so a repair actually clears the gate.
    auto strips = [&] {
        fp.rasterize();  // room_dims_net reads fp.cells; it agrees with occ
        for (size_t r = 0; r < fp.rooms.size(); ++r) {
            const PlanRoom& room = fp.rooms[r];
            if (room.fixed()) continue;  // winged rooms pass via covering (cost)
            double w_m, d_m;
            fp.room_dims_net(static_cast<int>(r), params.ir, w_m, d_m);
            if (std::min(w_m, d_m) < room.min_dim - 1e-6) return true;
        }
        return false;
    };
    const bool complete = absorb_free_wings();
    if ((!complete || strips()) && !repair_reslice(fp, params.ir, err)) {
        if (std::getenv("BTG_DEBUG_DUMP")) {
            std::string dump = "grow dump: storey_index " + std::to_string(fp.storey_index) +
                               ", repair failed: " + err +
                               "\nocc (. masked, ? free, A- room):\n";
            for (int j = fp.h - 1; j >= 0; --j) {
                dump += "  ";
                for (int i = 0; i < fp.w; ++i) {
                    const int32_t c = occ[static_cast<size_t>(j) * fp.w + i];
                    if (c == kCellFree)
                        dump += '?';
                    else if (c < 0)
                        dump += '.';
                    else
                        dump += static_cast<char>('A' + c % 26);
                }
                dump += "\n";
            }
            for (size_t r = 0; r < fp.rooms.size(); ++r) {
                const PlanRoom& room = fp.rooms[r];
                dump += "  " + room.id + " [" + std::to_string(room.x0) + "," +
                        std::to_string(room.z0) + " - " + std::to_string(room.x1) + "," +
                        std::to_string(room.z1) + "] target " + std::to_string(room.target) +
                        (room.pinned ? " pinned" : "") + "\n";
            }
            std::fprintf(stderr, "%s", dump.c_str());
        }
        return fp;
    }

    // Rebuild the final grid from the outside mask + room rects.
    fp.rasterize();
    if (std::string terr; !fp.check_rectangle_tiling(terr)) {
        err = "growth produced an invalid tiling: " + terr;
        return fp;
    }
    if (strips()) {
        err = "growth left a room thinner than its min_dim";
        if (std::getenv("BTG_DEBUG_DUMP")) {
            for (size_t r = 0; r < fp.rooms.size(); ++r) {
                const PlanRoom& room = fp.rooms[r];
                if (room.fixed()) continue;
                double w_m, d_m;
                fp.room_dims_net(static_cast<int>(r), params.ir, w_m, d_m);
                if (std::min(w_m, d_m) < room.min_dim - 1e-6)
                    std::fprintf(stderr,
                                 "thin room %s: net %.2f x %.2f m (min_dim %.2f), "
                                 "rect [%d,%d - %d,%d]\n",
                                 room.id.c_str(), w_m, d_m, room.min_dim, room.x0, room.z0,
                                 room.x1, room.z1);
            }
        }
        return fp;
    }
    return fp;
}

}  // namespace building_topology_generator
