#include "pch.h"

#include "ServeRuntime.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>

#include <pgg/src/eval/builtin_docs.h>
#include <pgg/src/eval/builtins.h>
#include <pgg/src/eval/docs_lookup.h>
#include <pgg/src/eval/obj_export.h>

#include "FrameCompare.h"
#include "PreviewCapture.h"

using nlohmann::json;

struct ServeRuntime::GpuJob {
    std::shared_ptr<DocumentSession> session;
    PreviewGeometry geo;
    PreviewBuildOptions opts;
    bool refit = true;

    bool hasOrbit = false;
    float yaw = 0.0f, pitch = 0.0f, orbitZoom = 1.0f;
    bool hasZoom = false;
    float zoom = 1.0f;
    bool hasDistance = false;
    float distance = 0.0f;
    PreviewFitMode fit = PreviewFitMode::All;
    PreviewProjection proj = PreviewProjection::Perspective;
    bool wire = false;
    enum class TargetApply { None, Point, BBox };
    TargetApply targetApply = TargetApply::None;
    glm::vec3 targetCenter{0.0f};
    float targetRadius = 1.0f;
    bool autoYaw = true;
    bool forceFitAll = false;
    bool hasTargetBBox = false;
    glm::vec3 targetBMin{0.0f}, targetBMax{0.0f};
    CameraTargetSpec targetSpec;

    int wantW = ServeRuntime::kDefaultFboW;
    int wantH = ServeRuntime::kDefaultFboH;

    std::string node;
    std::string outPath;
    bool writePng = true;
    bool comparePrev = false;
    bool compareBaseline = false;
    bool saveBaseline = false;
    bool isReference = false;
    std::string refImagePath;
    std::vector<std::uint8_t> refPixels;
    int refW = 0, refH = 0;
    std::string chromeEcho = "off";

    json stats;
    json diagnostics;
    bool reloaded = false;
    json loadDiagnostics;
    uint64_t cacheHits = 0, cacheMisses = 0;

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    std::string errKind, errMessage;
    json data;
};

ServeRuntime::ServeRuntime(ServeRpcServer& rpc) : m_rpc(rpc) {}

ServeRuntime::~ServeRuntime() { stopWorkers(); }

void ServeRuntime::touchLocked(const std::string& canonical) {
    auto it = m_lruIt.find(canonical);
    if (it == m_lruIt.end()) return;
    m_lru.erase(it->second);
    m_lru.push_front(canonical);
    m_lruIt[canonical] = m_lru.begin();
}

std::shared_ptr<DocumentSession> ServeRuntime::findSlot(const std::string& canonical) const {
    std::lock_guard<std::mutex> lock(m_slotMu);
    auto it = m_slots.find(canonical);
    if (it == m_slots.end()) return nullptr;
    return it->second;
}

std::shared_ptr<DocumentSession> ServeRuntime::getOrCreateSlot(const std::string& canonical,
                                                                  std::string& err) {
    std::lock_guard<std::mutex> lock(m_slotMu);
    auto it = m_slots.find(canonical);
    if (it != m_slots.end()) {
        touchLocked(canonical);
        return it->second;
    }
    if (m_slots.size() >= kMaxSlots) {
        std::string victim;
        for (auto lru = m_lru.rbegin(); lru != m_lru.rend(); ++lru) {
            auto sit = m_slots.find(*lru);
            if (sit != m_slots.end() && sit->second.use_count() == 1) {
                victim = *lru;
                break;
            }
        }
        if (victim.empty()) {
            err = "all " + std::to_string(kMaxSlots) + " file slots are busy";
            return nullptr;
        }
        m_lru.erase(m_lruIt[victim]);
        m_lruIt.erase(victim);
        m_slots.erase(victim);
    }
    auto session = std::make_shared<DocumentSession>(canonical);
    m_slots[canonical] = session;
    m_lru.push_front(canonical);
    m_lruIt[canonical] = m_lru.begin();
    return session;
}

std::shared_ptr<DocumentSession> ServeRuntime::resolveSlot(uint64_t clientId, const json& args) {
    std::string file = args.value("file", std::string{});
    if (file.empty()) file = m_rpc.clientFile(clientId);
    if (file.empty()) ServeRpcServer::fail("no_file", "no .pgg file loaded (load first or pass file=)");
    const std::string canonical = canonicalServePath(file);
    auto session = findSlot(canonical);
    if (!session) ServeRpcServer::fail("no_file", "no session for '" + file + "'");
    {
        std::lock_guard<std::mutex> lock(m_slotMu);
        touchLocked(canonical);
    }
    return session;
}

bool ServeRuntime::postCpu(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(m_cpuMu);
        if (m_cpuQueue.size() >= kMaxCpuQueue) return false;
        m_cpuQueue.push_back(std::move(fn));
    }
    m_cpuCv.notify_one();
    return true;
}

void ServeRuntime::defer(uint64_t clientId, std::function<json()> fn) {
    if (!postCpu([this, clientId, fn = std::move(fn)]() {
            try {
                json data = fn();
                m_rpc.reply(clientId, data);
            } catch (const RpcException& e) {
                m_rpc.replyError(clientId, e.kind, e.message);
            } catch (const std::exception& e) {
                m_rpc.replyError(clientId, "internal", e.what());
            }
        })) {
        ServeRpcServer::fail("busy", "CPU work queue is full");
    }
}

