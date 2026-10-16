#include "pch.h"

#include "village_geometry.h"

#include "pgg/pgg.h"  // formatDiagnostic

#include <glm/glm.hpp>

namespace village_geometry_generator {

uint64_t unit_seed(uint64_t seed, int64_t building_id) {
    uint64_t z = seed + 0x9e3779b97f4a7c15ull + static_cast<uint64_t>(building_id);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

namespace {

glm::vec3 rot_y(const glm::vec3& p, double c, double s) {
    return {static_cast<float>(p.x * c + p.z * s), p.y,
            static_cast<float>(-p.x * s + p.z * c)};
}

// Copy attr columns of one domain, transforming vec3 columns by tag.
pgg::AttrSet transform_attrs(const pgg::AttrSet* in, double c, double s, double tx, double tz) {
    pgg::AttrSet out;
    if (!in) return out;
    for (const auto& [name, col] : in->columns) {
        pgg::AttrColumn dst;
        dst.typeInfo = col.typeInfo;
        if (const auto* v3 = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&col.data)) {
            const bool affine = col.typeInfo == pgg::AttrTypeInfo::Point;
            const bool rotate = affine || col.typeInfo == pgg::AttrTypeInfo::Vector ||
                                col.typeInfo == pgg::AttrTypeInfo::Normal;
            if (rotate) {
                auto nv = std::make_shared<std::vector<glm::vec3>>();
                nv->reserve((*v3)->size());
                for (const auto& p : **v3) {
                    glm::vec3 q = rot_y(p, c, s);
                    if (affine) {
                        q.x += static_cast<float>(tx);
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

pgg::GeoPtr transform_geo(const pgg::GeoPtr& geo, double yaw_deg, double tx, double tz) {
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
            glm::vec3 q = rot_y(p, c, s);
            q.x += static_cast<float>(tx);
            q.z += static_cast<float>(tz);
            pos->push_back(q);
        }
    }
    out->positions = std::move(pos);

    if (geo->normals) {
        auto nrm = std::make_shared<std::vector<glm::vec3>>();
        nrm->reserve(geo->normals->size());
        for (const auto& n : *geo->normals) nrm->push_back(rot_y(n, c, s));
        out->normals = std::move(nrm);
    }

    out->cornerVerts = geo->cornerVerts;
    out->faceOffsets = geo->faceOffsets;
    out->pointAttrs = std::make_shared<const pgg::AttrSet>(
        transform_attrs(geo->pointAttrs.get(), c, s, tx, tz));
    out->cornerAttrs = std::make_shared<const pgg::AttrSet>(
        transform_attrs(geo->cornerAttrs.get(), c, s, tx, tz));
    out->faceAttrs = std::make_shared<const pgg::AttrSet>(
        transform_attrs(geo->faceAttrs.get(), c, s, tx, tz));
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

struct ParcelInfo {
    double w = 8.0;
    double d = 10.0;
};

ParcelInfo parcel_dims(const nlohmann::json& ir, int parcel_id) {
    ParcelInfo out;
    for (const auto& p : ir.value("parcels", nlohmann::json::array())) {
        if (p.value("id", -1) != parcel_id) continue;
        const auto& poly = p.value("polygon", nlohmann::json::array());
        if (poly.size() >= 3) {
            const auto d10x = poly[1][0].get<double>() - poly[0][0].get<double>();
            const auto d10y = poly[1][1].get<double>() - poly[0][1].get<double>();
            const auto d21x = poly[2][0].get<double>() - poly[1][0].get<double>();
            const auto d21y = poly[2][1].get<double>() - poly[1][1].get<double>();
            out.w = std::sqrt(d10x * d10x + d10y * d10y);
            out.d = std::sqrt(d21x * d21x + d21y * d21y);
        }
        return out;
    }
    return out;
}

}  // namespace

GeomResult generate_geometry(const nlohmann::json& ir, const GeomParams& params) {
    GeomResult result;
    const auto buildings = ir.value("buildings", nlohmann::json::array());
    result.buildings_total = static_cast<int>(buildings.size());

    std::vector<pgg::GeoPtr> placed;
    for (const auto& b : buildings) {
        const int id = b.value("id", -1);
        const std::string type = b.value("type", "house");
        const int parcel = b.value("parcel", -1);
        const auto pos = b.value("position", nlohmann::json::array({0.0, 0.0}));
        const auto facing = b.value("facing", nlohmann::json::array({0.0, 1.0}));

        std::string asset = params.assets_dir + "/" + type + ".pgg";
        {
            std::error_code ec;
            if (!std::filesystem::exists(asset, ec)) {
                result.diagnostics.push_back("no asset for type '" + type + "', using house.pgg");
                asset = params.assets_dir + "/house.pgg";
            }
        }

        const ParcelInfo dims = parcel_dims(ir, parcel);
        pgg::RunParams rp;
        rp.values = {{"w", pgg::Value(static_cast<float>(dims.w))},
                     {"d", pgg::Value(static_cast<float>(dims.d))},
                     {"rng_seed", pgg::Value(static_cast<int64_t>(unit_seed(params.seed, id)))}};
        rp.importRoots = params.import_roots;

        pgg::RunResult rr = pgg::runFile(asset, rp, {"mesh"});
        if (rr.hasErrors()) {
            for (const auto& d : rr.diagnostics) {
                result.diagnostics.push_back(pgg::formatDiagnostic(d, asset));
            }
            continue;
        }
        if (rr.outputs.empty()) {
            result.diagnostics.push_back(asset + ": no 'mesh' output");
            continue;
        }
        const pgg::GeoPtr g = pgg::asGeo(rr.outputs.front().value);
        if (!g || g->kind != pgg::GeoKind::Mesh || g->pointCount() == 0) {
            result.diagnostics.push_back(asset + ": 'mesh' output is not a non-empty mesh");
            continue;
        }

        // Map meters (x right, y down) -> world (x, 0, z); facade +Z -> facing.
        const double mx = pos[0].get<double>();
        const double mz = pos[1].get<double>();
        const double fx = facing[0].get<double>();
        const double fz = facing[1].get<double>();
        const double yaw_deg = std::atan2(fx, fz) * 180.0 / 3.14159265358979323846;
        placed.push_back(transform_geo(g, yaw_deg, mx, mz));
        ++result.buildings_built;
    }

    if (!placed.empty()) {
        result.mesh = merge_meshes(placed);
    }
    return result;
}

namespace {

// b_<type> def name, or nullptr when the type has no factory (caller falls
// back to b_house, mirroring generate_geometry).
const char* scene_def_for(const std::string& type) {
    static const char* kKnown[] = {"house", "bakery", "tavern", "mill", "well"};
    for (const char* k : kKnown) {
        if (type == k) return k;
    }
    return nullptr;
}

void append_building_lines(const nlohmann::json& ir, uint64_t seed, std::ostringstream& out,
                           std::vector<std::string>& placed_names) {
    char num[64];
    for (const auto& b : ir.value("buildings", nlohmann::json::array())) {
        const int id = b.value("id", -1);
        const std::string type = b.value("type", "house");
        const ParcelInfo dims = parcel_dims(ir, b.value("parcel", -1));
        const auto pos = b.value("position", nlohmann::json::array({0.0, 0.0}));
        const auto facing = b.value("facing", nlohmann::json::array({0.0, 1.0}));
        const double yaw_deg = std::atan2(facing[0].get<double>(), facing[1].get<double>()) * 180.0 /
                               3.14159265358979323846;

        const char* def = scene_def_for(type);
        if (!def) def = "house";
        std::snprintf(num, sizeof(num), "%.9g", dims.w);
        const std::string w = num;
        std::snprintf(num, sizeof(num), "%.9g", dims.d);
        const std::string d = num;
        out << "b" << id << " = vb.b_" << def << "(w = " << w << ", d = " << d
            << ", rng = rng_from_seed(" << static_cast<int64_t>(unit_seed(seed, id)) << "))\n";
        std::snprintf(num, sizeof(num), "%.9g", yaw_deg);
        const std::string yaw = num;
        std::snprintf(num, sizeof(num), "%.9g", pos[0].get<double>());
        const std::string mx = num;
        std::snprintf(num, sizeof(num), "%.9g", pos[1].get<double>());
        const std::string mz = num;
        out << "w" << id << " = transform(b" << id << ", rotate = vec3(0, " << yaw
            << ", 0), translate = vec3(" << mx << ", 0, " << mz << "))\n";
        placed_names.push_back("w" + std::to_string(id));
    }
}

}  // namespace

std::string emit_scene_pgg(const nlohmann::json& ir, uint64_t seed) {
    std::ostringstream out;
    out << "# Generated by VillageGeometryGeneratorCli --emit-pgg from a Layout IR.\n"
           "# Do not edit: regenerate from the IR. Import root: content/pgg.\n"
           "import Village.buildings_lib as vb\n\n";
    std::vector<std::string> placed;
    append_building_lines(ir, seed, out, placed);
    if (placed.empty()) return {};
    out << "\nmesh = merge(";
    for (size_t i = 0; i < placed.size(); ++i) {
        if (i) out << ", ";
        out << placed[i];
    }
    out << ")\noutput mesh\n";
    return out.str();
}

}  // namespace village_geometry_generator
