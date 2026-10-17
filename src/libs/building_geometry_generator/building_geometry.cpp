#include "pch.h"

#include "building_geometry.h"

#include "pgg/pgg.h"  // formatDiagnostic
#include "pgg/src/eval/modules.h"  // appendImportRoot

#include "unit.h"

namespace building_geometry_generator {

namespace btg = building_topology_generator;

uint64_t unit_seed(uint64_t seed, const std::string& unit_id) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (const unsigned char ch : unit_id) {
        h ^= ch;
        h *= 1099511628211ull;
    }
    uint64_t z = seed + 0x9e3779b97f4a7c15ull + h;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

namespace {

glm::vec3 rot_y_f(const glm::vec3& p, double c, double s) {
    return {static_cast<float>(p.x * c + p.z * s), p.y,
            static_cast<float>(-p.x * s + p.z * c)};
}

// Copy attr columns of one domain, transforming vec3 columns by tag.
pgg::AttrSet transform_attrs(const pgg::AttrSet* in, double c, double s, double tx, double ty,
                             double tz) {
    pgg::AttrSet out;
    if (!in) return out;
    for (const auto& [name, col] : in->columns) {
        pgg::AttrColumn dst;
        dst.typeInfo = col.typeInfo;
        if (const auto* v3 =
                std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&col.data)) {
            const bool affine = col.typeInfo == pgg::AttrTypeInfo::Point;
            const bool rotate = affine || col.typeInfo == pgg::AttrTypeInfo::Vector ||
                                col.typeInfo == pgg::AttrTypeInfo::Normal;
            if (rotate) {
                auto nv = std::make_shared<std::vector<glm::vec3>>();
                nv->reserve((*v3)->size());
                for (const auto& p : **v3) {
                    glm::vec3 q = rot_y_f(p, c, s);
                    if (affine) {
                        q.x += static_cast<float>(tx);
                        q.y += static_cast<float>(ty);
                        q.z += static_cast<float>(tz);
                    }
                    nv->push_back(q);
                }
                dst.data = std::move(nv);
                out.columns[name] = std::move(dst);
                continue;
            }
        }
        dst.data = col.data;
        out.columns[name] = std::move(dst);
    }
    return out;
}

template <typename T>
std::shared_ptr<const std::vector<T>> concat_column(const std::vector<const pgg::AttrColumn*>& cols,
                                                    const std::vector<size_t>& sizes) {
    auto out = std::make_shared<std::vector<T>>();
    size_t total = 0;
    for (const size_t n : sizes) total += n;
    out->reserve(total);
    for (size_t i = 0; i < cols.size(); ++i) {
        if (cols[i]) {
            const auto& v = *std::get<std::shared_ptr<const std::vector<T>>>(cols[i]->data);
            out->insert(out->end(), v.begin(), v.end());
        } else {
            out->resize(out->size() + sizes[i], T{});
        }
    }
    return out;
}

// Union of one attr domain across parts; missing columns zero-filled.
pgg::AttrSet merge_attrs(const std::vector<const pgg::AttrSet*>& sets,
                         const std::vector<size_t>& sizes) {
    std::map<std::string, pgg::AttrTypeInfo> names;  // ordered -> deterministic
    for (const auto* s : sets) {
        if (!s) continue;
        for (const auto& [name, col] : s->columns) names.emplace(name, col.typeInfo);
    }
    pgg::AttrSet out;
    for (const auto& [name, info] : names) {
        std::vector<const pgg::AttrColumn*> cols(sets.size(), nullptr);
        for (size_t i = 0; i < sets.size(); ++i) {
            if (sets[i]) {
                if (const pgg::AttrColumn* c = sets[i]->find(name)) cols[i] = c;
            }
        }
        const pgg::AttrColumn* first = nullptr;
        for (const auto* c : cols) {
            if (c) {
                first = c;
                break;
            }
        }
        pgg::AttrColumn dst;
        dst.typeInfo = info;
        const auto& data = first->data;
        if (std::holds_alternative<std::shared_ptr<const std::vector<float>>>(data)) {
            dst.data = concat_column<float>(cols, sizes);
        } else if (std::holds_alternative<std::shared_ptr<const std::vector<int64_t>>>(data)) {
            dst.data = concat_column<int64_t>(cols, sizes);
        } else if (std::holds_alternative<std::shared_ptr<const std::vector<uint8_t>>>(data)) {
            dst.data = concat_column<uint8_t>(cols, sizes);
        } else if (std::holds_alternative<std::shared_ptr<const std::vector<glm::vec2>>>(data)) {
            dst.data = concat_column<glm::vec2>(cols, sizes);
        } else if (std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(data)) {
            dst.data = concat_column<glm::vec3>(cols, sizes);
        } else if (std::holds_alternative<std::shared_ptr<const std::vector<glm::vec4>>>(data)) {
            dst.data = concat_column<glm::vec4>(cols, sizes);
        } else {
            dst.data = concat_column<std::string>(cols, sizes);
        }
        out.columns[name] = std::move(dst);
    }
    return out;
}

pgg::GroupSet merge_groups(const std::vector<const pgg::GroupSet*>& sets,
                           const std::vector<size_t>& sizes) {
    std::map<std::string, bool> names;
    for (const auto* s : sets) {
        if (!s) continue;
        for (const auto& [name, col] : s->columns) names.emplace(name, true);
    }
    pgg::GroupSet out;
    for (const auto& [name, _] : names) {
        auto col = std::make_shared<pgg::BoolColumn>();
        size_t total = 0;
        for (const size_t n : sizes) total += n;
        col->reserve(total);
        for (size_t i = 0; i < sets.size(); ++i) {
            const pgg::ConstBoolColumnPtr src = sets[i] ? sets[i]->find(name) : nullptr;
            if (src) {
                col->insert(col->end(), src->begin(), src->end());
            } else {
                col->resize(col->size() + sizes[i], 0);
            }
        }
        out.columns[name] = std::move(col);
    }
    return out;
}

}  // namespace