bool ServeRuntime::postGpuAndWait(const std::shared_ptr<GpuJob>& job) {
    if (!m_gpuReady) {
        job->ok = false;
        job->errKind = "no_gpu";
        job->errMessage = "PggServe is running without a GPU context (headless or no visible window yet)";
        job->done = true;
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(m_gpuMu);
        if (m_gpuQueue.size() >= kMaxGpuQueue) return false;
        m_gpuQueue.push_back(job);
    }
    std::unique_lock<std::mutex> lock(job->mu);
    job->cv.wait(lock, [&] { return job->done; });
    return true;
}

void ServeRuntime::startWorkers() {
    unsigned n = std::thread::hardware_concurrency();
    if (n < 2) n = 2;
    if (n > 8) n = 8;
    m_stop = false;
    for (unsigned i = 0; i < n; ++i) m_workers.emplace_back([this] { workerLoop(); });
}

void ServeRuntime::stopWorkers() {
    m_stop = true;
    m_cpuCv.notify_all();
    {
        std::lock_guard<std::mutex> lock(m_gpuMu);
        for (auto& job : m_gpuQueue) {
            std::lock_guard<std::mutex> jl(job->mu);
            if (!job->done) {
                job->ok = false;
                job->errKind = "internal";
                job->errMessage = "PggServe shutting down";
                job->done = true;
                job->cv.notify_all();
            }
        }
        m_gpuQueue.clear();
        if (m_gpuInflight) {
            std::lock_guard<std::mutex> jl(m_gpuInflight->mu);
            if (!m_gpuInflight->done) {
                m_gpuInflight->ok = false;
                m_gpuInflight->errKind = "internal";
                m_gpuInflight->errMessage = "PggServe shutting down";
                m_gpuInflight->done = true;
                m_gpuInflight->cv.notify_all();
            }
        }
    }
    for (std::thread& t : m_workers)
        if (t.joinable()) t.join();
    m_workers.clear();
}

void ServeRuntime::workerLoop() {
    for (;;) {
        std::function<void()> fn;
        {
            std::unique_lock<std::mutex> lock(m_cpuMu);
            m_cpuCv.wait(lock, [&] { return m_stop || !m_cpuQueue.empty(); });
            if (m_stop && m_cpuQueue.empty()) return;
            fn = std::move(m_cpuQueue.front());
            m_cpuQueue.pop_front();
        }
        fn();
    }
}

nlohmann::json ServeRuntime::statusJson() const {
    json slots = json::array();
    std::string lastFile;
    json lastParams = json::object();
    json lastCache = {{"size", 0}, {"capacity", 0}, {"hits", 0}, {"misses", 0}};
    json lastPreview = {{"target", ""}, {"has_value", false}};
    json lastProfile = json::array();
    double profTotal = 0.0;
    {
        std::lock_guard<std::mutex> lock(m_slotMu);
        for (const std::string& path : m_lru) {
            auto it = m_slots.find(path);
            if (it == m_slots.end()) continue;
            DocumentSession& s = *it->second;
            json cache = {{"size", s.cache() ? s.cache()->size() : 0},
                          {"capacity", s.cache() ? s.cache()->capacity() : 0},
                          {"hits", s.lastCacheHits},
                          {"misses", s.lastCacheMisses}};
            json preview = {{"target", s.previewTarget}, {"has_value", s.previewHasValue}};
            json row = {{"file", s.canonicalPath()},
                        {"cache", cache},
                        {"preview", preview},
                        {"last_node", s.lastNode}};
            slots.push_back(row);
            if (lastFile.empty()) {
                lastFile = s.canonicalPath();
                lastParams = s.paramsJson();
                lastCache = cache;
                lastPreview = preview;
                for (const pgg::BindingProfile& b : pgg::profileByTime(s.lastProfile)) {
                    profTotal += b.ms;
                    if (lastProfile.size() < 20)
                        lastProfile.push_back({{"name", b.name},
                                               {"ms", b.ms},
                                               {"field_evals", b.fieldEvals},
                                               {"cache_hit", b.cacheHit}});
                }
            }
        }
    }
    return {{"file", lastFile},
            {"params", lastParams},
            {"cache", lastCache},
            {"preview", lastPreview},
            {"profile", lastProfile},
            {"profile_total_ms", profTotal},
            {"slots", slots},
            {"gpu", m_gpuReady.load()},
            {"uptime_s", startTimeSec > 0.0 ? wallNowSec() - startTimeSec : 0.0}};
}

void ServeRuntime::registerHandlers() {
    m_rpc.on("ping", [this](uint64_t, const json& args) -> std::optional<json> {
        return handlePing(args);
    });
    m_rpc.on("status", [this](uint64_t, const json& args) -> std::optional<json> {
        return handleStatus(args);
    });
    m_rpc.on("load", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleLoad(id, args); });
        return std::nullopt;
    });
    m_rpc.on("params", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleParams(id, args); });
        return std::nullopt;
    });
    m_rpc.on("views", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleViews(id, args); });
        return std::nullopt;
    });
    m_rpc.on("render", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleRender(id, args); });
        return std::nullopt;
    });
    m_rpc.on("reference", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleReference(id, args); });
        return std::nullopt;
    });
    m_rpc.on("probe", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleProbe(id, args); });
        return std::nullopt;
    });
    m_rpc.on("export", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleExport(id, args); });
        return std::nullopt;
    });
    m_rpc.on("diff", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleDiff(id, args); });
        return std::nullopt;
    });
    m_rpc.on("docs", [this](uint64_t id, const json& args) -> std::optional<json> {
        defer(id, [this, id, args] { return handleDocs(id, args); });
        return std::nullopt;
    });
}

json ServeRuntime::handlePing(const json&) {
    return {{"pong", true}, {"app", "PggServe"}, {"protocol", 1}};
}

