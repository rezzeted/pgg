// Geometry preview (spec §9 L3 groundwork): renders the value of a selected
// node — geo<mesh>, geo<points>, geo<instances> (realized) or sdf (meshed at
// a preview voxel) — into an 8x-MSAA offscreen sokol target (falls back to 4x
// then 1x; resolved to a 1-sample texture) shown in the docked preview pane
// below the graph, with an orbit camera.
// The value comes from a RunParams::pulls run (any binding, not only declared
// outputs). Optional highlight of one group.
//
    // Frame contract: CPU buildPreviewGeometry; render() OUTSIDE any sokol
    // swapchain pass. The ImGui pane lives in PggViewer (PreviewPane).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <sokol_gfx.h>

#include <pgg/src/eval/value.h>

struct PreviewVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;  // albedo: @Cd (linear rgb) or the neutral base grey
    float mask;       // 1 = in the highlighted group
};

struct PreviewGeometry {
    std::vector<PreviewVertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 bmin{0.0f}, bmax{0.0f};
    std::string summary;                 // "mesh 12508 pts, 25004 tri" / "sdf -> mesh ..." / error text
    std::vector<std::string> groups;     // highlightable group names ("<domain>:<name>")
    // Per-group bounding boxes (A2 camera targeting), same keys as `groups`
    // (groups with no elements are absent). Points groups bound their points,
    // face groups bound the points of their faces.
    std::map<std::string, std::pair<glm::vec3, glm::vec3>> groupBBoxes;
    // Edge line-list for the wire overlay (meshes only): deduplicated
    // undirected edges over the source point positions (shared, not copied).
    std::shared_ptr<const std::vector<glm::vec3>> wirePositions;
    std::vector<uint32_t> wireIndices;   // pairs into wirePositions
    bool hasColor = false;               // a vec3 @Cd column was found and baked into the vertices
    bool ok = false;
};

// How mesh normals are chosen for shading.
//   Auto   — corner attribute N (compute_normals(mode = flat)) if present, else
//            point @N (smooth), else face normals.
//   Smooth — point @N if present, else face normals.
//   Flat   — always face normals (ignores @N; architecture built from welded
//            boxes reads as faceted instead of "pillowed").
enum class PreviewShading { Auto, Smooth, Flat };

// Projection of the preview camera (A2). The ortho modes snap the orbit to a
// fixed axis view (front = camera on +Z, side = on +X, top = above on +Y) and
// use an orthographic projection fit to the active radius; the user can still
// orbit afterwards (the view matrix is shared, only the projection differs).
enum class PreviewProjection { Perspective, OrthoFront, OrthoSide, OrthoTop };

// What a refit (setGeometry(refit = true)) and the Fit button aim at (A2):
// the whole geometry or the explicit target set via setTarget().
enum class PreviewFitMode { All, Target };

struct PreviewBuildOptions {
    std::string highlightGroup;  // "<domain>:<name>" from PreviewGeometry::groups, "" = none
    PreviewShading shading = PreviewShading::Auto;
    // Albedo from the vec3 attribute @Cd (spec §4.3: surface color, any domain;
    // faces/corners unweld the mesh so face colors stay crisp). false = neutral grey.
    bool vertexColors = true;
    int sdfResolution = 64;      // longest bbox axis in voxels for sdf values
    unsigned threads = 0;
};

// Converts a runtime value into render-ready triangles (CPU only, no sokol).
PreviewGeometry buildPreviewGeometry(const pgg::Value& value, const PreviewBuildOptions& opts);

class GeometryPreview {
public:
    void init();
    void shutdown();

    // Uploads new geometry and refits the camera (keeps the orbit angles).
    void setGeometry(const PreviewGeometry& geo, bool refit);
    void clear();
    bool hasGeometry() const { return m_indexCount > 0; }
    // Orbit camera from the CLI (--preview-orbit): angles in degrees, zoom is a
    // multiplier on the fit distance (1 = Fit, <1 closer). Applied on every
    // refit until the user orbits by hand.
    void setOrbit(float yawDeg, float pitchDeg, float zoom);
    // Zoom alone (F2, RPC render "zoom" — alias of setOrbit's third
    // component): the fit-distance multiplier, 1 = fit the target, 0.5 =
    // twice closer, 3 = three times farther. Survives refits like setOrbit.
    void setZoom(float zoom);
    // Absolute orbit distance in meters from the orbit center (F2, RPC render
    // "distance"); converted to the equivalent fit-zoom so refits keep it.
    void setDistance(float meters);

    // Aims the orbit at an explicit target (A2): the target is remembered, so
    // with fit mode Target the refits and the Fit button return to it instead
    // of the whole-scene fit. radius is the target's fit radius (group bbox
    // extent); distance defaults to the usual fit distance (radius-scaled).
    void setTarget(const glm::vec3& center, float radius, std::optional<float> distance = std::nullopt);
    // Turns the orbit yaw so the camera sits on the target's side of the scene
    // (A2): with a facade-side target and the default yaw the eye ended up
    // inside the model, looking at the target through the back wall. No-op
    // without a target, in the ortho views, or when the target is (nearly) at
    // the scene centre — then no side is "outside" and the yaw is kept.
    void faceTargetFromOutside();
    void setFitMode(PreviewFitMode mode) { m_fitMode = mode; }
    PreviewFitMode fitMode() const { return m_fitMode; }
    bool hasTarget() const { return m_hasTarget; }
    // Applies the current fit mode now (the Fit button): the remembered target
    // when fit mode is Target and one was set, otherwise the whole scene.
    void fit();
    // Switches the projection (A2). The ortho modes also snap yaw/pitch to the
    // axis preset (front/side/top); Perspective keeps the current orbit.
    void setProjection(PreviewProjection p);
    PreviewProjection projection() const { return m_projection; }
    // Wire overlay (A2): mesh edges drawn as dark lines over the shading.
    void setWireframe(bool on) { m_wireframe = on; }
    bool wireframe() const { return m_wireframe; }

