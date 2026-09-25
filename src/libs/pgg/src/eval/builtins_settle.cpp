#include "../../pch.h"

// §8.14 rigid_settle (v1.37): one-shot rest-pose bake. Each connected island
// of the input mesh is a dynamic convex hull; the optional static mesh is a
// triangle soup of infinite mass and is not part of the output. Jolt runs on
// one thread for exactly `steps` fixed timesteps, so the result does not
// depend on the engine thread pool (N1). steps = 0 and an empty mesh return
// the input without starting the solver.

#include "builtins.h"
#include "fracture.h"
#include "geometry.h"

#include <mutex>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace pgg {
namespace {

constexpr float kConvexRadius = 0.002f;

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
}  // namespace Layers

class BpLayers final : public JPH::BroadPhaseLayerInterface {
public:
    BpLayers() {
        map_[Layers::kStatic] = JPH::BroadPhaseLayer(0);
        map_[Layers::kMoving] = JPH::BroadPhaseLayer(1);
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return map_[layer]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "bp"; }
#endif
private:
    JPH::BroadPhaseLayer map_[2]{};
};

class ObjVsBp final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        if (layer == Layers::kStatic) return bp == JPH::BroadPhaseLayer(1);
        return true;
    }
};

class ObjVsObj final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::kStatic) return b == Layers::kMoving;
        return true;
    }
};

void ensureJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

bool finite3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

glm::quat toGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

glm::vec3 rotateVec(const glm::quat& r, const glm::vec3& v) { return r * v; }

struct IslandPose {
    glm::vec3 com{0.0f};
    glm::vec3 outCom{0.0f};
    glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
};

glm::vec3 movedPoint(const glm::vec3& p, const IslandPose& pose) {
    return pose.rot * (p - pose.com) + pose.outCom;
}

AttrSet transformAttrs(const AttrSet* src, Domain domain, const Geo& geo, const std::vector<int>& pointIsland,
                       const std::vector<IslandPose>& poses) {
    AttrSet out;
    if (!src) return out;
    const int32_t* corners = geo.cornerVerts ? geo.cornerVerts->data() : nullptr;
    for (const auto& [name, col] : src->columns) {
        AttrColumn next = col;
        const bool vec3 = std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(col.data);
        const bool vec4 = std::holds_alternative<std::shared_ptr<const std::vector<glm::vec4>>>(col.data);
        if (col.typeInfo == AttrTypeInfo::None || (!vec3 && !vec4)) {
            out.columns.emplace(name, std::move(next));
            continue;
        }
        auto islandOf = [&](size_t i) -> int {
            if (domain == Domain::Points) return i < pointIsland.size() ? pointIsland[i] : -1;
            if (domain == Domain::Corners && corners && i < geo.cornerCount()) {
                const int32_t p = corners[i];
                return p >= 0 && static_cast<size_t>(p) < pointIsland.size() ? pointIsland[static_cast<size_t>(p)] : -1;
            }
            return -1;
        };
        if (col.typeInfo == AttrTypeInfo::Quaternion && vec4) {
            const auto& in = *std::get<std::shared_ptr<const std::vector<glm::vec4>>>(col.data);
            std::vector<glm::vec4> buf = in;
            for (size_t i = 0; i < buf.size(); ++i) {
                const int id = islandOf(i);
                if (id < 0) continue;
                const glm::quat q = poses[static_cast<size_t>(id)].rot *
                                    glm::quat(buf[i].w, buf[i].x, buf[i].y, buf[i].z);
                buf[i] = glm::vec4(q.x, q.y, q.z, q.w);
            }
            next.data = std::make_shared<const std::vector<glm::vec4>>(std::move(buf));
        } else if (vec3) {
            const auto& in = *std::get<std::shared_ptr<const std::vector<glm::vec3>>>(col.data);
            std::vector<glm::vec3> buf = in;
            for (size_t i = 0; i < buf.size(); ++i) {
                const int id = islandOf(i);
                if (id < 0) continue;
                const IslandPose& pose = poses[static_cast<size_t>(id)];
                if (col.typeInfo == AttrTypeInfo::Point) buf[i] = movedPoint(buf[i], pose);
                else if (col.typeInfo == AttrTypeInfo::Normal) {
                    const glm::vec3 rn = rotateVec(pose.rot, buf[i]);
                    const float len = glm::length(rn);
                    buf[i] = len > 0.0f ? rn / len : glm::vec3(0.0f);
                } else {
                    buf[i] = rotateVec(pose.rot, buf[i]);
                }
            }
            next.data = std::make_shared<const std::vector<glm::vec3>>(std::move(buf));
        }
        out.columns.emplace(name, std::move(next));
    }
    return out;
}