json ServeRuntime::handleStatus(const json&) { return statusJson(); }

json ServeRuntime::handleLoad(uint64_t clientId, const json& args) {
    std::string path;
    std::vector<std::string> roots;
    if (args.contains("lib_roots") && args["lib_roots"].is_array()) {
        for (const json& r : args["lib_roots"])
            if (r.is_string()) roots.push_back(r.get<std::string>());
    }
    bool fromSource = false;
    if (args.contains("source")) {
        const std::filesystem::path dir = serveRepoRoot() / "tmp" / "pgg_rpc_source";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        path = (dir / ("src_" + std::to_string(++m_srcCounter) + ".pgg")).string();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) ServeRpcServer::fail("io_error", "cannot write " + path);
        out << args.value("source", std::string{});
        out.close();
        if (!out) ServeRpcServer::fail("io_error", "cannot write " + path);
        fromSource = true;
    } else if (args.contains("path")) {
        path = args.value("path", std::string{});
    } else {
        ServeRpcServer::fail("bad_args", "load needs 'path' or 'source'");
    }
    const std::string resolved = resolveServePath(path);
    const std::string canonical = canonicalServePath(resolved);
    std::string slotErr;
    auto session = getOrCreateSlot(canonical, slotErr);
    if (!session) ServeRpcServer::fail("busy", slotErr);
    std::lock_guard<std::mutex> engine(session->engineMu);
    if (!session->loadFromDisk(resolved, roots, fromSource, true))
        ServeRpcServer::fail("io_error", "cannot open " + path);
    m_rpc.setClientFile(clientId, session->canonicalPath());
    json data = session->staticCheck();
    data["path"] = path;
    data["session"] = session->sessionEcho();
    const bool wantSnapshot = args.value("snapshot", false);
    if (wantSnapshot) {
        bool recorded = false;
        if (!data.value("has_errors", true)) {
            std::vector<std::pair<std::string, std::optional<uint64_t>>> fps;
            double ms = 0.0;
            std::string why;
            if (!session->runOutputsFingerprints(fps, ms, why))
                ServeRpcServer::fail("run_errors", why.empty() ? "run failed" : why);
            session->diffSnapshot() = DiffSnapshot{session->canonicalPath(), fps};
            recorded = true;
        }
        data["snapshot"] = recorded;
    }
    return data;
}

json ServeRuntime::handleParams(uint64_t clientId, const json& args) {
    auto session = resolveSlot(clientId, args);
    std::lock_guard<std::mutex> engine(session->engineMu);
    json unknown;
    session->setParams(args, unknown);
    return {{"params", session->paramsJson()}, {"unknown", unknown}, {"session", session->sessionEcho()}};
}

json ServeRuntime::handleViews(uint64_t clientId, const json& args) {
    auto session = resolveSlot(clientId, args);
    std::lock_guard<std::mutex> engine(session->engineMu);
    return session->viewsJson();
}

void ServeRuntime::applyRenderArgs(const json& args, GpuJob& job, CameraTargetSpec& target,
                                   std::string& chromeEcho, std::string& err) {
    chromeEcho = "off";
    if (args.contains("chrome")) {
        const std::string c = args.value("chrome", std::string{});
        if (c != "off" && c != "on") {
            err = "chrome must be 'on' or 'off'";
            return;
        }
        chromeEcho = c;
    }

    job.wire = args.contains("wire") ? args.value("wire", false) : false;

    if (!args.contains("ortho")) {
        job.proj = PreviewProjection::Perspective;
    } else {
        const std::string o = args.value("ortho", std::string{});
        if (o == "front")
            job.proj = PreviewProjection::OrthoFront;
        else if (o == "side")
            job.proj = PreviewProjection::OrthoSide;
        else if (o == "top")
            job.proj = PreviewProjection::OrthoTop;
        else if (o == "off" || o == "perspective")
            job.proj = PreviewProjection::Perspective;
        else {
            err = "ortho must be front|side|top|off";
            return;
        }
    }

    if (args.contains("highlight"))
        job.opts.highlightGroup = args.value("highlight", std::string{});
    else
        job.opts.highlightGroup.clear();

    if (!args.contains("shading")) {
        job.opts.shading = PreviewShading::Auto;
    } else {
        const std::string v = args.value("shading", std::string{"auto"});
        if (v == "flat")
            job.opts.shading = PreviewShading::Flat;
        else if (v == "smooth")
            job.opts.shading = PreviewShading::Smooth;
        else if (v == "auto")
            job.opts.shading = PreviewShading::Auto;
        else {
            err = "shading must be auto|flat|smooth";
            return;
        }
    }

    if (!args.contains("colors"))
        job.opts.vertexColors = true;
    else
        job.opts.vertexColors = args.value("colors", true);

    job.autoYaw = !args.contains("orbit");
    if (args.contains("orbit")) {
        const json& o = args["orbit"];
        if (!o.is_array() || o.size() < 2) {
            err = "orbit must be [yaw, pitch] or [yaw, pitch, zoom]";
            return;
        }
        job.hasOrbit = true;
        job.yaw = o[0].get<float>();
        job.pitch = o[1].get<float>();
        job.orbitZoom = o.size() >= 3 ? o[2].get<float>() : 1.0f;
    }

    const bool hasOrbitZoom = args.contains("orbit") && args["orbit"].is_array() && args["orbit"].size() >= 3;
    if (args.contains("zoom")) {
        job.hasZoom = true;
        job.zoom = args.value("zoom", 1.0f);
    } else if (!args.contains("distance") && !hasOrbitZoom) {
        job.hasZoom = true;
        job.zoom = 1.0f;
    }

    if (args.contains("distance")) {
        job.hasDistance = true;
        job.distance = args.value("distance", 0.0f);
    }

    if (!args.contains("target")) {
        target = CameraTargetSpec{};
        job.fit = PreviewFitMode::All;
    } else {
        const std::string t = args.value("target", std::string{});
        if (t.empty()) {
            target = CameraTargetSpec{};
            job.fit = PreviewFitMode::All;
        } else {
            target = parseCameraTargetSpec(t);
            if (target.kind == CameraTargetSpec::Kind::None) {
                err = "unparseable target '" + t +
                      "' (want x,y,z | group:<name> | group:<name>@<binding> | binding:<path>)";
                return;
            }
            if (!args.contains("fit")) job.fit = PreviewFitMode::Target;
        }
    }
    if (args.contains("fit")) {
        const std::string f = args.value("fit", std::string{});
        if (f == "target")
            job.fit = PreviewFitMode::Target;
        else if (f == "all")
            job.fit = PreviewFitMode::All;
        else
            err = "fit must be 'all' or 'target'";
    }

    job.wantW = kDefaultFboW;
    job.wantH = kDefaultFboH;
    if (args.contains("size")) {
        const json& sz = args["size"];
        if (sz.is_array() && sz.size() >= 2) {
            job.wantW = std::max(1, static_cast<int>(std::lround(sz[0].get<float>())));
            job.wantH = std::max(1, static_cast<int>(std::lround(sz[1].get<float>())));
        }
    }
    // frame=window and chrome are accepted and ignored: the FBO is always the preview.
    (void)args.contains("frame");
}