    // Read-only camera state (RPC reporting / smoke checks).
    glm::vec3 center() const { return m_center; }
    float fitRadius() const { return m_radius; }  // radius of the active fit (scene or target)
    float distance() const { return m_distance; }
    float fitZoom() const { return m_fitZoom; }  // fit-distance multiplier (1 = fit)
    glm::vec3 sceneCenter() const { return m_sceneCenter; }
    float sceneRadius() const { return m_sceneRadius; }
    // Orbit angles in degrees (the F3 frame key — the effective camera state).
    float yawDeg() const { return glm::degrees(m_yaw); }
    float pitchDeg() const { return glm::degrees(m_pitch); }

    // Offscreen pass clear color (linear 0..1) — the known exact background of
    // preview captures; the F3 silhouette metrics take it as the model bg.
    static constexpr float kClearColor[4] = {0.14f, 0.15f, 0.18f, 1.0f};
    static constexpr int kMaxTarget = 4096;

    // Size the offscreen color target (clamped to kMaxTarget). Returns the
    // actual size; sizeClamped is true when the request was reduced.
    void ensureTarget(int w, int h);
    int targetWidth() const { return m_targetW; }
    int targetHeight() const { return m_targetH; }
    bool targetSizeClamped() const { return m_sizeClamped; }
    // 1-sample color image used for sampling and FBO readback (resolve when
    // MSAA is on, otherwise the color attachment itself).
    sg_image resolvedColorImage() const { return m_sampleCount > 1 ? m_resolve : m_color; }
    sg_view texView() const { return m_texView; }

    // Viewer orbit/pan (ImGui pane); Serve does not call these.
    void nudgeOrbit(float dyaw, float dpitch);
    void nudgePan(float dx, float dy);
    void nudgeDistanceWheel(float wheel);

    // Offscreen pass. No-op without a target.
    void render();

    const std::string& summary() const { return m_summary; }
    void setSummary(const std::string& s) { m_summary = s; }
    const std::string& error() const { return m_error; }
    void setError(const std::string& s) { m_error = s; }

    // view * proj of the current camera state; public for headless checks
    // (pure math — the depth-range backend flag is cached by init()).
    glm::mat4 viewProj(float aspect) const;

private:
    struct VsParams {
        float mvp[16];
    };
    struct FsParams {
        float lightDir[4];   // key light (camera frame, upper left)
        float fillDir[4];    // fill light (camera frame, opposite side)
        float highlight[4];  // rgb + strength (albedo itself is a vertex attribute)
    };
    struct WireFsParams {
        float color[4];  // flat line color of the wire overlay
    };

    void destroyTarget();
    bool makePipelines(int samples);
    glm::mat4 viewMatrix() const;

    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_buffer m_vbuf{};
    sg_buffer m_ibuf{};
    int m_indexCount = 0;

    sg_shader m_wireShader{};
    sg_pipeline m_wirePip{};
    sg_buffer m_wireVbuf{};
    sg_buffer m_wireIbuf{};
    int m_wireIndexCount = 0;

    // Offscreen MSAA color/depth (prefer 8x, fall back to 4 then 1) plus a
    // 1-sample resolve image that ImGui samples when MSAA is on. The swapchain
    // stays 1x so window readback (capturePng) does not have to blit out of a
    // multisampled default FB.
    sg_image m_color{};
    sg_image m_resolve{};
    sg_image m_depth{};
    sg_view m_colorAttach{};
    sg_view m_resolveAttach{};
    sg_view m_depthAttach{};
    sg_view m_texView{};
    int m_sampleCount = 1;
    int m_targetW = 0, m_targetH = 0;
    bool m_sizeClamped = false;

    // Orbit camera. m_center/m_distance/m_radius are the CURRENT state;
    // m_scene*/m_target* are the two remembered fits switched by m_fitMode.
    glm::vec3 m_center{0.0f};
    glm::vec3 m_sceneCenter{0.0f};
    glm::vec3 m_targetCenter{0.0f};
    float m_radius = 1.0f;        // fit radius of the active fit (near/far, zoom clamps)
    float m_sceneRadius = 1.0f;   // fit radius of the whole geometry
    float m_targetRadius = 1.0f;  // fit radius of the explicit target
    float m_distance = 3.0f;
    float m_yaw = 0.6f;
    float m_pitch = 0.5f;
    float m_fitZoom = 1.0f;  // fit distance multiplier (--preview-orbit)
    bool m_hasTarget = false;
    PreviewFitMode m_fitMode = PreviewFitMode::All;
    PreviewProjection m_projection = PreviewProjection::Perspective;
    bool m_wireframe = false;
    bool m_backendZeroToOne = false;  // depth range of the backend, cached in init()

    std::string m_summary;
    std::string m_error;
    bool m_ok = false;
};