float islandVolume(const std::vector<glm::vec3>& pts, const std::vector<int>& ids, int island) {
    glm::vec3 c(0.0f);
    int n = 0;
    for (size_t i = 0; i < pts.size(); ++i)
        if (ids[i] == island) {
            c += pts[i];
            n += 1;
        }
    if (n < 4) return 0.0f;
    c /= static_cast<float>(n);
    float score = 0.0f;
    glm::vec3 a(0.0f);
    bool haveA = false;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (ids[i] != island) continue;
        const glm::vec3 d = pts[i] - c;
        if (!haveA) {
            if (glm::dot(d, d) > 1e-12f) {
                a = d;
                haveA = true;
            }
            continue;
        }
        const glm::vec3 cr = glm::cross(a, d);
        for (size_t j = i + 1; j < pts.size(); ++j) {
            if (ids[j] != island) continue;
            score = std::max(score, std::abs(glm::dot(cr, pts[j] - c)));
        }
    }
    return score;
}

JPH::RefConst<JPH::Shape> hullOf(const std::vector<glm::vec3>& pts, const std::vector<int>& ids, int island,
                                 std::string& error) {
    if (islandVolume(pts, ids, island) < 1e-6f) {
        error = "degenerate";
        return nullptr;
    }
    JPH::Array<JPH::Vec3> local;
    for (size_t i = 0; i < pts.size(); ++i)
        if (ids[i] == island) local.push_back(JPH::Vec3(pts[i].x, pts[i].y, pts[i].z));
    JPH::ConvexHullShapeSettings settings(local, kConvexRadius);
    const JPH::ShapeSettings::ShapeResult built = settings.Create();
    if (built.HasError()) {
        error = built.GetError();
        return nullptr;
    }
    return built.Get();
}

JPH::RefConst<JPH::Shape> staticMeshOf(const Geo& geo) {
    JPH::TriangleList tris;
    if (!geo.positions || !geo.cornerVerts || !geo.faceOffsets) return nullptr;
    const auto& pos = *geo.positions;
    const auto& corners = *geo.cornerVerts;
    const auto& offs = *geo.faceOffsets;
    for (size_t f = 0; f + 1 < offs.size(); ++f) {
        const int a = offs[f];
        const int b = offs[f + 1];
        if (b - a < 3) continue;
        const auto at = [&](int c) {
            const glm::vec3& p = pos[static_cast<size_t>(corners[c])];
            return JPH::Vec3(p.x, p.y, p.z);
        };
        for (int c = a + 1; c + 1 < b; ++c) tris.push_back(JPH::Triangle(at(a), at(c), at(c + 1)));
    }
    if (tris.empty()) return nullptr;
    JPH::MeshShapeSettings settings(tris);
    const JPH::ShapeSettings::ShapeResult built = settings.Create();
    if (built.HasError()) return nullptr;
    return built.Get();
}

}  // namespace