json ServeRuntime::handleRender(uint64_t clientId, const json& argsIn) {
    auto session = resolveSlot(clientId, argsIn);
    std::lock_guard<std::mutex> engine(session->engineMu);
    const AutoReloadResult reload = session->autoReloadIfChanged();
    std::string viewErr;
    const json args = session->mergeNamedViewArgs(argsIn, viewErr);
    if (!viewErr.empty()) ServeRpcServer::fail("bad_args", viewErr);
    const std::string node = args.value("node", std::string{});
    if (node.empty()) ServeRpcServer::fail("bad_args", "render needs 'node' (or a named view that sets it)");

    if (args.contains("frame")) {
        const std::string f = args.value("frame", std::string{});
        if (f != "preview" && f != "window") ServeRpcServer::fail("bad_args", "frame must be 'preview' or 'window'");
    }

    bool comparePrev = false, compareBaseline = false;
    if (args.contains("compare")) {
        const std::string c = args.value("compare", std::string{});
        if (c == "prev")
            comparePrev = true;
        else if (c == "baseline")
            compareBaseline = true;
        else
            ServeRpcServer::fail("bad_args", "compare must be 'prev' or 'baseline'");
    }
    const bool saveBaseline = args.value("save_baseline", false);
    const bool writePng = args.value("png", true);

    auto job = std::make_shared<GpuJob>();
    job->session = session;
    CameraTargetSpec target;
    std::string chromeEcho = "off";
    std::string applyErr;
    applyRenderArgs(args, *job, target, chromeEcho, applyErr);
    if (!applyErr.empty()) ServeRpcServer::fail("bad_args", applyErr);
    job->chromeEcho = chromeEcho;
    job->targetSpec = target;
    job->node = node;
    job->comparePrev = comparePrev;
    job->compareBaseline = compareBaseline;
    job->saveBaseline = saveBaseline;
    job->writePng = writePng;
    job->reloaded = reload.reloaded;
    if (reload.reloaded) job->loadDiagnostics = diagnosticsJson(reload.diags);

    DocumentSession::PullResult pull = session->pullGeometry(node);
    job->cacheHits = pull.cacheHits;
    job->cacheMisses = pull.cacheMisses;
    job->diagnostics = diagnosticsJson(pull.diags);
    if (!pull.ok)
        ServeRpcServer::fail("run_failed", pull.error.empty() ? "run failed for '" + node + "'" : pull.error);
    if (diagsHaveErrors(pull.diags)) {
        std::string why;
        for (const pgg::Diagnostic& d : pull.diags) {
            if (d.isWarning) continue;
            why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
        }
        ServeRpcServer::fail("run_errors", why);
    }
    job->stats = valueStatsJson(pull.value, pull.ms);
    job->geo = buildPreviewGeometry(pull.value, job->opts);
    job->refit = true;

    if (target.kind != CameraTargetSpec::Kind::None) {
        glm::vec3 c, mn, mx;
        float r = 1.0f;
        bool hasBBox = false;
        std::string terr;
        if (!session->resolveCameraTarget(target, job->geo, c, r, hasBBox, mn, mx, terr))
            ServeRpcServer::fail("target_unresolved", terr);
        job->hasTargetBBox = hasBBox;
        job->targetBMin = mn;
        job->targetBMax = mx;
        if (target.kind == CameraTargetSpec::Kind::Point) {
            job->targetApply = GpuJob::TargetApply::Point;
            job->targetCenter = c;
        } else {
            job->targetApply = GpuJob::TargetApply::BBox;
            job->targetCenter = c;
            job->targetRadius = r;
        }
    }

    if (!job->hasOrbit && session->stickyYawDeg && session->stickyPitchDeg) {
        job->hasOrbit = true;
        job->yaw = *session->stickyYawDeg;
        job->pitch = *session->stickyPitchDeg;
        job->orbitZoom = 1.0f;
        job->autoYaw = false;
    }
    if (!job->hasDistance && session->stickyDistance && !job->hasZoom) {
        job->hasDistance = true;
        job->distance = *session->stickyDistance;
        job->hasZoom = false;
    }

    if (writePng) {
        const std::string outArg = args.value("out", std::string{});
        if (!outArg.empty()) {
            job->outPath = outArg;
        } else {
            job->outPath = (serveRepoRoot() / "tmp" / "pgg_rpc_shots" /
                            ("shot_" + std::to_string(++m_shotCounter) + ".png"))
                               .string();
        }
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(job->outPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }

    if (!postGpuAndWait(job)) ServeRpcServer::fail("busy", "GPU queue is full");
    if (!job->ok) ServeRpcServer::fail(job->errKind, job->errMessage);
    job->data["session"] = session->sessionEcho();
    return job->data;
}

json ServeRuntime::handleReference(uint64_t clientId, const json& args) {
    auto session = resolveSlot(clientId, args);
    std::lock_guard<std::mutex> engine(session->engineMu);
    const std::string node = args.value("node", std::string{});
    const std::string image = args.value("image", std::string{});
    if (node.empty() || image.empty()) ServeRpcServer::fail("bad_args", "reference needs 'node' and 'image'");
    const AutoReloadResult reload = session->autoReloadIfChanged();

    std::vector<std::uint8_t> refPixels;
    int refW = 0, refH = 0;
    if (!loadImageRgba(image, refPixels, refW, refH))
        ServeRpcServer::fail("invalid_input", "cannot read reference image '" + image + "'");

    auto job = std::make_shared<GpuJob>();
    job->session = session;
    job->node = node;
    job->isReference = true;
    job->refImagePath = image;
    job->refPixels = std::move(refPixels);
    job->refW = refW;
    job->refH = refH;
    job->forceFitAll = true;
    job->fit = PreviewFitMode::All;
    job->autoYaw = !args.contains("orbit");
    job->reloaded = reload.reloaded;
    if (reload.reloaded) job->loadDiagnostics = diagnosticsJson(reload.diags);
    if (args.contains("orbit")) {
        const json& o = args["orbit"];
        if (o.is_array() && o.size() >= 2) {
            job->hasOrbit = true;
            job->yaw = o[0].get<float>();
            job->pitch = o[1].get<float>();
            job->orbitZoom = o.size() >= 3 ? o[2].get<float>() : 1.0f;
        }
    }
    if (args.contains("zoom")) {
        job->hasZoom = true;
        job->zoom = args.value("zoom", 1.0f);
    }
    const std::string o = args.value("ortho", std::string{"front"});
    if (o == "front")
        job->proj = PreviewProjection::OrthoFront;
    else if (o == "side")
        job->proj = PreviewProjection::OrthoSide;
    else if (o == "top")
        job->proj = PreviewProjection::OrthoTop;
    else if (o == "off" || o == "perspective")
        job->proj = PreviewProjection::Perspective;
    else
        ServeRpcServer::fail("bad_args", "ortho must be front|side|top|off");
    job->wantW = kDefaultFboW;
    job->wantH = kDefaultFboH;
    if (args.contains("size")) {
        const json& sz = args["size"];
        if (sz.is_array() && sz.size() >= 2) {
            job->wantW = std::max(1, static_cast<int>(std::lround(sz[0].get<float>())));
            job->wantH = std::max(1, static_cast<int>(std::lround(sz[1].get<float>())));
        }
    }

    DocumentSession::PullResult pull = session->pullGeometry(node);
    job->cacheHits = pull.cacheHits;
    job->cacheMisses = pull.cacheMisses;
    job->diagnostics = diagnosticsJson(pull.diags);
    if (!pull.ok)
        ServeRpcServer::fail("run_failed", pull.error.empty() ? "run failed for '" + node + "'" : pull.error);
    if (diagsHaveErrors(pull.diags)) {
        std::string why;
        for (const pgg::Diagnostic& d : pull.diags) {
            if (d.isWarning) continue;
            why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
        }
        ServeRpcServer::fail("run_errors", why);
    }
    job->stats = valueStatsJson(pull.value, pull.ms);
    job->geo = buildPreviewGeometry(pull.value, job->opts);
    job->outPath = (serveRepoRoot() / "tmp" / "pgg_rpc_shots" / ("ref_" + std::to_string(++m_refCounter) + ".png"))
                       .string();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(job->outPath).parent_path(), ec);

    if (!postGpuAndWait(job)) ServeRpcServer::fail("busy", "GPU queue is full");
    if (!job->ok) ServeRpcServer::fail(job->errKind, job->errMessage);
    job->data["session"] = session->sessionEcho();
    return job->data;
}

