#include "../../pch.h"

// §8.9 geometry queries (v1.34). raycast(geo, target, dir, origin, max_dist)
// casts one ray per point of geo against the triangles of target (BVH) and
// stamps the answer on the points: @hit (bool), @hit_pos (vec3, point),
// @hit_n (vec3, normal — the geometric normal of the hit triangle, as wound),
// @hit_dist (f32), @hit_face (int, target face index). A miss keeps
// hit = false, hit_pos = origin, hit_n = 0, hit_dist = -1, hit_face = -1.

#include "builtins.h"
#include "parallel.h"
#include "sdf.h"

namespace pgg {
namespace {

Value opRaycast(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const GeoPtr target = asGeo(bound.values[1]);
    const float maxDist = asF32(bound.values[4]);
    const size_t n = in.pointCount();

    ConstBufferPtr dirBuf = bound.fields[2]
                                ? convertBuffer(evalField(bound.fields[2], in, Domain::Points, run), ScalarType::Vec3)
                                : makeConstBuffer(Value(glm::vec3(0.0f, -1.0f, 0.0f)), n);
    ConstBufferPtr originBuf =
        bound.fields[3] ? convertBuffer(evalField(bound.fields[3], in, Domain::Points, run), ScalarType::Vec3)
                        : nullptr;
    if (!dirBuf) return Value();
    const auto& dir = std::get<Vec3Buf>(*dirBuf);
    const Vec3Buf* origin = originBuf ? &std::get<Vec3Buf>(*originBuf) : nullptr;

    MeshBvh bvh;
    if (target) bvh.build(*target);
    if (!target || target->faceCount() == 0) {
        run.report("E603", bound.span, "raycast: target has no faces",
                   "pass a geo<mesh> with faces (select(..., b = empty_mesh()) turns every ray into a miss)");
    }

    std::vector<uint8_t> hit(n, 0);
    std::vector<glm::vec3> hitPos(n), hitN(n, glm::vec3(0.0f));
    std::vector<float> hitDist(n, -1.0f);
    std::vector<int64_t> hitFace(n, -1);
    parallelFor(n, run.threads, [&](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
            const glm::vec3 o = origin ? (*origin)[i] : (*in.positions)[i];
            hitPos[i] = o;
            const float len = glm::length(dir[i]);
            if (!(len > 1e-12f) || !std::isfinite(len)) continue;
            float t = 0.0f;
            glm::vec3 nrm(0.0f);
            int32_t face = -1;
            if (!bvh.raycast(o, dir[i] / len, maxDist, t, nrm, face)) continue;
            hit[i] = 1;
            hitPos[i] = o + dir[i] / len * t;
            hitN[i] = nrm;
            hitDist[i] = t;
            hitFace[i] = face;
        }
    });

    AttrSet attrs = in.pointAttrs ? *in.pointAttrs : AttrSet{};
    attrs.columns["hit"] = AttrColumn{ColumnData(std::make_shared<const std::vector<uint8_t>>(std::move(hit)))};
    attrs.columns["hit_pos"] =
        AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(std::move(hitPos))), AttrTypeInfo::Point};
    attrs.columns["hit_n"] =
        AttrColumn{ColumnData(std::make_shared<const std::vector<glm::vec3>>(std::move(hitN))), AttrTypeInfo::Normal};
    attrs.columns["hit_dist"] = AttrColumn{ColumnData(std::make_shared<const std::vector<float>>(std::move(hitDist)))};
    attrs.columns["hit_face"] =
        AttrColumn{ColumnData(std::make_shared<const std::vector<int64_t>>(std::move(hitFace)))};
    return Value(withAttrs(in, Domain::Points, std::make_shared<const AttrSet>(std::move(attrs))));
}

}  // namespace

Value evalQueryBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Raycast: return opRaycast(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