Value evalSettleBuiltin(const BoundCall& bound, RunContext& run) {
    const Value& in = bound.values[0];
    if (isNone(in) || !std::holds_alternative<GeoPtr>(in.data)) {
        run.report("E204", bound.span, "rigid_settle expects a geo<mesh>", "pass a mesh; realize() instances first");
        return in;
    }
    const Geo& geo = *asGeo(in);
    if (geo.kind != GeoKind::Mesh) {
        run.report("E204", bound.span, "rigid_settle expects a geo<mesh>", "realize() first, or pass anchor points as a mesh");
        return in;
    }
    const int64_t steps = bound.values.size() > 2 && !isNone(bound.values[2]) ? asInt(bound.values[2]) : 180;
    const float dt = bound.values.size() > 3 && !isNone(bound.values[3]) ? asF32(bound.values[3]) : (1.0f / 60.0f);
    const glm::vec3 gravity = bound.values.size() > 4 && !isNone(bound.values[4]) ? asVec3(bound.values[4])
                                                                                   : glm::vec3(0.0f, -9.81f, 0.0f);
    const float friction = bound.values.size() > 5 && !isNone(bound.values[5]) ? asF32(bound.values[5]) : 0.6f;
    const float restitution = bound.values.size() > 6 && !isNone(bound.values[6]) ? asF32(bound.values[6]) : 0.05f;
    auto fail = [&](const std::string& msg) {
        run.report("E613", bound.span, msg, "steps >= 0, dt > 0, finite parameters, and a volume per island");
        return in;
    };
    if (steps < 0) return fail("rigid_settle: steps must be >= 0");
    if (!(dt > 0.0f) || !std::isfinite(dt)) return fail("rigid_settle: dt must be > 0");
    if (!finite3(gravity) || !std::isfinite(friction) || !std::isfinite(restitution))
        return fail("rigid_settle: gravity, friction and restitution must be finite");
    if (steps == 0 || geo.pointCount() == 0 || geo.faceCount() == 0) return in;

    size_t islandCount = 0;
    const std::vector<int32_t> faceIsland = computeIslands(geo, islandCount);
    if (islandCount == 0) return in;
    std::vector<int> pointIsland(geo.pointCount(), -1);
    const auto& corners = *geo.cornerVerts;
    const auto& offs = *geo.faceOffsets;
    for (size_t f = 0; f < geo.faceCount(); ++f) {
        for (int c = offs[f]; c < offs[f + 1]; ++c) {
            const int32_t p = corners[static_cast<size_t>(c)];
            if (p >= 0 && static_cast<size_t>(p) < pointIsland.size())
                pointIsland[static_cast<size_t>(p)] = faceIsland[f];
        }
    }

    const Geo* ground = nullptr;
    if (bound.values.size() > 1 && !isNone(bound.values[1])) {
        if (!std::holds_alternative<GeoPtr>(bound.values[1].data) || asGeo(bound.values[1])->kind != GeoKind::Mesh) {
            run.report("E204", bound.span, "rigid_settle: static must be a geo<mesh> or none", "pass a mesh collider or omit static");
            return in;
        }
        ground = asGeo(bound.values[1]).get();
    }

    ensureJolt();
    const auto& pos = *geo.positions;
    std::vector<JPH::RefConst<JPH::Shape>> hulls(islandCount);
    std::vector<IslandPose> poses(islandCount);
    for (size_t i = 0; i < islandCount; ++i) {
        std::string err;
        hulls[i] = hullOf(pos, pointIsland, static_cast<int>(i), err);
        if (!hulls[i]) return fail("rigid_settle: island " + std::to_string(i) + " has no volume" + (err.empty() ? "" : " (" + err + ")"));
        const JPH::Vec3 com = hulls[i]->GetCenterOfMass();
        poses[i].com = glm::vec3(com.GetX(), com.GetY(), com.GetZ());
        poses[i].outCom = poses[i].com;
    }

    BpLayers bp;
    ObjVsBp ovb;
    ObjVsObj pairs;
    const JPH::uint nBodies = static_cast<JPH::uint>(islandCount + 2);
    JPH::PhysicsSystem physics;
    physics.Init(nBodies, 0, nBodies * nBodies, nBodies * nBodies, bp, ovb, pairs);
    physics.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    JPH::BodyInterface& bodies = physics.GetBodyInterface();
    std::vector<JPH::BodyID> ids(islandCount);
    for (size_t i = 0; i < islandCount; ++i) {
        // Hull points are already in world space, and mPosition is the shape origin,
        // not the center of mass. The shape's own COM offset places the body.
        JPH::BodyCreationSettings settings(hulls[i], JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                           JPH::EMotionType::Dynamic, Layers::kMoving);
        settings.mFriction = friction;
        settings.mRestitution = restitution;
        ids[i] = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }
    if (ground) {
        if (JPH::RefConst<JPH::Shape> shape = staticMeshOf(*ground)) {
            JPH::BodyCreationSettings settings(shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                               JPH::EMotionType::Static, Layers::kStatic);
            settings.mFriction = friction;
            settings.mRestitution = restitution;
            bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        }
    }
    physics.OptimizeBroadPhase();

    JPH::TempAllocatorImpl temp(16 * 1024 * 1024);
    JPH::JobSystemSingleThreaded jobs(JPH::cMaxPhysicsJobs);
    for (int64_t s = 0; s < steps; ++s) physics.Update(dt, 1, &temp, &jobs);

    for (size_t i = 0; i < islandCount; ++i) {
        const JPH::RVec3 p = bodies.GetCenterOfMassPosition(ids[i]);
        poses[i].outCom = glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
        poses[i].rot = toGlm(bodies.GetRotation(ids[i]));
    }

    std::vector<glm::vec3> moved(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        const int id = pointIsland[i];
        moved[i] = id < 0 ? pos[i] : movedPoint(pos[i], poses[static_cast<size_t>(id)]);
    }
    GeoPtr out = withPositions(geo, std::move(moved));
    if (geo.normals) {
        std::vector<glm::vec3> normals = *geo.normals;
        for (size_t i = 0; i < normals.size() && i < pointIsland.size(); ++i) {
            const int id = pointIsland[i];
            if (id < 0) continue;
            const glm::vec3 rn = rotateVec(poses[static_cast<size_t>(id)].rot, normals[i]);
            const float len = glm::length(rn);
            normals[i] = len > 0.0f ? rn / len : glm::vec3(0.0f);
        }
        out = withNormals(*out, std::move(normals));
    }
    out = withAttrs(*out, Domain::Points, std::make_shared<const AttrSet>(transformAttrs(geo.pointAttrs.get(), Domain::Points, geo, pointIsland, poses)));
    out = withAttrs(*out, Domain::Corners, std::make_shared<const AttrSet>(transformAttrs(geo.cornerAttrs.get(), Domain::Corners, geo, pointIsland, poses)));
    return Value(out);
}