json ServeRuntime::handleProbe(uint64_t clientId, const json& args) {
    auto session = resolveSlot(clientId, args);
    std::lock_guard<std::mutex> engine(session->engineMu);
    std::vector<std::string> specs;
    if (args.contains("spec") && args["spec"].is_string() && !args["spec"].get<std::string>().empty())
        specs.push_back(args["spec"].get<std::string>());
    if (args.contains("specs")) {
        if (!args["specs"].is_array()) ServeRpcServer::fail("bad_args", "'specs' must be an array of strings");
        for (const json& s : args["specs"]) {
            if (!s.is_string() || s.get<std::string>().empty())
                ServeRpcServer::fail("bad_args", "'specs' must be an array of non-empty strings");
            specs.push_back(s.get<std::string>());
        }
    }
    if (specs.empty()) ServeRpcServer::fail("bad_args", "probe needs 'spec' or 'specs'");
    const AutoReloadResult reload = session->autoReloadIfChanged();
    double ms = 0.0;
    pgg::RunResult r = session->runProbes(specs, ms);
    json records = json::array();
    for (const pgg::ProbeRecord& pr : r.probes)
        records.push_back(
            {{"origin", pr.origin}, {"path", pr.path}, {"inspector", pr.inspector}, {"text", pr.text}});
    json data = {{"records", records},
                 {"diagnostics", diagnosticsJson(r.diagnostics)},
                 {"has_errors", r.hasErrors()},
                 {"ms", ms},
                 {"cache", {{"hits", session->lastCacheHits}, {"misses", session->lastCacheMisses}}},
                 {"reloaded", reload.reloaded},
                 {"session", session->sessionEcho()}};
    if (reload.reloaded) data["load_diagnostics"] = diagnosticsJson(reload.diags);
    return data;
}

