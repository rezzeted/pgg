#pragma once

// One PggServe slot: a loaded .pgg file, its MemoryCache, sticky camera,
// visual baselines and the diff snapshot. Engine work on a slot is serialized
// by engineMu; GPU never runs inside that mutex for long — the GPU job waits
// with the mutex held so two renders of the same file cannot interleave
// last-frame compare.

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/cache.h>
#include <pgg/src/eval/modules.h>

#include "FrameCompare.h"
#include "GeometryPreview.h"

struct NamedView {
    std::string name;
    nlohmann::json fields;
};

struct DiffSnapshot {
    std::string filePath;
    std::vector<std::pair<std::string, std::optional<uint64_t>>> fps;
};

struct BaselineFrame {
    int w = 0, h = 0;
    std::vector<std::uint8_t> pixels;
};

struct AutoReloadResult {
    bool reloaded = false;
    std::vector<pgg::Diagnostic> diags;
};

struct CameraTargetSpec {
    enum class Kind { None, Point, Group, Binding, GroupOnBinding };
    Kind kind = Kind::None;
    glm::vec3 point{0.0f};
    std::string name;
    std::string binding;
    std::string raw;
};

class DocumentSession {
public:
    static constexpr size_t kCacheCapacity = 4096;

    explicit DocumentSession(std::string canonicalPath);

    std::mutex engineMu;

    const std::string& canonicalPath() const { return m_canonical; }

    // explicitLoad: RPC load (clears visual baselines, diff snapshot, sticky camera).
    // Auto-reload keeps those so F4 can report fingerprint/frame diffs of the edit.
    bool loadFromDisk(const std::string& resolvedPath, const std::vector<std::string>& extraRoots,
                      bool fromRpcSource, bool explicitLoad);
    nlohmann::json staticCheck() const;

    AutoReloadResult autoReloadIfChanged();

    pgg::RunParams makeRunParams() const;
    std::vector<std::string> importRoots() const;
    std::vector<std::string> boundParamNames() const;
    nlohmann::json paramsJson() const;
    void setParams(const nlohmann::json& args, nlohmann::json& unknown);

    nlohmann::json viewsJson() const;
    nlohmann::json mergeNamedViewArgs(const nlohmann::json& args, std::string& err) const;

    bool runOutputsFingerprints(std::vector<std::pair<std::string, std::optional<uint64_t>>>& outFps,
                                double& outMs, std::string& why);

    struct PullResult {
        bool ok = false;
        pgg::Value value;
        std::vector<pgg::Diagnostic> diags;
        std::vector<pgg::BindingProfile> profile;
        uint64_t cacheHits = 0, cacheMisses = 0;
        double ms = 0.0;
        std::string error;
    };
    PullResult pullGeometry(const std::string& node);

    pgg::RunResult runProbes(const std::vector<std::string>& specs, double& ms);

    bool resolveCameraTarget(const CameraTargetSpec& spec, const PreviewGeometry& geo,
                              glm::vec3& outCenter, float& outRadius, bool& hasBBox, glm::vec3& bmin,
                              glm::vec3& bmax, std::string& err);

    nlohmann::json sessionEcho() const { return {{"file", m_canonical}}; }

    void recordMtimes();
    void loadNamedViews(const std::string& pggPath);

    void clearVisualState();  // baselines + last frame; called on explicit load
    void resetDiffSnapshot() { m_diffSnapshot.reset(); }

    std::optional<DiffSnapshot>& diffSnapshot() { return m_diffSnapshot; }
    std::map<std::string, BaselineFrame>& baselineFrames() { return m_baselineFrames; }

    std::string lastFrameKey;
    int lastFrameW = 0, lastFrameH = 0;
    std::vector<std::uint8_t> lastFramePixels;

    std::optional<float> stickyYawDeg;
    std::optional<float> stickyPitchDeg;
    std::optional<float> stickyDistance;

    std::string lastNode;
    std::string previewTarget;
    bool previewHasValue = false;
    uint64_t lastCacheHits = 0, lastCacheMisses = 0;
    std::vector<pgg::BindingProfile> lastProfile;
    bool mainFileFromRpcSource = false;

    pgg::MemoryCache* cache() { return m_cache.get(); }
    const pgg::MemoryCache* cache() const { return m_cache.get(); }
    const pgg::Document& document() const { return m_doc; }
    bool hasFile() const { return m_doc.file != nullptr; }

private:
    std::string m_canonical;
    std::string m_resolvedPath;
    pgg::Document m_doc;
    std::unique_ptr<pgg::ModuleClosure> m_closure;
    std::vector<pgg::Diagnostic> m_allDiags;
    std::vector<std::pair<std::string, std::string>> m_paramValues;
    std::vector<std::string> m_extraRoots;
    std::unique_ptr<pgg::MemoryCache> m_cache;
    std::map<std::string, std::int64_t> m_fileMtimes;
    std::vector<NamedView> m_namedViews;
    std::optional<DiffSnapshot> m_diffSnapshot;
    std::map<std::string, BaselineFrame> m_baselineFrames;

    bool m_bindingTargetResolved = false;
    std::string m_bindingTargetPath;
    PreviewGeometry m_bindingTargetGeo;
};

std::filesystem::path serveRepoRoot();
std::string resolveServePath(const std::string& path);
std::string canonicalServePath(const std::string& path);
std::int64_t fileMtimeNs(const std::string& path);
double wallNowSec();
bool diagsHaveErrors(const std::vector<pgg::Diagnostic>& diags);
nlohmann::json diagnosticsJson(const std::vector<pgg::Diagnostic>& diags);
nlohmann::json fingerprintJson(const std::optional<uint64_t>& fp);
nlohmann::json vec3Json(const glm::vec3& v);
nlohmann::json valueStatsJson(const pgg::Value& v, double ms);
nlohmann::json silhouetteJson(const SilhouetteMetrics& m);
std::array<std::uint8_t, 3> previewClearRgb8();
pgg::Value parseCliValue(const std::string& v);
std::string jsonToParamText(const nlohmann::json& v);
std::string literalText(const pgg::Expr* e);
CameraTargetSpec parseCameraTargetSpec(const std::string& text);
std::string currentFrameKey(const std::string& node, const GeometryPreview& preview,
                             const PreviewBuildOptions& opts);
nlohmann::json cameraJson(const GeometryPreview& preview, bool hasBBox, const glm::vec3& bmin,
                          const glm::vec3& bmax);
nlohmann::json renderStateJson(const GeometryPreview& preview, const CameraTargetSpec& target,
                              const std::string& chromeEcho);