std::vector<glm::vec3> faceNormals(const Geo& geo, const std::vector<glm::vec3>& pos) {
    std::vector<glm::vec3> n(pos.size(), glm::vec3(0.0f));
    if (!geo.cornerVerts || !geo.faceOffsets) return n;
    const auto& corners = *geo.cornerVerts;
    const auto& offs = *geo.faceOffsets;
    for (size_t f = 0; f + 1 < offs.size(); ++f) {
        const int a = offs[f];
        const int b = offs[f + 1];
        if (b - a < 3) continue;
        const glm::vec3 p0 = pos[static_cast<size_t>(corners[a])];
        for (int c = a + 1; c + 1 < b; ++c) {
            const glm::vec3 fn = glm::cross(pos[static_cast<size_t>(corners[c])] - p0,
                                            pos[static_cast<size_t>(corners[c + 1])] - p0);
            n[static_cast<size_t>(corners[a])] += fn;
            n[static_cast<size_t>(corners[c])] += fn;
            n[static_cast<size_t>(corners[c + 1])] += fn;
        }
    }
    for (glm::vec3& v : n) {
        const float len = glm::length(v);
        v = len > 0.0f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return n;
}

Value evalClothBuiltin(const BoundCall& bound, RunContext& run) {
    const Value& in = bound.values[0];
    if (isNone(in) || !std::holds_alternative<GeoPtr>(in.data) || asGeo(in)->kind != GeoKind::Mesh) {
        run.report("E204", bound.span, "cloth_drape expects a geo<mesh> cloth", "pass a mesh; a grid is the usual sheet");
        return in;
    }
    const Geo& cloth = *asGeo(in);
    if (bound.values.size() < 2 || isNone(bound.values[1]) || !std::holds_alternative<GeoPtr>(bound.values[1].data) ||
        asGeo(bound.values[1])->kind != GeoKind::Mesh) {
        run.report("E204", bound.span, "cloth_drape expects a geo<mesh> collider", "pass the settled pile, or another mesh");
        return in;
    }
    const Geo& collider = *asGeo(bound.values[1]);
    const int64_t steps = bound.values.size() > 2 && !isNone(bound.values[2]) ? asInt(bound.values[2]) : 180;
    const float dt = bound.values.size() > 3 && !isNone(bound.values[3]) ? asF32(bound.values[3]) : (1.0f / 60.0f);
    const glm::vec3 gravity = bound.values.size() > 4 && !isNone(bound.values[4]) ? asVec3(bound.values[4])
                                                                                   : glm::vec3(0.0f, -9.81f, 0.0f);
    const float friction = bound.values.size() > 5 && !isNone(bound.values[5]) ? asF32(bound.values[5]) : 0.8f;
    auto fail = [&](const std::string& msg) {
        run.report("E614", bound.span, msg, "steps >= 0, dt > 0, finite parameters, and a cloth sheet with area");
        return in;
    };
    if (steps < 0) return fail("cloth_drape: steps must be >= 0");
    if (!(dt > 0.0f) || !std::isfinite(dt)) return fail("cloth_drape: dt must be > 0");
    if (!finite3(gravity) || !std::isfinite(friction)) return fail("cloth_drape: gravity and friction must be finite");
    if (steps == 0 || cloth.pointCount() == 0) return in;
    if (cloth.faceCount() == 0) return fail("cloth_drape: cloth has no area");

    ensureJolt();
    JPH::Ref<JPH::SoftBodySharedSettings> settings = new JPH::SoftBodySharedSettings;
    const auto& pos = *cloth.positions;
    settings->mVertices.reserve(static_cast<JPH::uint>(pos.size()));
    for (const glm::vec3& p : pos) {
        JPH::SoftBodySharedSettings::Vertex v;
        v.mPosition = JPH::Float3(p.x, p.y, p.z);
        v.mInvMass = 1.0f;
        settings->mVertices.push_back(v);
    }
    const auto& corners = *cloth.cornerVerts;
    const auto& offs = *cloth.faceOffsets;
    int faces = 0;
    for (size_t f = 0; f + 1 < offs.size(); ++f) {
        const int a = offs[f];
        const int b = offs[f + 1];
        if (b - a < 3) continue;
        for (int c = a + 1; c + 1 < b; ++c) {
            const uint32_t i0 = static_cast<uint32_t>(corners[a]);
            const uint32_t i1 = static_cast<uint32_t>(corners[c]);
            const uint32_t i2 = static_cast<uint32_t>(corners[c + 1]);
            if (i0 == i1 || i0 == i2 || i1 == i2) continue;
            settings->AddFace(JPH::SoftBodySharedSettings::Face(i0, i1, i2));
            faces += 1;
        }
    }
    if (faces == 0) return fail("cloth_drape: cloth has no area");
    // Shear at the flat rest length locks each quad into a plate. Leave it off
    // so the sheet can fold; a soft dihedral bend then makes wrinkles instead
    // of pulling the cloth back to that flat rest angle.
    // Shear and dihedral bend stay off (FLT_MAX). A flat rest angle pulls the
    // sheet back to a plate; folds come from extra edge length in the input.
    const JPH::SoftBodySharedSettings::VertexAttributes attr(1.0e-5f, FLT_MAX, FLT_MAX);
    settings->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Dihedral);
    settings->Optimize();

    size_t islandCount = 0;
    const std::vector<int32_t> faceIsland = computeIslands(collider, islandCount);
    if (islandCount == 0) return fail("cloth_drape: collider has no triangles");
    std::vector<int> pointIsland(collider.pointCount(), -1);
    const auto& colCorners = *collider.cornerVerts;
    const auto& colOffs = *collider.faceOffsets;
    const auto& colPos = *collider.positions;
    for (size_t f = 0; f < collider.faceCount(); ++f) {
        for (int c = colOffs[f]; c < colOffs[f + 1]; ++c) {
            const int32_t p = colCorners[static_cast<size_t>(c)];
            if (p >= 0 && static_cast<size_t>(p) < pointIsland.size())
                pointIsland[static_cast<size_t>(p)] = faceIsland[f];
        }
    }

    BpLayers bp;
    ObjVsBp ovb;
    ObjVsObj pairs;
    const JPH::uint nBodies = static_cast<JPH::uint>(islandCount + 4);
    JPH::PhysicsSystem physics;
    physics.Init(nBodies, 0, 16384, 16384, bp, ovb, pairs);
    physics.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    JPH::BodyInterface& bodies = physics.GetBodyInterface();
    int hulls = 0;
    for (size_t i = 0; i < islandCount; ++i) {
        std::string err;
        JPH::RefConst<JPH::Shape> hull = hullOf(colPos, pointIsland, static_cast<int>(i), err);
        if (!hull) continue;
        // Hull points are already in world space; mPosition is the shape origin.
        JPH::BodyCreationSettings ground(hull, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                         JPH::EMotionType::Static, Layers::kStatic);
        ground.mFriction = friction;
        bodies.CreateAndAddBody(ground, JPH::EActivation::DontActivate);
        hulls += 1;
    }
    if (hulls == 0) return fail("cloth_drape: collider has no volume");

    JPH::SoftBodyCreationSettings clothSettings(settings, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), Layers::kMoving);
    clothSettings.mFriction = friction;
    clothSettings.mRestitution = 0.0f;
    clothSettings.mLinearDamping = 0.15f;
    clothSettings.mVertexRadius = 0.008f;
    clothSettings.mNumIterations = 10;
    clothSettings.mPressure = 0.0f;
    const JPH::BodyID clothId = bodies.CreateAndAddSoftBody(clothSettings, JPH::EActivation::Activate);
    physics.OptimizeBroadPhase();

    JPH::TempAllocatorImpl temp(32 * 1024 * 1024);
    JPH::JobSystemSingleThreaded jobs(JPH::cMaxPhysicsJobs);
    for (int64_t s = 0; s < steps; ++s) physics.Update(dt, 1, &temp, &jobs);

    std::vector<glm::vec3> moved(pos.size());
    {
        JPH::BodyLockRead lock(physics.GetBodyLockInterface(), clothId);
        if (!lock.Succeeded() || !lock.GetBody().IsSoftBody()) return fail("cloth_drape: solver did not keep the cloth");
        const JPH::Body& body = lock.GetBody();
        const auto* mp = static_cast<const JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
        const JPH::RMat44 com = body.GetCenterOfMassTransform();
        const JPH::Array<JPH::SoftBodyVertex>& verts = mp->GetVertices();
        if (verts.size() != pos.size()) return fail("cloth_drape: solver changed the vertex count");
        for (size_t i = 0; i < pos.size(); ++i) {
            const JPH::RVec3 w = com * verts[i].mPosition;
            moved[i] = glm::vec3(static_cast<float>(w.GetX()), static_cast<float>(w.GetY()), static_cast<float>(w.GetZ()));
        }
    }
    GeoPtr out = withPositions(cloth, std::move(moved));
    if (cloth.normals) out = withNormals(*out, faceNormals(cloth, *out->positions));
    return Value(out);
}

}  // namespace pgg