json ServeRuntime::handleExport(uint64_t clientId, const json& args) {
    auto session = resolveSlot(clientId, args);
    std::lock_guard<std::mutex> engine(session->engineMu);
    const std::string node = args.value("node", std::string{});
    const std::string objPath = args.value("obj_path", std::string{});
    if (node.empty() || objPath.empty())
        ServeRpcServer::fail("bad_args", "export needs 'node' and 'obj_path'");
    const AutoReloadResult reload = session->autoReloadIfChanged();
    DocumentSession::PullResult pull = session->pullGeometry(node);
    if (!pull.ok)
        ServeRpcServer::fail("run_failed", pull.error.empty() ? "no geometry value at '" + node + "'" : pull.error);
    if (pgg::valueBase(pull.value) == pgg::ScalarType::Sdf)
        ServeRpcServer::fail("no_geometry", "sdf values are not exported; mesh them with mesh_from_sdf");
    pgg::GeoPtr geo = pgg::asGeo(pull.value);
    bool realized = false;
    if (geo->kind == pgg::GeoKind::Instances) {
        geo = pgg::realizeInstances(*geo);
        realized = true;
        if (!geo) ServeRpcServer::fail("run_failed", "realizeInstances returned null");
        pull.value = pgg::Value(geo);
    }
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(objPath).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::string err;
    if (!pgg::writeObj(objPath, *geo, &err)) ServeRpcServer::fail("io_error", err);
    json data = {{"path", objPath},
                 {"realized", realized},
                 {"stats", valueStatsJson(pull.value, pull.ms)},
                 {"cache", {{"hits", pull.cacheHits}, {"misses", pull.cacheMisses}}},
                 {"reloaded", reload.reloaded},
                 {"session", session->sessionEcho()}};
    if (reload.reloaded) data["load_diagnostics"] = diagnosticsJson(reload.diags);
    return data;
}

json ServeRuntime::handleDiff(uint64_t clientId, const json& args) {
    auto session = resolveSlot(clientId, args);
    std::lock_guard<std::mutex> engine(session->engineMu);
    const bool update = args.value("update", false);
    const AutoReloadResult reload = session->autoReloadIfChanged();
    std::vector<std::pair<std::string, std::optional<uint64_t>>> fps;
    double ms = 0.0;
    std::string why;
    if (!session->runOutputsFingerprints(fps, ms, why))
        ServeRpcServer::fail("run_errors", why.empty() ? "run failed" : why);
    json data;
    data["ms"] = ms;
    data["cache"] = {{"hits", session->lastCacheHits}, {"misses", session->lastCacheMisses}};
    data["reloaded"] = reload.reloaded;
    data["session"] = session->sessionEcho();
    if (reload.reloaded) data["load_diagnostics"] = diagnosticsJson(reload.diags);
    auto& snap = session->diffSnapshot();
    if (!snap || snap->filePath != session->canonicalPath()) {
        snap = DiffSnapshot{session->canonicalPath(), fps};
        json outs = json::array();
        for (const auto& [n, fp] : fps)
            outs.push_back({{"name", n}, {"fingerprint", fingerprintJson(fp)}});
        data["baseline_created"] = true;
        data["outputs"] = std::move(outs);
        return data;
    }
    std::map<std::string, std::optional<uint64_t>> prev;
    for (const auto& [n, fp] : snap->fps) prev[n] = fp;
    json outs = json::array();
    std::map<std::string, bool> seen;
    bool identical = true;
    for (const auto& [n, fp] : fps) {
        seen[n] = true;
        json jo;
        jo["name"] = n;
        jo["fingerprint_now"] = fingerprintJson(fp);
        const auto it = prev.find(n);
        if (it == prev.end()) {
            jo["status"] = "added";
            jo["fingerprint_prev"] = nullptr;
            identical = false;
        } else if (!fp || !it->second) {
            jo["status"] = "skipped";
            jo["note"] = "no structural fingerprint";
            jo["fingerprint_prev"] = fingerprintJson(it->second);
        } else if (*fp == *it->second) {
            jo["status"] = "identical";
            jo["fingerprint_prev"] = fingerprintJson(it->second);
        } else {
            jo["status"] = "changed";
            jo["fingerprint_prev"] = fingerprintJson(it->second);
            identical = false;
        }
        outs.push_back(std::move(jo));
    }
    for (const auto& [n, fp] : snap->fps) {
        if (seen.count(n)) continue;
        identical = false;
        outs.push_back({{"name", n},
                        {"status", "removed"},
                        {"fingerprint_prev", fingerprintJson(fp)},
                        {"fingerprint_now", nullptr}});
    }
    data["baseline_created"] = false;
    data["identical"] = identical;
    data["outputs"] = std::move(outs);
    data["snapshot_updated"] = update;
    if (update) snap->fps = fps;
    return data;
}