pgg::GeoPtr transform_geo(const pgg::GeoPtr& geo, double yaw_deg, double tx, double ty,
                          double tz) {
    if (!geo) return nullptr;
    const double rad = yaw_deg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);

    auto out = std::make_shared<pgg::Geo>();
    out->kind = geo->kind;

    auto pos = std::make_shared<std::vector<glm::vec3>>();
    if (geo->positions) {
        pos->reserve(geo->positions->size());
        for (const auto& p : *geo->positions) {
            glm::vec3 q = rot_y_f(p, c, s);
            q.x += static_cast<float>(tx);
            q.y += static_cast<float>(ty);
            q.z += static_cast<float>(tz);
            pos->push_back(q);
        }
    }
    out->positions = std::move(pos);

    if (geo->normals) {
        auto nrm = std::make_shared<std::vector<glm::vec3>>();
        nrm->reserve(geo->normals->size());
        for (const auto& n : *geo->normals) nrm->push_back(rot_y_f(n, c, s));
        out->normals = std::move(nrm);
    }

    out->cornerVerts = geo->cornerVerts;
    out->faceOffsets = geo->faceOffsets;
    out->pointAttrs = std::make_shared<const pgg::AttrSet>(
        transform_attrs(geo->pointAttrs.get(), c, s, tx, ty, tz));
    out->cornerAttrs = std::make_shared<const pgg::AttrSet>(
        transform_attrs(geo->cornerAttrs.get(), c, s, tx, ty, tz));
    out->faceAttrs = std::make_shared<const pgg::AttrSet>(
        transform_attrs(geo->faceAttrs.get(), c, s, tx, ty, tz));
    out->detailAttrs = geo->detailAttrs;
    out->pointGroups = geo->pointGroups;
    out->cornerGroups = geo->cornerGroups;
    out->faceGroups = geo->faceGroups;
    out->detailGroups = geo->detailGroups;
    out->instanceSources = geo->instanceSources;
    return out;
}

