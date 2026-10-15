#include "pch.h"

#include "massing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "codes.h"
#include "roof.h"

namespace building_topology_generator {

namespace {

constexpr double kEps = 1e-9;

std::string fmt(double v, int prec = 1) {
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(prec);
    s << v;
    return s.str();
}

// Axes-space rect outline (the frozen-house order: (min,min) -> (min,max) ->
// (max,max) -> (max,min)), expanded by half the exterior wall thickness (§5.2).
std::vector<glm::dvec2> rect_outline(double x0, double z0, double x1, double z1, double t_ext) {
    const double t = t_ext / 2;
    return {{x0 - t, z0 - t}, {x0 - t, z1 + t}, {x1 + t, z1 + t}, {x1 + t, z0 - t}};
}

// L footprint: the rect with a corner of a x b meters removed.
// corner: 0 (min,min), 1 (min,max), 2 (max,max), 3 (max,min); same winding as
// rect_outline.
std::vector<glm::dvec2> l_outline(double x0, double z0, double x1, double z1, double t_ext,
                                  int corner, double a, double b) {
    const double t = t_ext / 2;
    x0 -= t;
    z0 -= t;
    x1 += t;
    z1 += t;
    switch (corner) {
        case 0:
            return {{x0, z0 + b}, {x0, z1}, {x1, z1}, {x1, z0}, {x0 + a, z0}, {x0 + a, z0 + b}};
        case 1:
            return {{x0, z0}, {x0, z1 - b}, {x0 + a, z1 - b}, {x0 + a, z1}, {x1, z1}, {x1, z0}};
        case 2:
            return {{x0, z0}, {x0, z1}, {x1 - a, z1}, {x1 - a, z1 - b}, {x1, z1 - b}, {x1, z0}};
        default:
            return {{x0, z0}, {x0, z1}, {x1, z1}, {x1, z0 + b}, {x1 - a, z0 + b}, {x1 - a, z0}};
    }
}

struct BBox {
    double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
};

BBox bbox_of(const std::vector<glm::dvec2>& p) {
    BBox b;
    b.x0 = b.z0 = 1e30;
    b.x1 = b.z1 = -1e30;
    for (const glm::dvec2& v : p) {
        b.x0 = std::min(b.x0, v.x);
        b.z0 = std::min(b.z0, v.y);
        b.x1 = std::max(b.x1, v.x);
        b.z1 = std::max(b.z1, v.y);
    }
    return b;
}

BBox bbox_union(BBox a, const BBox& b) {
    a.x0 = std::min(a.x0, b.x0);
    a.z0 = std::min(a.z0, b.z0);
    a.x1 = std::max(a.x1, b.x1);
    a.z1 = std::max(a.z1, b.z1);
    return a;
}

double polygon_area(const std::vector<glm::dvec2>& p) {
    double a = 0.0;
    for (size_t i = 0; i < p.size(); ++i) {
        const glm::dvec2& u = p[i];
        const glm::dvec2& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return std::abs(a) / 2;
}

// §5.6 / algorithms §2: distance from the outline inward where the
// under-ceiling height reaches h_walk.
double walk_inset(double pitch_deg, double eave_rise, const IrParams& p) {
    const double pitch = glm::radians(pitch_deg);
    const double need = (p.h_walk() - eave_rise + p.roof_t / std::cos(pitch)) / std::tan(pitch);
    return std::max(0.0, need);
}

// Analytic useful attic area of a rect footprint (algorithms §3). j0: first
// useful cell row from each sloped edge (worst-corner rule, §5.6).
double rect_attic_useful(int w_cells, int d_cells, int roof_kind, double pitch_deg,
                         double eave_rise, const MassingParams& params) {
    const double s = params.ir.cell;
    const double t_walk = walk_inset(pitch_deg, eave_rise, params.ir);
    const int j0 = static_cast<int>(std::ceil((t_walk - params.ir.t_exterior / 2) / s - kEps));
    long w = w_cells, d = d_cells;
    // Gable: slopes on the front/back (z) edges; hip/shed: every/free edges.
    d -= 2L * j0;
    if (roof_kind == kRoofHip) w -= 2L * j0;
    if (w <= 0 || d <= 0) return 0.0;
    return static_cast<double>(w) * static_cast<double>(d) * s * s;
}

// Useful attic area of any footprint via the skeleton: a cell is walkable
// when every corner clears h_walk (the under-ceiling field is linear inside a
// skeleton face, so the corners bound it; §5.6). Corners outside the outline
// (the L cut) fail ceiling_y_at, so no explicit cut mask is needed.
double attic_useful(const MassingVolume& vol, int w_cells, int d_cells,
                    const MassingParams& params) {
    IrVolume iv;
    iv.id = "attic";
    iv.outline = vol.outline;
    iv.roof.kind_code = vol.roof_kind;
    iv.roof.pitch_by_edge = vol.pitch_by_edge;
    iv.roof.overhang = params.overhang;
    iv.roof.eave_rise = vol.eave_rise;
    // Only deltas above the attic floor matter: put the attic floor at 0.
    iv.roof.eave_y = vol.eave_rise;
    iv.roof.roof_t = params.ir.roof_t;
    RoofModel model;
    std::string err;
    if (!build_roof_model(iv, model, err)) return 0.0;
    const double s = params.ir.cell;
    // The grid is queried in the outline frame: the inner field starts half an
    // exterior wall inside the bbox.
    const BBox mb = bbox_of(vol.outline);
    const double ox = mb.x0 + params.ir.t_exterior / 2;
    const double oz = mb.z0 + params.ir.t_exterior / 2;
    int useful = 0;
    for (int i = 0; i < w_cells; ++i)
        for (int j = 0; j < d_cells; ++j) {
            bool walk = true;
            for (int ci = 0; ci <= 1 && walk; ++ci)
                for (int cj = 0; cj <= 1 && walk; ++cj) {
                    double cy = 0.0;
                    if (!ceiling_y_at(model, ox + (i + ci) * s, oz + (j + cj) * s, cy) ||
                        cy < params.ir.h_walk())
                        walk = false;
                }
            if (walk) ++useful;
        }
    return useful * s * s;
}

// Per-storey net areas (algorithms §3 step 1): gross minus the exterior wall
// band, minus the partitions estimate, minus cores (stair, chimney channels);
// the attic by the useful mask.
std::vector<double> storey_areas(int w_cells, int d_cells, int cut_cells, int n_full, bool attic,
                                 int roof_kind, double pitch, double eave_rise, double annex_net,
                                 int hearth_channels, const MassingParams& params) {
    const double s = params.ir.cell;
    const double gross = (static_cast<double>(w_cells) * d_cells - cut_cells) * s * s;
    const double perim = 2.0 * (w_cells + d_cells) * s;  // a corner cut keeps the perimeter
    const double shell = perim * params.ir.t_exterior / 2;
    std::vector<double> net;
    const bool has_stair = n_full > 1 || attic;
    for (int k = 0; k < n_full; ++k) {
        double a = gross - shell - params.kappa_partitions * gross;
        if (has_stair) a -= params.stair_area;
        if (k == 0) {
            a -= params.chimney_area * hearth_channels;
            a += annex_net;
        }
        net.push_back(std::max(0.0, a));
    }
    if (attic) {
        double a = rect_attic_useful(w_cells, d_cells, roof_kind, pitch, eave_rise, params) *
                   (1.0 - params.kappa_partitions);
        if (has_stair) a -= params.stair_area * 0.5;
        net.push_back(std::max(0.0, a));
    }
    return net;
}

// Greedy storey assignment (algorithms §3 step 2: the fast version of §4).
struct Assignment {
    std::vector<double> used;
    int optionals_in = 0;
    bool all_required = false;
};

Assignment assign_rooms(const ExpandedProgram& prog, const std::vector<double>& net, int n_full,
                        bool attic, const MassingParams& params) {
    const int n_storeys = static_cast<int>(net.size());
    Assignment a;
    a.used.assign(n_storeys, 0.0);

    auto allowed = [&](const RoomInstance& r) {
        std::vector<int> out;
        for (int k = 0; k < n_storeys; ++k) {
            const bool is_attic = attic && k == n_full;
            const int code = is_attic ? kStoreyAttic : (k == 0 ? kStoreyGround : kStoreyUpper);
            if (r.storeys_mask & (1u << code)) out.push_back(k);
        }
        return out;
    };

    std::vector<const RoomInstance*> required, optional;
    for (const RoomInstance& r : prog.rooms) (r.optional ? optional : required).push_back(&r);
    std::sort(required.begin(), required.end(), [&](const RoomInstance* x, const RoomInstance* y) {
        const size_t nx = allowed(*x).size(), ny = allowed(*y).size();
        if (nx != ny) return nx < ny;
        if (x->target != y->target) return x->target > y->target;
        return x->id < y->id;
    });

    for (const RoomInstance* r : required) {
        const std::vector<int> cand = allowed(*r);
        int best = -1;
        double best_spare = 0.0;
        for (int k : cand) {
            const double spare = net[k] - a.used[k] - r->area_min;
            if (spare < -kEps) continue;
            // Zone preference on ties: private up, public/service down (§7.3).
            if (best < 0 || spare > best_spare + kEps ||
                (std::abs(spare - best_spare) <= kEps &&
                 (r->zone_code == kZonePrivate ? k > best : k < best))) {
                best = k;
                best_spare = spare;
            }
        }
        if (best < 0) return a;  // a required room fits nowhere
        a.used[best] += r->target;
    }
    a.all_required = true;

    // Optionals while a storey keeps the circulation-adjusted spare (§7.3).
    for (const RoomInstance* r : optional) {
        const std::vector<int> cand = allowed(*r);
        int best = -1;
        double best_spare = 0.0;
        for (int k : cand) {
            const double spare = (net[k] - a.used[k]) / (1.0 + params.comms_share) - r->target;
            if (spare < -kEps) continue;
            if (best < 0 || spare > best_spare + kEps ||
                (std::abs(spare - best_spare) <= kEps &&
                 (r->zone_code == kZonePrivate ? k > best : k < best))) {
                best = k;
                best_spare = spare;
            }
        }
        if (best < 0) continue;
        a.used[best] += r->target;
        ++a.optionals_in;
    }
    return a;
}

// Roof term of a rect footprint: ridge height above the eave (C2).
double roof_term_rect(int w_cells, int d_cells, double pitch, const MassingParams& params) {
    const double half_span = (std::min(w_cells, d_cells) * params.ir.cell + params.ir.t_exterior) / 2;
    return 0.05 * half_span * std::tan(glm::radians(pitch));
}

std::vector<double> pitch_values(const ProgramStyle& style) {
    // Deterministic discretization: step 5, at most 5 values (algorithms §3).
    std::vector<double> out;
    if (style.pitch_max - style.pitch_min <= 20.0) {
        for (double p = style.pitch_min; p <= style.pitch_max + kEps; p += 5.0) out.push_back(p);
    } else {
        for (int i = 0; i < 5; ++i)
            out.push_back(style.pitch_min + (style.pitch_max - style.pitch_min) * i / 4.0);
    }
    return out;
}

const std::vector<double>& eave_rise_values() {
    static const std::vector<double> kRises = {0.0, 0.4, 0.8, 1.2};
    return kRises;
}

void score_candidate(MassingCandidate& cand, const ExpandedProgram& prog,
                     const MassingParams& params, double footprint_area, double footprint_perim,
                     double roof_term, double prop_ratio) {
    const Assignment a = assign_rooms(prog, cand.net_area, cand.storeys, cand.attic, params);
    cand.used_area = a.used;
    cand.optionals_in = a.optionals_in;

    MassingScore sc;
    for (size_t k = 0; k < cand.net_area.size(); ++k) {
        const double req = a.used[k] * (1.0 + params.comms_share);
        sc.fit += std::pow(std::max(0.0, req - cand.net_area[k]), 2.0);
        sc.waste += std::max(0.0, cand.net_area[k] - req);
    }
    sc.compact = footprint_perim / std::sqrt(std::max(1.0, footprint_area));
    sc.roof = roof_term;
    const double center = std::sqrt(prog.style.prop_min * prog.style.prop_max);
    sc.prop = std::abs(std::log(prop_ratio / center));
    sc.opt = -static_cast<double>(a.optionals_in);
    sc.total = params.w.fit * sc.fit + params.w.waste * sc.waste + params.w.compact * sc.compact +
               params.w.roof * sc.roof + params.w.prop * sc.prop + params.w.opt * sc.opt;
    cand.score = sc;
}

// Deterministic total order for candidates (N1).
std::string candidate_key(const MassingCandidate& c) {
    std::ostringstream s;
    s << c.shape << "|" << c.storeys << "|" << c.attic << "|" << fmt(c.pitch, 2) << "|"
      << fmt(c.eave_rise, 2);
    for (const MassingVolume& v : c.volumes) {
        const BBox b = bbox_of(v.outline);
        s << "|" << fmt(b.x0, 3) << "," << fmt(b.z0, 3) << "," << fmt(b.x1, 3) << ","
          << fmt(b.z1, 3);
    }
    return s.str();
}

bool candidate_less(const MassingCandidate& a, const MassingCandidate& b) {
    if (std::abs(a.score.total - b.score.total) > 1e-12) return a.score.total < b.score.total;
    return candidate_key(a) < candidate_key(b);
}

std::string score_breakdown(const MassingScore& sc) {
    std::ostringstream s;
    s << "fit " << fmt(sc.fit, 2) << " | waste " << fmt(sc.waste, 1) << " | compact "
      << fmt(sc.compact, 2) << " | roof " << fmt(sc.roof, 2) << " | prop " << fmt(sc.prop, 3)
      << " | opt " << fmt(sc.opt, 0);
    return s.str();
}

std::string describe(const MassingCandidate& cand) {
    std::ostringstream s;
    const BBox b = bbox_of(cand.volumes.front().outline);
    s << cand.shape << " " << fmt(b.x1 - b.x0) << "x" << fmt(b.z1 - b.z0) << " m, "
      << cand.storeys << " storey(s)" << (cand.attic ? " + attic" : "") << ", "
      << roof_kind_name(cand.volumes.front().roof_kind) << " " << fmt(cand.pitch, 0) << " deg";
    if (cand.attic) s << ", eave_rise " << fmt(cand.eave_rise);
    if (cand.volumes.size() > 1) {
        const BBox ab = bbox_of(cand.volumes.back().outline);
        s << ", annex " << fmt(ab.x1 - ab.x0) << "x" << fmt(ab.z1 - ab.z0) << " m";
    }
    s << " -> score " << fmt(cand.score.total, 3) << " (" << score_breakdown(cand.score) << ")";
    return s.str();
}

// Sort + diverse top-M + report; shared by the scan and the pin path.
void finish_massing(const ExpandedProgram& prog, const Site& site, const MassingParams& params,
                    std::vector<MassingCandidate> valid, MassingResult& result) {
    std::sort(valid.begin(), valid.end(), candidate_less);
    // Exact duplicates (same outline key and score: e.g. L cuts 2x4 vs 4x2)
    // are listed once; diversity keeps at most two candidates per
    // (storeys, attic, shape) key.
    std::set<std::string> seen;
    std::map<std::string, int> key_count;
    for (const MassingCandidate& c : valid) {
        std::ostringstream dk;
        dk << candidate_key(c) << "|" << fmt(c.score.total, 6);
        if (!seen.insert(dk.str()).second) continue;
        std::ostringstream k;
        k << c.storeys << "|" << c.attic << "|" << c.shape;
        const int count = ++key_count[k.str()];
        if (count > 2) continue;
        result.top.push_back(c);
        if (result.top.size() >= static_cast<size_t>(params.top_m)) break;
    }
    if (result.top.empty()) {
        result.ok = false;
        return;
    }
    result.chosen = result.top.front();
    result.ok = true;

    std::ostringstream r;
    r << "program: " << prog.type << " — " << prog.rooms.size() << " room instance(s), required "
      << fmt(prog.required_area()) << " m2 + " << fmt(params.comms_share * 100, 0)
      << "% circulation\n";
    r << "site: " << fmt(site.w) << " x " << fmt(site.d) << " m, setback " << fmt(site.setback)
      << ", max storeys " << site.max_storeys << "\n";
    r << "candidates: " << result.candidates_total << " considered, "
      << "rejected: " << result.rejected_site << " site, " << result.rejected_prop
      << " proportions, " << result.rejected_fit << " program fit, " << result.rejected_roof
      << " roof clash\n";
    r << "top " << result.top.size() << ":\n";
    for (size_t i = 0; i < result.top.size(); ++i)
        r << "  " << (i + 1) << ". " << describe(result.top[i]) << "\n";
    r << "chosen: " << describe(result.chosen) << "\n";
    r << "why: best total score = weighted fit (deficit^2), waste, compactness, roof "
         "complexity, facade proportion, optional rooms bonus; per-storey net vs used:";
    const MassingCandidate& c = result.chosen;
    for (size_t k = 0; k < c.net_area.size(); ++k)
        r << "\n  storey " << k << ": net " << fmt(c.net_area[k]) << " m2, used "
          << fmt(k < c.used_area.size() ? c.used_area[k] : 0.0) << " m2";
    r << "\n";
    result.report = r.str();
}

std::string no_fit_error(const ExpandedProgram& prog, const Site& site,
                         const MassingParams& params, int n_max, int w_max, int d_max) {
    const double s = params.ir.cell;
    const double max_net = w_max * s * d_max * s * (1.0 - params.kappa_partitions) * n_max;
    std::ostringstream e;
    e << "the program does not fit the plot: required " << fmt(prog.required_area())
      << " m2 net (+ circulation), the plot allows at most ~" << fmt(max_net) << " m2 over "
      << n_max << " storey(s) of " << fmt(w_max * s) << " x " << fmt(d_max * s)
      << " m; loosen: a bigger plot, more storeys (site max_storeys / program storeys.max), "
         "smaller room areas or fewer required rooms";
    return e.str();
}

}  // namespace

MassingResult generate_massing(const ExpandedProgram& prog, const Site& site,
                               const MassingParams& params, uint64_t seed) {
    (void)seed;  // the scan is exhaustive and fixed-order; the seed lands in the IR
    MassingResult result;
    const double s = params.ir.cell;
    const double t_ext = params.ir.t_exterior;
    const int hearth_channels = prog.hearth_count() > 0 ? 1 : 0;
    const double h_ground = prog.h_ground > 0.0 ? prog.h_ground : params.ir.h_ground;

    const double buildable_w = site.w - 2 * site.setback;
    const double buildable_d = site.d - 2 * site.setback;
    const int w_max =
        static_cast<int>(std::floor((buildable_w - t_ext - 2 * params.overhang) / s + kEps));
    const int d_max =
        static_cast<int>(std::floor((buildable_d - t_ext - 2 * params.overhang) / s + kEps));
    const int n_max = std::min(prog.storeys_max, site.max_storeys);
    const std::vector<double> pitches = pitch_values(prog.style);
    static const std::vector<double> kNoRise = {0.0};

    std::vector<MassingCandidate> valid;
    for (int w = 6; w <= w_max; ++w)
        for (int d = 6; d <= d_max; ++d) {
            ++result.candidates_total;
            const double ratio = static_cast<double>(w) / d;
            if (ratio < prog.style.prop_min - kEps || ratio > prog.style.prop_max + kEps) {
                ++result.rejected_prop;
                continue;
            }
            for (int n = prog.storeys_min; n <= n_max; ++n) {
                for (int attic_i = 0; attic_i < 2; ++attic_i) {
                    const bool attic = attic_i != 0;
                    if (attic && prog.attic == AtticMode::Forbidden) continue;
                    if (!attic && prog.attic == AtticMode::Required) continue;
                    for (double pitch : pitches)
                        for (double rise : (attic ? eave_rise_values() : kNoRise)) {
                            MassingCandidate cand;
                            cand.shape = "rect";
                            cand.storeys = n;
                            cand.attic = attic;
                            cand.pitch = pitch;
                            cand.eave_rise = attic ? rise : 0.0;
                            MassingVolume vol;
                            vol.outline = rect_outline(0, 0, w * s, d * s, t_ext);
                            vol.storeys = n;
                            vol.attic = attic;
                            vol.roof_kind = prog.style.roof_kind;
                            vol.pitch_by_edge = prog.style.roof_kind == kRoofGable
                                                    ? std::vector<double>{90.0, pitch, 90.0, pitch}
                                                    : std::vector<double>(4, pitch);
                            vol.eave_rise = cand.eave_rise;
                            cand.volumes.push_back(vol);
                            cand.net_area = storey_areas(w, d, 0, n, attic, vol.roof_kind, pitch,
                                                         cand.eave_rise, 0.0, hearth_channels,
                                                         params);
                            if (!assign_rooms(prog, cand.net_area, n, attic, params)
                                     .all_required) {
                                ++result.rejected_fit;
                                continue;
                            }
                            score_candidate(cand, prog, params, w * s * d * s,
                                            2.0 * (w + d) * s, roof_term_rect(w, d, pitch, params),
                                            ratio);
                            valid.push_back(cand);
                        }
                }
            }
        }

    // Top-K rect candidates seed the L and annex families (algorithms §3).
    constexpr size_t kTopK = 8;
    std::vector<MassingCandidate> top_rects;
    {
        std::vector<MassingCandidate> sorted = valid;
        std::sort(sorted.begin(), sorted.end(), candidate_less);
        for (size_t i = 0; i < sorted.size() && top_rects.size() < kTopK; ++i)
            top_rects.push_back(sorted[i]);
    }

    for (const MassingCandidate& base : top_rects) {
        const MassingVolume& bv = base.volumes.front();
        const BBox bb = bbox_of(bv.outline);
        const int w = static_cast<int>(std::lround((bb.x1 - bb.x0 - t_ext) / s));
        const int d = static_cast<int>(std::lround((bb.z1 - bb.z0 - t_ext) / s));

        // L family: corner cuts of 2..4 x 2..4 cells (valleys/hips from the
        // skeleton, C2; L volumes scan as hip roofs). Corner 0 only: on a
        // symmetric rect the other corners are mirrors (same score, G6 cares
        // about the facade corner, not G2).
        for (int corner = 0; corner < 1; ++corner)
            for (int a = 2; a <= 4; ++a)
                for (int b = 2; b <= 4; ++b) {
                    if (a * 2 >= w || b * 2 >= d) continue;
                    ++result.candidates_total;
                    MassingCandidate cand = base;
                    cand.shape = "l";
                    MassingVolume vol = bv;
                    vol.roof_kind = kRoofHip;
                    vol.outline = l_outline(0, 0, w * s, d * s, t_ext, corner, a * s, b * s);
                    vol.pitch_by_edge.assign(6, base.pitch);
                    cand.volumes = {vol};
                    cand.net_area = storey_areas(w, d, a * b, base.storeys, base.attic, kRoofHip,
                                                 base.pitch, base.eave_rise, 0.0,
                                                 hearth_channels, params);
                    if (base.attic) {
                        const double useful = attic_useful(vol, w, d, params);
                        double net = useful * (1.0 - params.kappa_partitions);
                        net -= params.stair_area * 0.5;
                        cand.net_area.back() = std::max(0.0, net);
                    }
                    if (!assign_rooms(prog, cand.net_area, base.storeys, base.attic, params)
                             .all_required) {
                        ++result.rejected_fit;
                        continue;
                    }
                    const double gross = (static_cast<double>(w) * d - a * b) * s * s;
                    const double roof_term = 1.0 + roof_term_rect(w, d, base.pitch, params);
                    score_candidate(cand, prog, params, gross, 2.0 * (w + d) * s, roof_term,
                                    static_cast<double>(w) / d);
                    valid.push_back(cand);
                }

        // Annex family: a 1-storey shed against the back / left / right wall.
        for (int side = 0; side < 3; ++side)  // 0 back (-Z), 1 left (-X), 2 right (+X)
            for (int wa = 4; wa <= 8; wa += 2)
                for (int da = 4; da <= 6; da += 2) {
                    ++result.candidates_total;
                    const double annex_eave = h_ground + params.ir.slab_t;
                    const double annex_top =
                        annex_eave + (da * s + t_ext) * std::tan(glm::radians(base.pitch));
                    // §7.2: the annex roof stays apron_gap below the main roof
                    // surface along the attachment edge. Back = an eave edge
                    // of a gable main: the main roof over it is at eave_y. On
                    // a gable side the profile rises toward the ridge.
                    const double main_plate =
                        h_ground + params.ir.slab_t +
                        (base.storeys - 1) * (params.ir.h_upper + params.ir.slab_t);
                    const double main_eave = main_plate + base.eave_rise;
                    double main_roof_at = main_eave;
                    if (side != 0 && bv.roof_kind == kRoofGable) {
                        const double d_main = d * s + t_ext;
                        const double annex_half = wa * s / 2 + t_ext / 2;
                        main_roof_at = main_eave + (d_main / 2 - annex_half) *
                                                       std::tan(glm::radians(base.pitch));
                    }
                    if (annex_top > main_roof_at - params.apron_gap + kEps) {
                        ++result.rejected_roof;
                        continue;
                    }
                    MassingVolume annex;
                    double ax0 = 0.0, az0 = 0.0, ax1 = 0.0, az1 = 0.0;
                    if (side == 0) {  // back: shares the main back wall line
                        ax0 = (w * s - wa * s) / 2;
                        ax1 = ax0 + wa * s;
                        az0 = -da * s;
                        az1 = 0.0;
                    } else if (side == 1) {
                        ax0 = -da * s;
                        ax1 = 0.0;
                        az0 = (d * s - wa * s) / 2;
                        az1 = az0 + wa * s;
                    } else {
                        ax0 = w * s;
                        ax1 = w * s + da * s;
                        az0 = (d * s - wa * s) / 2;
                        az1 = az0 + wa * s;
                    }
                    annex.outline = rect_outline(ax0, az0, ax1, az1, t_ext);
                    annex.storeys = 1;
                    annex.roof_kind = kRoofShed;
                    annex.pitch_by_edge.assign(4, 90.0);
                    // Shed: the attachment edge and both side edges vertical.
                    if (side == 0) annex.pitch_by_edge[3] = base.pitch;  // outer: z-min edge
                    else annex.pitch_by_edge[0] = base.pitch;            // outer: x-min edge
                    const BBox ub = bbox_union(bbox_of(bv.outline), bbox_of(annex.outline));
                    if (ub.x1 - ub.x0 + 2 * params.overhang > buildable_w + kEps ||
                        ub.z1 - ub.z0 + 2 * params.overhang > buildable_d + kEps) {
                        ++result.rejected_site;
                        continue;
                    }
                    MassingCandidate cand = base;
                    cand.shape = "rect+annex";
                    cand.volumes.push_back(annex);
                    const double annex_net =
                        (wa * s - t_ext) * (da * s - t_ext) * (1.0 - params.kappa_partitions);
                    cand.net_area = storey_areas(w, d, 0, base.storeys, base.attic, bv.roof_kind,
                                                 base.pitch, base.eave_rise, annex_net,
                                                 hearth_channels, params);
                    if (!assign_rooms(prog, cand.net_area, base.storeys, base.attic, params)
                             .all_required) {
                        ++result.rejected_fit;
                        continue;
                    }
                    const double gross = w * s * d * s + wa * s * da * s;
                    const double shared = std::min(wa * s, side == 0 ? w * s : d * s);
                    const double perim = 2.0 * (w + d) * s + 2.0 * (wa + da) * s - 2.0 * shared;
                    const double roof_term = 1.0 + roof_term_rect(w, d, base.pitch, params);
                    score_candidate(cand, prog, params, gross, perim, roof_term,
                                    static_cast<double>(w) / d);
                    valid.push_back(cand);
                }
    }

    if (valid.empty()) {
        result.ok = false;
        result.error = no_fit_error(prog, site, params, n_max, w_max, d_max);
        result.report = result.error + "\n";
        return result;
    }
    finish_massing(prog, site, params, std::move(valid), result);
    return result;
}

MassingResult massing_from_pin(const ExpandedProgram& prog, const Site& site,
                               const MassingParams& params, uint64_t seed,
                               std::vector<MassingVolume> volumes, const std::string& pin_desc) {
    (void)seed;
    MassingResult result;
    const double s = params.ir.cell;
    const double t_ext = params.ir.t_exterior;
    const int hearth_channels = prog.hearth_count() > 0 ? 1 : 0;

    // §4.5: a pinned massing is validated, not re-searched.
    MassingCandidate cand;
    cand.shape = volumes.size() > 1 ? "pinned+annex" : "pinned";
    for (const MassingVolume& v : volumes) {
        // Grid consistency: every edge length is a whole number of cells
        // (full rect sides carry an extra t_ext) — otherwise the pin comes
        // from another grid.
        for (size_t i = 0; i < v.outline.size(); ++i) {
            const glm::dvec2 dvec = v.outline[(i + 1) % v.outline.size()] - v.outline[i];
            const double len = glm::length(dvec);
            const double r1 = std::abs(len / s - std::round(len / s));
            const double r2 = std::abs((len - t_ext) / s - std::round((len - t_ext) / s));
            if (std::min(r1, r2) > 1e-3) {
                result.error = "pin: volume edge of " + fmt(len, 3) +
                               " m is not on the cell grid (cell " + fmt(s, 2) +
                               " m) — the pin comes from another grid";
                result.report = result.error + "\n";
                return result;
            }
        }
        cand.storeys = std::max(cand.storeys, v.storeys);
        cand.attic = cand.attic || v.attic;
    }
    cand.volumes = volumes;
    const MassingVolume& main = volumes.front();
    cand.pitch = 0.0;
    for (double p : main.pitch_by_edge)
        if (p < 90.0) cand.pitch = p;
    cand.eave_rise = main.eave_rise;

    // Site fit: the union outline + overhang stays in the plot (I8).
    BBox ub = bbox_of(volumes.front().outline);
    for (const MassingVolume& v : volumes) ub = bbox_union(ub, bbox_of(v.outline));
    if (ub.x1 - ub.x0 + 2 * params.overhang > site.w - 2 * site.setback + kEps ||
        ub.z1 - ub.z0 + 2 * params.overhang > site.d - 2 * site.setback + kEps) {
        result.error = "pin: the pinned massing (" + fmt(ub.x1 - ub.x0, 2) + " x " +
                       fmt(ub.z1 - ub.z0, 2) + " m + overhang) does not fit the plot " +
                       fmt(site.w) + " x " + fmt(site.d) + " m with setback " +
                       fmt(site.setback) + " — loosen the pin or the plot";
        result.report = result.error + "\n";
        return result;
    }

    const BBox mb = bbox_of(main.outline);
    const int w = static_cast<int>(std::lround((mb.x1 - mb.x0 - t_ext) / s));
    const int d = static_cast<int>(std::lround((mb.z1 - mb.z0 - t_ext) / s));
    // Corner cut of the main volume, recovered from the area: the outline is
    // expanded by t_ext/2 around the w*d cell field, so the cell count under
    // the polygon is (area - wall band) / s^2.
    const int cut = std::max(0, w * d - static_cast<int>(std::lround(
                                          (polygon_area(main.outline) -
                                           t_ext * s * (w + d) - t_ext * t_ext) /
                                          (s * s))));
    double annex_net = 0.0;
    for (size_t i = 1; i < volumes.size(); ++i) {
        const BBox ab = bbox_of(volumes[i].outline);
        annex_net += (ab.x1 - ab.x0 - t_ext) * (ab.z1 - ab.z0 - t_ext) *
                     (1.0 - params.kappa_partitions);
    }
    cand.net_area = storey_areas(w, d, cut, main.storeys, main.attic, main.roof_kind, cand.pitch,
                                 cand.eave_rise, annex_net, hearth_channels, params);
    if (main.attic && cut > 0) {
        // storey_areas uses the analytic rect attic; an L footprint needs the
        // skeleton mask (same override as the L family).
        const double useful = attic_useful(main, w, d, params);
        double net = useful * (1.0 - params.kappa_partitions);
        net -= params.stair_area * 0.5;
        cand.net_area.back() = std::max(0.0, net);
    }
    if (!assign_rooms(prog, cand.net_area, main.storeys, main.attic, params).all_required) {
        result.error = "pin: the pinned massing does not fit the program (a required room has "
                       "no storey with enough area) — loosen the pin or the program";
        result.report = result.error + "\n";
        return result;
    }
    // Same terms as the generated families: a corner cut keeps the perimeter,
    // +1.0 of roof complexity per non-rect shape (the L penalty and each annex).
    const double gross = (static_cast<double>(w) * d - cut) * s * s;
    const double roof_term = roof_term_rect(w, d, cand.pitch, params) + (cut > 0 ? 1.0 : 0.0) +
                             (volumes.size() - 1) * 1.0;
    score_candidate(cand, prog, params, gross, 2.0 * (w + d) * s, roof_term,
                    static_cast<double>(w) / d);
    result.ok = true;
    result.chosen = cand;
    result.top = {cand};
    std::ostringstream r;
    r << "massing pinned by " << pin_desc << " (§4.5: validated, not searched)\n"
      << "chosen: " << describe(cand) << "\n";
    for (size_t k = 0; k < cand.net_area.size(); ++k)
        r << "  storey " << k << ": net " << fmt(cand.net_area[k]) << " m2, used "
          << fmt(k < cand.used_area.size() ? cand.used_area[k] : 0.0) << " m2\n";
    result.report = r.str();
    return result;
}

bool massing_to_ir(const MassingCandidate& cand, const ExpandedProgram& prog,
                   const MassingParams& params, uint64_t seed, Ir& out, std::string& err) {
    const double h_ground = prog.h_ground > 0.0 ? prog.h_ground : params.ir.h_ground;
    const int n_full = cand.storeys;

    Ir ir;
    ir.seed = seed;
    ir.program = prog.type;
    ir.params = params.ir;

    // Storeys: full storeys, then the attic (§5.3).
    double y = 0.0;
    for (int k = 0; k < n_full; ++k) {
        IrStorey st;
        st.id = k == 0 ? "ground" : (k == 1 ? "upper" : "upper" + std::to_string(k));
        st.kind_code = k == 0 ? kStoreyGround : kStoreyUpper;
        st.y = y;
        st.h = k == 0 ? h_ground : params.ir.h_upper;
        st.slab_t = params.ir.slab_t;
        y += st.h + st.slab_t;
        ir.storeys.push_back(st);
    }
    if (cand.attic) {
        IrStorey st;
        st.id = "attic";
        st.kind_code = kStoreyAttic;
        st.y = y;
        st.h = cand.eave_rise;  // the attic walls carry the raised eave (§5.6)
        st.slab_t = 0.0;
        ir.storeys.push_back(st);
    }
    const double attic_floor_y = y;

    // Building frame: the origin is the center of the footprint union (§5.1).
    BBox ub = bbox_of(cand.volumes.front().outline);
    for (const MassingVolume& v : cand.volumes) ub = bbox_union(ub, bbox_of(v.outline));
    const glm::dvec2 center{(ub.x0 + ub.x1) / 2, (ub.z0 + ub.z1) / 2};

    for (size_t vi = 0; vi < cand.volumes.size(); ++vi) {
        const MassingVolume& mv = cand.volumes[vi];
        IrVolume vol;
        vol.id = vi == 0 ? "main" : "annex" + (vi > 1 ? std::to_string(vi) : "");
        for (const glm::dvec2& p : mv.outline) vol.outline.push_back(p - center);
        vol.storey0 = 0;
        vol.storey1 = mv.storeys - 1 + (mv.attic ? 1 : 0);
        vol.roof.kind_code = mv.roof_kind;
        vol.roof.pitch_by_edge = mv.pitch_by_edge;
        vol.roof.overhang = params.overhang;
        vol.roof.eave_rise = mv.eave_rise;
        // eave_y = the plate above the topmost full storey + eave_rise (§5.6).
        double plate = 0.0;
        for (int k = 0; k < mv.storeys; ++k) {
            const double h = k == 0 ? h_ground : params.ir.h_upper;
            plate += h + params.ir.slab_t;
        }
        vol.roof.eave_y = plate + mv.eave_rise;
        vol.roof.roof_t = params.ir.roof_t;
        ir.volumes.push_back(vol);
    }

    // Derived roof data (§10): the same skeleton the geometry will use.
    for (const IrVolume& vol : ir.volumes) {
        RoofModel model;
        if (!build_roof_model(vol, model, err)) {
            err = "massing_to_ir: volume " + vol.id + ": " + err;
            return false;
        }
        for (const IrRoofPlane& p : model.planes) ir.roof.planes.push_back(p);
        for (const IrRoofEdge& e : model.ridges) ir.roof.ridges.push_back(e);
        for (const IrRoofEdge& e : model.valleys) ir.roof.valleys.push_back(e);
        for (const IrRoofEdge& e : model.hips) ir.roof.hips.push_back(e);
    }

    ir.report_cost["massing_fit"] = cand.score.fit;
    ir.report_cost["massing_waste"] = cand.score.waste;
    ir.report_cost["massing_compact"] = cand.score.compact;
    ir.report_cost["massing_roof"] = cand.score.roof;
    ir.report_cost["massing_prop"] = cand.score.prop;
    ir.report_cost["massing_opt"] = cand.score.opt;
    ir.report_cost["massing_total"] = cand.score.total;

    sort_ir(ir);
    (void)attic_floor_y;
    out = std::move(ir);
    return true;
}

}  // namespace building_topology_generator