json ServeRuntime::handleDocs(uint64_t clientId, const json& args) {
    const std::string symbol = args.value("symbol", std::string{});
    if (symbol.empty()) ServeRpcServer::fail("bad_args", "docs needs 'symbol'");
    auto builtinCard = [&](const std::string& name, const std::string& shown) -> json {
        const pgg::BuiltinDoc* bdoc = pgg::findBuiltinDoc(name);
        const pgg::BuiltinSig* bsig = pgg::findBuiltin(name);
        if (!bdoc || !bsig) return {};
        return json{{"symbol", shown},
                    {"kind", "builtin"},
                    {"name", name},
                    {"signature", pgg::builtinSignatureText(*bsig)},
                    {"group", bdoc->group},
                    {"summary", bdoc->summary},
                    {"example", bdoc->example}};
    };
    auto notFound = [&](const std::string& name, const std::string& msg) {
        std::string full = msg;
        const auto near = pgg::suggestBuiltinNames(name);
        if (!near.empty()) {
            full += "; did you mean:";
            for (const std::string& n : near) full += " " + n;
        }
        full += " (try builtin:<name> or PggTool docs builtins | rg -i …)";
        ServeRpcServer::fail("not_found", full);
    };
    if (symbol.rfind("builtin:", 0) == 0) {
        const std::string name = symbol.substr(8);
        json card = builtinCard(name, symbol);
        if (card.empty()) notFound(name, "builtin not found: " + name);
        return card;
    }
    std::shared_ptr<DocumentSession> session;
    try {
        session = resolveSlot(clientId, args);
    } catch (const RpcException&) {
        session.reset();
    }
    if (session) {
        std::lock_guard<std::mutex> engine(session->engineMu);
        if (session->hasFile()) {
            pgg::DocsLookupResult res =
                pgg::findDef(*session->document().file, session->canonicalPath(), symbol,
                             session->importRoots());
            if (res.found)
                return json{{"symbol", symbol},
                            {"kind", "def"},
                            {"signature", res.signature},
                            {"docstring", res.hasDoc ? res.docstring : std::string{}},
                            {"session", session->sessionEcho()}};
        }
    }
    json card = builtinCard(symbol, symbol);
    if (!card.empty()) return card;
    std::string msg = "def not found: " + symbol;
    if (!session || !session->hasFile()) msg = "no .pgg file loaded and not a builtin: " + symbol;
    notFound(symbol, msg);
    return {};
}

void ServeRuntime::beginGpuFrame(GeometryPreview& preview) {
    std::shared_ptr<GpuJob> job;
    {
        std::lock_guard<std::mutex> lock(m_gpuMu);
        if (m_gpuInflight || m_gpuQueue.empty()) return;
        m_gpuInflight = m_gpuQueue.front();
        m_gpuQueue.pop_front();
        job = m_gpuInflight;
    }
    preview.setWireframe(job->wire);
    preview.setProjection(job->proj);
    preview.setFitMode(job->fit);
    if (job->hasOrbit)
        preview.setOrbit(job->yaw, job->pitch, job->orbitZoom);
    else
        preview.setOrbit(34.3774677f, 28.6478898f, 1.0f);  // GeometryPreview defaults (0.6 / 0.5 rad)
    preview.setGeometry(job->geo, job->refit);
    if (job->forceFitAll) {
        preview.setFitMode(PreviewFitMode::All);
        preview.fit();
    } else if (job->targetApply == GpuJob::TargetApply::Point) {
        preview.setTarget(job->targetCenter, preview.sceneRadius());
        if (job->autoYaw) preview.faceTargetFromOutside();
    } else if (job->targetApply == GpuJob::TargetApply::BBox) {
        preview.setTarget(job->targetCenter, job->targetRadius);
        if (job->autoYaw) preview.faceTargetFromOutside();
    }
    if (job->hasZoom) preview.setZoom(job->zoom);
    if (job->hasDistance) preview.setDistance(job->distance);
    preview.ensureTarget(job->wantW, job->wantH);
    preview.render();
}