pgg::GeoPtr merge_meshes(const std::vector<pgg::GeoPtr>& parts) {
    auto out = std::make_shared<pgg::Geo>();
    out->kind = pgg::GeoKind::Mesh;

    auto pos = std::make_shared<std::vector<glm::vec3>>();
    auto nrm = std::make_shared<std::vector<glm::vec3>>();
    auto corners = std::make_shared<std::vector<int32_t>>();
    auto offsets = std::make_shared<std::vector<int32_t>>();
    offsets->push_back(0);
    bool all_normals = true;

    std::vector<const pgg::AttrSet*> point_sets, corner_sets, face_sets;
    std::vector<const pgg::GroupSet*> point_grps, face_grps;
    std::vector<size_t> point_sizes, corner_sizes, face_sizes;

    for (const auto& g : parts) {
        if (!g || g->kind != pgg::GeoKind::Mesh) continue;
        const int32_t pbase = static_cast<int32_t>(pos->size());
        const int32_t cbase = static_cast<int32_t>(corners->size());
        if (g->positions) pos->insert(pos->end(), g->positions->begin(), g->positions->end());
        if (g->normals) {
            nrm->insert(nrm->end(), g->normals->begin(), g->normals->end());
        } else {
            all_normals = false;
        }
        if (g->cornerVerts) {
            for (const int32_t ci : *g->cornerVerts) corners->push_back(pbase + ci);
        }
        if (g->faceOffsets) {
            for (size_t i = 1; i < g->faceOffsets->size(); ++i) {
                offsets->push_back(cbase + (*g->faceOffsets)[i]);
            }
        }
        point_sets.push_back(g->pointAttrs.get());
        corner_sets.push_back(g->cornerAttrs.get());
        face_sets.push_back(g->faceAttrs.get());
        point_grps.push_back(g->pointGroups.get());
        face_grps.push_back(g->faceGroups.get());
        point_sizes.push_back(g->pointCount());
        corner_sizes.push_back(g->cornerCount());
        face_sizes.push_back(g->faceCount());
    }

    out->positions = std::move(pos);
    if (all_normals && !parts.empty()) out->normals = std::move(nrm);
    out->cornerVerts = std::move(corners);
    out->faceOffsets = std::move(offsets);
    out->pointAttrs = std::make_shared<const pgg::AttrSet>(merge_attrs(point_sets, point_sizes));
    out->cornerAttrs = std::make_shared<const pgg::AttrSet>(merge_attrs(corner_sets, corner_sizes));
    out->faceAttrs = std::make_shared<const pgg::AttrSet>(merge_attrs(face_sets, face_sizes));
    out->pointGroups = std::make_shared<const pgg::GroupSet>(merge_groups(point_grps, point_sizes));
    out->faceGroups = std::make_shared<const pgg::GroupSet>(merge_groups(face_grps, face_sizes));
    return out;
}

namespace {

// One asset run; null on failure (diagnostics appended inside).
pgg::GeoPtr run_unit(const std::string& id, const std::string& asset, pgg::RunParams& rp,
                     const FillParams& params, FillResult& res) {
    rp.importRoots = params.import_roots;
    const pgg::RunResult rr = pgg::runFile(asset, rp, {"mesh"});
    if (rr.hasErrors()) {
        for (const auto& d : rr.diagnostics)
            res.diagnostics.push_back(id + ": " + pgg::formatDiagnostic(d, asset));
        return nullptr;
    }
    if (rr.outputs.empty()) {
        res.diagnostics.push_back(id + ": " + asset + ": no 'mesh' output");
        return nullptr;
    }
    const pgg::GeoPtr g = pgg::asGeo(rr.outputs.front().value);
    if (!g || g->kind != pgg::GeoKind::Mesh || g->pointCount() == 0) {
        res.diagnostics.push_back(id + ": " + asset +
                                  ": 'mesh' output is not a non-empty mesh");
        return nullptr;
    }
    return g;
}

void add_unit(FillResult& res, const FillParams& params, const std::string& id,
              const std::string& slot_rel, pgg::RunParams&& rp, const Placement& pl) {
    ++res.units_total;
    const std::string asset = params.assets_dir + "/" + slot_rel;
    rp.values.emplace_back("rng_seed",
                           pgg::Value(static_cast<int64_t>(unit_seed(params.seed, id))));
    FillUnit unit{id, asset, run_unit(id, asset, rp, params, res)};
    if (unit.mesh) {
        unit.mesh = transform_geo(unit.mesh, pl.yaw_deg, pl.at.x, pl.at.y, pl.at.z);
        ++res.units_built;
    }
    res.units.push_back(std::move(unit));
}

bool asset_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

}  // namespace

