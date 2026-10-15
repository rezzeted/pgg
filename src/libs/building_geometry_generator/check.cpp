#include "pch.h"

#include "roof.h"

#include "unit.h"

namespace building_geometry_generator {

namespace btg = building_topology_generator;

namespace {

constexpr double kTol = 1e-3;  // mesh-level tolerance, meters (IR is 1e-4-quantized)

// Stair norms (requirements §5.5). The frozen B0 house hand-rounds its main
// stair to 16 x 0.190625 (0.6 mm over r_max to keep the stacked shaft
// plan-identical on both floors); the tolerance below absorbs that rounding.
// The generator (B3) targets strict compliance via n = ceil(H / r_max).
constexpr double kRiseMax = 0.19 + 1e-3;
constexpr double kGoingMin = 0.25 - 1e-3;
constexpr double kBlondelLo = 0.60 - 2e-3;
constexpr double kBlondelHi = 0.65 + 2e-3;

const FillUnit* find_unit(const FillResult& fill, const std::string& id) {
    for (const auto& u : fill.units)
        if (u.id == id) return &u;
    return nullptr;
}

bool rect_overlap(const btg::IrSlabRect& a, const btg::IrSlabRect& b) {
    const double ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
    const double oz = std::min(a.z1, b.z1) - std::max(a.z0, b.z0);
    return ox > 1e-6 && oz > 1e-6;
}

bool inside_well(const std::vector<btg::IrSlabRect>& well, double x, double z) {
    for (const auto& r : well) {
        if (x >= r.x0 - 1e-4 && x <= r.x1 + 1e-4 && z >= r.z0 - 1e-4 && z <= r.z1 + 1e-4)
            return true;
    }
    return false;
}

// (s, t) frame rect -> world corners; s along `dir`, t along plan_left(dir).
void check_frame_rect(const btg::IrStair& st, double s0, double s1, double t0, double t1,
                      const char* what, CheckResult& res) {
    const glm::dvec2 left = plan_left(st.dir);
    const glm::dvec2 corners[4] = {
        st.start + st.dir * s0 + left * t0, st.start + st.dir * s1 + left * t0,
        st.start + st.dir * s1 + left * t1, st.start + st.dir * s0 + left * t1};
    // Inset toward the centroid so boundary touches do not false-positive.
    glm::dvec2 c{0.0, 0.0};
    for (const auto& p : corners) c += p;
    c *= 0.25;
    for (const auto& p : corners) {
        const glm::dvec2 q = p + (c - p) * 1e-3;
        if (!inside_well(st.well, q.x, q.y)) {
            res.failures.push_back("stair " + st.id + ": slab opening too small — " + what +
                                   " corner (" + std::to_string(q.x) + ", " +
                                   std::to_string(q.y) + ") is outside the well");
            return;
        }
    }
}

void check_stair(const btg::Ir& ir, const btg::IrStair& st, CheckResult& res) {
    const btg::IrStorey* from = find_storey(ir, st.from);
    const btg::IrStorey* to = find_storey(ir, st.to);
    if (!from || !to) {
        res.failures.push_back("stair " + st.id + ": unknown storey ref");
        return;
    }
    const double h = to->y - from->y;
    if (st.rise > kRiseMax)
        res.failures.push_back("stair " + st.id + ": rise " + std::to_string(st.rise) +
                               " over the 0.19 norm");
    if (st.going < kGoingMin)
        res.failures.push_back("stair " + st.id + ": going " + std::to_string(st.going) +
                               " under the 0.25 norm");
    const double blondel = 2.0 * st.rise + st.going;
    if (blondel < kBlondelLo || blondel > kBlondelHi)
        res.failures.push_back("stair " + st.id + ": Blondel " + std::to_string(blondel) +
                               " outside [0.60, 0.65]");
    if (std::abs(h - st.risers * st.rise) > 5e-3)
        res.failures.push_back("stair " + st.id + ": risers * rise != storey height (" +
                               std::to_string(h) + ")");

    // Required slab opening (§5.5): headroom under the slab above step i is
    // H - t - i * rise; steps with less than h_walk need the opening.
    const btg::IrSlab* slab = nullptr;
    for (const auto& s : ir.slabs) {
        if (s.storey_below == st.from && s.storey_above == st.to) {
            slab = &s;
            break;
        }
    }
    if (!slab) {
        res.failures.push_back("stair " + st.id + ": no slab between " + st.from + " and " +
                               st.to);
        return;
    }
    const double i0 = std::floor((h - slab->t - ir.params.h_walk()) / st.rise) + 1.0;
    const double l1 = (st.risers_low - 1) * st.going;  // march 1 plan length
    const double l2 = (st.risers - st.risers_low - 1) * st.going;  // march 2
    const double hw = st.width * 0.5;
    // March 1 band from step i0's nose up to the landing.
    const double s_open = std::max(0.0, (i0 - 1.0) * st.going);
    if (s_open < l1 - 1e-9)
        check_frame_rect(st, s_open, l1, -hw, hw, "march 1", res);
    // Landing and march 2 are always above the headroom line in B0 stairs.
    check_frame_rect(st, l1, l1 + st.width, -hw, hw, "landing", res);
    if (l2 > 1e-9) {
        if (glm::dot(st.dir2, plan_left(st.dir)) > 0.0) {
            check_frame_rect(st, l1, l1 + st.width, hw, hw + l2, "march 2", res);
        } else {
            check_frame_rect(st, l1, l1 + st.width, -hw - l2, -hw, "march 2", res);
        }
    }
    // Well rects must not eat into the slab.
    for (const auto& wr : st.well)
        for (const auto& sr : slab->rects)
            if (rect_overlap(wr, sr))
                res.failures.push_back("stair " + st.id + ": well rect overlaps a slab rect");
}

void check_chimney(const btg::Ir& ir, const btg::IrChimney& c, const FillResult& fill,
                   CheckResult& res) {
    if (c.roof_plane < 0 || c.roof_plane >= static_cast<int>(ir.roof.planes.size())) {
        res.failures.push_back("chimney " + c.id + ": no exit roof plane");
        return;
    }
    const std::string& vol_id = ir.roof.planes[static_cast<size_t>(c.roof_plane)].volume;
    const btg::IrVolume* vol = nullptr;
    for (const auto& v : ir.volumes)
        if (v.id == vol_id) vol = &v;
    if (!vol) {
        res.failures.push_back("chimney " + c.id + ": unknown volume '" + vol_id + "'");
        return;
    }
    btg::RoofModel model;
    std::string err;
    if (!btg::build_roof_model(*vol, model, err)) {
        res.failures.push_back("chimney " + c.id + ": roof model failed: " + err);
        return;
    }
    double ridge_d = 0.0, ridge_y = 0.0, exit_y = 0.0;
    if (!btg::nearest_ridge(model, c.top.x, c.top.z, ridge_d, ridge_y)) {
        res.failures.push_back("chimney " + c.id + ": no ridge in the exit volume");
        return;
    }
    if (!btg::roof_y_at(model, c.top.x, c.top.z, exit_y)) {
        res.failures.push_back("chimney " + c.id + ": exit point outside the slopes");
        return;
    }
    const double required =
        btg::chimney_top_required(ridge_d, ridge_y, exit_y, ir.params.h_above_roof);
    if (c.top.y < required - kTol)
        res.failures.push_back("chimney " + c.id + ": top " + std::to_string(c.top.y) +
                               " below the required " + std::to_string(required));
    // Tiles must stay out of the stack footprint (the roof hole, §14.2).
    const FillUnit* roof = find_unit(fill, "roof:" + vol_id);
    if (!roof || !roof->mesh) {
        res.failures.push_back("chimney " + c.id + ": roof mesh missing, hole unchecked");
        return;
    }
    const double hx = c.section.x * 0.5 + 0.06 - 1e-3;
    const double hz = c.section.y * 0.5 + 0.06 - 1e-3;
    for (const glm::vec3& p : *roof->mesh->positions) {
        if (std::abs(p.x - c.top.x) < hx && std::abs(p.z - c.top.z) < hz) {
            res.failures.push_back("chimney " + c.id + ": roof tile inside the stack footprint");
            return;
        }
    }
}

void check_gable_walls(const btg::Ir& ir, const FillResult& fill, CheckResult& res) {
    std::map<std::string, btg::RoofModel> models;
    for (const auto& w : ir.walls) {
        if (w.top_profile.empty()) continue;
        const FillUnit* unit = find_unit(fill, w.id);
        if (!unit || !unit->mesh) {
            res.failures.push_back("gable wall " + w.id + ": mesh missing");
            continue;
        }
        for (const glm::vec3& p : *unit->mesh->positions) {
            double roof_y = -1e300;
            bool any = false;
            for (const auto& v : ir.volumes) {
                if (!models.count(v.id)) {
                    btg::RoofModel m;
                    std::string err;
                    if (!btg::build_roof_model(v, m, err)) continue;
                    models.emplace(v.id, std::move(m));
                }
                double y = 0.0;
                if (btg::roof_y_at(models.at(v.id), p.x, p.z, y)) {
                    roof_y = std::max(roof_y, y);
                    any = true;
                }
            }
            if (any && p.y > roof_y + kTol) {
                res.failures.push_back("gable wall " + w.id + ": pokes " +
                                       std::to_string(p.y - roof_y) +
                                       " m above the roof plane");
                break;
            }
        }
    }
}

}  // namespace

CheckResult check_building(const btg::Ir& ir, const FillResult& fill) {
    CheckResult res;
    for (const auto& st : ir.stairs) check_stair(ir, st, res);
    for (const auto& c : ir.chimneys) check_chimney(ir, c, fill, res);
    check_gable_walls(ir, fill, res);
    res.notes.push_back("facings are not in B0: opening-in-facing equality not checked");
    res.notes.push_back("gap/double-geometry scan between wall bodies, nodes and slabs is "
                        "not in B0");
    return res;
}

}  // namespace building_geometry_generator