void ServeRuntime::finishGpuFrame(GeometryPreview& preview) {
    std::shared_ptr<GpuJob> job;
    {
        std::lock_guard<std::mutex> lock(m_gpuMu);
        job = m_gpuInflight;
        m_gpuInflight.reset();
    }
    if (!job) return;

    auto failJob = [&](const std::string& kind, const std::string& message) {
        std::lock_guard<std::mutex> lock(job->mu);
        job->ok = false;
        job->errKind = kind;
        job->errMessage = message;
        job->done = true;
        job->cv.notify_all();
    };

    const PreviewCaptureResult cap = capturePreview(preview);
    if (!cap.ok) {
        failJob("capture_failed", "preview FBO readback failed");
        return;
    }

    DocumentSession& session = *job->session;
    if (job->hasOrbit) {
        session.stickyYawDeg = preview.yawDeg();
        session.stickyPitchDeg = preview.pitchDeg();
    } else {
        session.stickyYawDeg = preview.yawDeg();
        session.stickyPitchDeg = preview.pitchDeg();
    }
    session.stickyDistance = preview.distance();

    json data;
    if (job->isReference) {
        const auto bg = previewClearRgb8();
        const SilhouetteMetrics modelM =
            silhouetteMetrics(cap.pixels, cap.width, cap.height, bg[0], bg[1], bg[2]);
        const auto refBg = estimateBackground(job->refPixels, job->refW, job->refH);
        const SilhouetteMetrics refM =
            silhouetteMetrics(job->refPixels, job->refW, job->refH, refBg[0], refBg[1], refBg[2]);
        const SideBySideImage sbs =
            composeSideBySide(cap.pixels, cap.width, cap.height, job->refPixels, job->refW, job->refH);
        if (!sbs.ok || !writePngRgba(job->outPath.c_str(), sbs.width, sbs.height, sbs.pixels)) {
            failJob("capture_failed", "side-by-side compose/write failed for " + job->outPath);
            return;
        }
        data = {{"path", job->outPath},
                {"width", sbs.width},
                {"height", sbs.height},
                {"node", job->node},
                {"model", silhouetteJson(modelM)},
                {"reference", silhouetteJson(refM)},
                {"reference_background", refBg},
                {"ms", job->stats.value("ms", 0.0)},
                {"stats", job->stats},
                {"diagnostics", job->diagnostics},
                {"camera", cameraJson(preview, job->hasTargetBBox, job->targetBMin, job->targetBMax)},
                {"cache", {{"hits", job->cacheHits}, {"misses", job->cacheMisses}}}};
        if (cap.sizeClamped) data["size_clamped"] = true;
        data["reloaded"] = job->reloaded;
        if (job->reloaded) data["load_diagnostics"] = job->loadDiagnostics;
    } else {
        if (job->writePng && !job->outPath.empty()) {
            if (!writePngRgba(job->outPath.c_str(), cap.width, cap.height, cap.pixels)) {
                failJob("capture_failed", "writePngRgba failed for " + job->outPath);
                return;
            }
        }
        data = {{"width", cap.width},
                {"height", cap.height},
                {"node", job->node},
                {"frame", "preview"},
                {"chrome", job->chromeEcho},
                {"stats", job->stats},
                {"diagnostics", job->diagnostics},
                {"camera", cameraJson(preview, job->hasTargetBBox, job->targetBMin, job->targetBMax)},
                {"render_state", renderStateJson(preview, job->targetSpec, job->chromeEcho)},
                {"cache", {{"hits", job->cacheHits}, {"misses", job->cacheMisses}}}};
        if (job->writePng && !job->outPath.empty()) data["path"] = job->outPath;
        if (cap.sizeClamped) data["size_clamped"] = true;
        data["reloaded"] = job->reloaded;
        if (job->reloaded) data["load_diagnostics"] = job->loadDiagnostics;

        const std::string frameKey = currentFrameKey(job->node, preview, job->opts);
        auto packCompare = [&](const std::vector<std::uint8_t>& prev, int prevW, int prevH,
                               const std::string& missing) -> json {
            json cmp;
            if (prev.empty()) {
                cmp = {{"available", false}, {"reason", missing}};
            } else if (prevW != cap.width || prevH != cap.height) {
                cmp = {{"available", false},
                       {"reason", "size mismatch (prev " + std::to_string(prevW) + "x" +
                                      std::to_string(prevH) + ", now " + std::to_string(cap.width) + "x" +
                                      std::to_string(cap.height) + ")"}};
            } else {
                const FrameCompareResult d = compareFrames(prev, cap.pixels, cap.width, cap.height);
                cmp = {{"available", true},
                       {"changed_pct", d.changedPct},
                       {"change_bbox_px", {d.changeX0, d.changeY0, d.changeX1, d.changeY1}}};
                const std::filesystem::path diffPath =
                    serveRepoRoot() / "tmp" / "pgg_rpc_shots" /
                    ("diff_" + std::to_string(++m_diffCounter) + ".png");
                std::error_code ec;
                std::filesystem::create_directories(diffPath.parent_path(), ec);
                if (writePngRgba(diffPath.string().c_str(), cap.width, cap.height, d.diffPixels))
                    cmp["diff_png"] = diffPath.string();
                else
                    cmp["diff_png"] = nullptr;
            }
            return cmp;
        };

        if (job->comparePrev) {
            if (session.lastFrameKey != frameKey)
                data["compare"] = packCompare({}, 0, 0, "no previous frame");
            else
                data["compare"] =
                    packCompare(session.lastFramePixels, session.lastFrameW, session.lastFrameH,
                                "no previous frame");
        }
        if (job->compareBaseline) {
            auto& slots = session.baselineFrames();
            auto it = slots.find(frameKey);
            if (it == slots.end()) {
                if (job->saveBaseline || true) {
                    // First compare=baseline without a slot creates it (same as the viewer).
                    slots[frameKey] = BaselineFrame{cap.width, cap.height, cap.pixels};
                    data["compare"] = {{"available", false},
                                       {"reason", "no previous frame"},
                                       {"baseline_created", true}};
                }
            } else {
                json cmp = packCompare(it->second.pixels, it->second.w, it->second.h, "no previous frame");
                if (job->saveBaseline) {
                    it->second = BaselineFrame{cap.width, cap.height, cap.pixels};
                    cmp["baseline_created"] = false;
                }
                data["compare"] = std::move(cmp);
            }
        } else if (job->saveBaseline) {
            const std::string key = frameKey;
            session.baselineFrames()[key] = BaselineFrame{cap.width, cap.height, cap.pixels};
        }

        session.lastFrameKey = frameKey;
        session.lastFrameW = cap.width;
        session.lastFrameH = cap.height;
        session.lastFramePixels = cap.pixels;
    }

    std::lock_guard<std::mutex> lock(job->mu);
    job->ok = true;
    job->data = std::move(data);
    job->done = true;
    job->cv.notify_all();
}