FillResult fill_building(const btg::Ir& ir, const FillParams& params) {
    FillResult res;

    for (const auto& w : ir.walls) {
        const WallUnit u = wall_unit(ir, w);
        if (!find_storey(ir, w.storey))
            res.diagnostics.push_back("wall " + w.id + ": unknown storey '" + w.storey + "'");
        pgg::RunParams rp;
        rp.values = {{"len", pgg::Value(static_cast<float>(u.len))},
                     {"thick", pgg::Value(static_cast<float>(u.thick))},
                     {"h_a", pgg::Value(static_cast<float>(u.h))},
                     {"h_b", pgg::Value(static_cast<float>(u.h))},
                     {"class", pgg::Value(u.class_code)},
                     {"cuts", pgg::Value(u.cuts.build())},
                     {"top_planes", pgg::Value(u.top_planes.build())}};
        add_unit(res, params, w.id, "walls/body.pgg", std::move(rp), u.pl);
    }

    for (const auto& n : ir.nodes) {
        const NodeUnit u = node_unit(ir, n);
        pgg::RunParams rp;
        rp.values = {{"size", pgg::Value(u.size)}};
        add_unit(res, params, n.id, "walls/node.pgg", std::move(rp), u.pl);
    }

    for (const auto& s : ir.slabs) {
        const SlabUnit u = slab_unit(s);
        pgg::RunParams rp;
        rp.values = {{"rects", pgg::Value(u.rects.build())},
                     {"t", pgg::Value(static_cast<float>(u.t))}};
        add_unit(res, params, u.id, "floors/slab.pgg", std::move(rp), Placement{});
    }

    for (const auto& s : ir.stairs) {
        if (s.type_code != btg::kStairL) {
            res.diagnostics.push_back("stair " + s.id + ": B0 fills only L-stairs, skipped");
            continue;
        }
        const StairUnit u = stair_unit(ir, s);
        pgg::RunParams rp;
        rp.values = {{"risers", pgg::Value(u.risers)},
                     {"rise", pgg::Value(static_cast<float>(u.rise))},
                     {"going", pgg::Value(static_cast<float>(u.going))},
                     {"w", pgg::Value(static_cast<float>(u.w))},
                     {"risers_low", pgg::Value(u.risers_low)},
                     {"turn", pgg::Value(u.turn)}};
        add_unit(res, params, s.id, "stairs/stair_l.pgg", std::move(rp), u.pl);
    }

    for (const auto& r : ir.railings) {
        const RailingUnit u = railing_unit(ir, r);
        pgg::RunParams rp;
        rp.values = {{"len", pgg::Value(static_cast<float>(u.len))},
                     {"h", pgg::Value(static_cast<float>(u.h))}};
        add_unit(res, params, r.id, "rails/rail.pgg", std::move(rp), u.pl);
    }

    for (const auto& w : ir.walls) {
        for (const auto& o : w.openings) {
            const OpeningUnit u = opening_unit(ir, w, o);
            const std::string slot_rel = "openings/" + u.type + ".pgg";
            if (!asset_exists(params.assets_dir + "/" + slot_rel)) {
                ++res.units_total;
                res.diagnostics.push_back("opening " + o.id + ": no asset for type '" + u.type +
                                          "', skipped");
                continue;
            }
            pgg::RunParams rp;
            rp.values = {{"w", pgg::Value(static_cast<float>(u.w))},
                         {"h", pgg::Value(static_cast<float>(u.h))}};
            add_unit(res, params, o.id, slot_rel, std::move(rp), u.pl);
        }
    }

    for (const auto& c : ir.chimneys) {
        const ChimneyUnit u = chimney_unit(c);
        pgg::RunParams rp;
        rp.values = {{"w", pgg::Value(static_cast<float>(u.w))},
                     {"d", pgg::Value(static_cast<float>(u.d))},
                     {"h", pgg::Value(static_cast<float>(u.h))}};
        add_unit(res, params, c.id, "chimneys/stack.pgg", std::move(rp), u.pl);

        for (const auto& h : c.hearths) {
            if (h.kind_code != btg::kHearthFireplace) {
                ++res.units_total;
                res.diagnostics.push_back("hearth at " + h.wall + ": B0 fills only fireplaces, skipped");
                continue;
            }
            HearthUnit hu;
            hu.id = "hearth:" + h.space + "@" + h.wall;
            if (!hearth_unit(ir, h, hu)) {
                ++res.units_total;
                res.diagnostics.push_back("hearth at " + h.wall + ": unknown wall, skipped");
                continue;
            }
            pgg::RunParams rp2;
            rp2.values = {{"w", pgg::Value(static_cast<float>(hu.w))}};
            add_unit(res, params, hu.id, "hearths/fireplace.pgg", std::move(rp2), hu.pl);
        }
    }

    for (const auto& v : ir.volumes) {
        const RoofUnit u = roof_unit(ir, v, res.diagnostics);
        {
            pgg::RunParams rp;
            rp.values = {{"outline", pgg::Value(u.outline.build())},
                         {"pitch", pgg::Value(static_cast<float>(u.pitch))},
                         {"y0", pgg::Value(static_cast<float>(u.y0))},
                         {"overhang", pgg::Value(static_cast<float>(u.overhang))},
                         {"holes", pgg::Value(u.holes.build())},
                         {"hole_margin", pgg::Value(static_cast<float>(u.hole_margin))}};
            add_unit(res, params, "roof:" + v.id, "roof/roof.pgg", std::move(rp), Placement{});
        }
        const btg::IrStorey* top =
            v.storey1 >= 0 && v.storey1 < static_cast<int>(ir.storeys.size())
                ? &ir.storeys[static_cast<size_t>(v.storey1)]
                : nullptr;
        if (top && top->kind_code == btg::kStoreyAttic) {
            pgg::RunParams rp;
            rp.values = {{"outline", pgg::Value(u.outline.build())},
                         {"pitch", pgg::Value(static_cast<float>(u.pitch))},
                         {"y0", pgg::Value(static_cast<float>(u.y0))},
                         {"overhang", pgg::Value(static_cast<float>(u.overhang))},
                         {"roof_t", pgg::Value(static_cast<float>(u.roof_t))},
                         {"inner", pgg::Value(u.inner.build())},
                         {"holes", pgg::Value(u.holes.build())},
                         {"hole_margin", pgg::Value(static_cast<float>(u.inner_hole_margin))}};
            add_unit(res, params, "ceiling:" + v.id, "ceilings/attic.pgg", std::move(rp),
                     Placement{});
        }
    }

    if (!res.units.empty()) {
        std::vector<pgg::GeoPtr> parts;
        parts.reserve(res.units.size());
        for (const auto& u : res.units)
            if (u.mesh) parts.push_back(u.mesh);
        if (!parts.empty()) res.mesh = merge_meshes(parts);
    }
    return res;
}

namespace {

double quant(double v) { return std::round(v * 1e4) / 1e4; }

nlohmann::json quant_vec3(const glm::dvec3& p) {
    return nlohmann::json::array({quant(p.x), quant(p.y), quant(p.z)});
}

}  // namespace

nlohmann::json anchors_json(const btg::Ir& ir) {
    nlohmann::json anchors = nlohmann::json::array();
    for (const auto& a : ir.anchors) {
        anchors.push_back({{"kind", btg::anchor_kind_name(a.kind_code)},
                           {"kind_code", a.kind_code},
                           {"p", quant_vec3(a.p)},
                           {"dir", quant_vec3(a.dir)},
                           {"space", a.space}});
    }
    return {{"format", "building-geometry-generator-anchors/0"}, {"anchors", anchors}};
}

}  // namespace building_geometry_generator
