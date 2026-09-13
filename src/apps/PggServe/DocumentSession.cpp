#include "pch.h"

#include "DocumentSession.h"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

#include <pgg/src/eval/expand.h>
#include <pgg/src/eval/fingerprint.h>
#include <pgg/src/eval/geometry.h>
#include <pgg/src/eval/sdf.h>
#include <pgg/src/eval/typecheck.h>

#include "ServeRpcServer.h"

namespace {

std::string slashToDot(std::string path) {
    for (char& c : path)
        if (c == '/') c = '.';
    return path;
}

bool resolveGroupBBox(const std::map<std::string, std::pair<glm::vec3, glm::vec3>>& boxes,
                      const std::string& name, glm::vec3& outMin, glm::vec3& outMax,
                      std::string& err) {
    auto found = boxes.end();
    if (const auto it = boxes.find(name); it != boxes.end()) {
        found = it;
    } else {
        for (auto jt = boxes.begin(); jt != boxes.end(); ++jt) {
            const size_t colon = jt->first.find(':');
            const std::string bare = colon == std::string::npos ? jt->first : jt->first.substr(colon + 1);
            if (bare != name) continue;
            if (found != boxes.end())
                spdlog::warn("PggServe: target group '{}' is ambiguous (taking '{}', also '{}')", name,
                             found->first, jt->first);
            else
                found = jt;
        }
    }
    if (found == boxes.end()) {
        std::string known;
        for (const auto& [key, bb] : boxes) known += (known.empty() ? "" : ", ") + key;
        err = "group '" + name + "' is not on the geometry (groups: " +
              (known.empty() ? std::string("none") : known) + ")";
        return false;
    }
    outMin = found->second.first;
    outMax = found->second.second;
    return true;
}

}  // namespace

std::filesystem::path serveRepoRoot() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec) return ".";
    for (int i = 0; i < 12; ++i) {
        if (std::filesystem::exists(dir / ".git", ec)) return dir;
        if (!dir.has_parent_path() || dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return ".";
}

std::string resolveServePath(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (std::filesystem::is_regular_file(p, ec)) return p.string();
    if (!p.empty() && !p.is_absolute()) {
        const std::filesystem::path fromRepo = serveRepoRoot() / p;
        if (std::filesystem::is_regular_file(fromRepo, ec)) return fromRepo.string();
    }
    return path;
}

std::string canonicalServePath(const std::string& path) {
    const std::string resolved = resolveServePath(path);
    std::error_code ec;
    const std::filesystem::path canon =
        std::filesystem::weakly_canonical(std::filesystem::path(resolved), ec);
    return ec ? resolved : canon.string();
}

std::int64_t fileMtimeNs(const std::string& path) {
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

double wallNowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool diagsHaveErrors(const std::vector<pgg::Diagnostic>& diags) {
    for (const pgg::Diagnostic& d : diags)
        if (!d.isWarning) return true;
    return false;
}

nlohmann::json diagnosticsJson(const std::vector<pgg::Diagnostic>& diags) {
    nlohmann::json arr = nlohmann::json::array();
    for (const pgg::Diagnostic& d : diags) {
        nlohmann::json j = {{"code", d.code},      {"line", d.span.line}, {"col", d.span.col},
                            {"warning", d.isWarning}, {"message", d.message}};
        if (!d.hint.empty()) j["hint"] = d.hint;
        arr.push_back(std::move(j));
    }
    return arr;
}

nlohmann::json fingerprintJson(const std::optional<uint64_t>& fp) {
    if (!fp) return nullptr;
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(*fp));
    return buf;
}

nlohmann::json vec3Json(const glm::vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

nlohmann::json valueStatsJson(const pgg::Value& v, double ms) {
    nlohmann::json s;
    const pgg::ScalarType base = pgg::valueBase(v);
    s["kind"] = pgg::scalarName(base);
    s["pts"] = 0;
    s["tri"] = 0;
    s["groups"] = nlohmann::json::array();
    if (base == pgg::ScalarType::Geo) {
        const pgg::Geo& g = *pgg::asGeo(v);
        s["kind"] = pgg::geoKindName(g.kind);
        s["pts"] = g.pointCount();
        size_t tri = 0;
        if (g.kind == pgg::GeoKind::Mesh && g.faceOffsets)
            for (size_t f = 0; f < g.faceCount(); ++f) {
                const int32_t corners = (*g.faceOffsets)[f + 1] - (*g.faceOffsets)[f];
                if (corners >= 3) tri += static_cast<size_t>(corners - 2);
            }
        s["tri"] = tri;
        if (g.pointCount() > 0) {
            glm::vec3 mn, mx;
            pgg::geoBBox(g, mn, mx);
            s["bbox"] = {{"min", vec3Json(mn)}, {"max", vec3Json(mx)}};
        }
        std::vector<std::string> groups;
        for (pgg::Domain d : {pgg::Domain::Points, pgg::Domain::Corners, pgg::Domain::Faces}) {
            const pgg::GroupSet* gs = g.groups(d);
            if (!gs) continue;
            for (const auto& [name, column] : gs->columns)
                groups.push_back(std::string(pgg::domainName(d)) + ":" + name);
        }
        std::sort(groups.begin(), groups.end());
        groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
        s["groups"] = groups;
    } else if (base == pgg::ScalarType::Sdf) {
        glm::vec3 mn, mx;
        pgg::asSdf(v)->conservativeBBox(mn, mx);
        if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z)
            s["bbox"] = {{"min", vec3Json(mn)}, {"max", vec3Json(mx)}};
    }
    s["ms"] = ms;
    return s;
}

nlohmann::json silhouetteJson(const SilhouetteMetrics& m) {
    return {{"bbox_frac", {m.bboxX0, m.bboxY0, m.bboxX1, m.bboxY1}},
            {"w_over_h", m.wOverH},
            {"rows", m.rows},
            {"empty", m.empty}};
}

std::array<std::uint8_t, 3> previewClearRgb8() {
    std::array<std::uint8_t, 3> bg{};
    for (int i = 0; i < 3; ++i)
        bg[i] = static_cast<std::uint8_t>(
            std::clamp<long>(std::lround(GeometryPreview::kClearColor[i] * 255.0f), 0, 255));
    return bg;
}

pgg::Value parseCliValue(const std::string& v) {
    if (v == "true") return pgg::Value(true);
    if (v == "false") return pgg::Value(false);
    if (v.size() >= 5 && v.front() == '(' && v.back() == ')') {
        std::vector<float> comps;
        std::stringstream ss(v.substr(1, v.size() - 2));
        std::string item;
        bool ok = true;
        while (std::getline(ss, item, ',')) {
            char* end = nullptr;
            const float f = std::strtof(item.c_str(), &end);
            if (end == item.c_str() || *end != '\0') ok = false;
            comps.push_back(f);
        }
        if (ok && comps.size() == 2) return pgg::Value(glm::vec2(comps[0], comps[1]));
        if (ok && comps.size() == 3) return pgg::Value(glm::vec3(comps[0], comps[1], comps[2]));
        if (ok && comps.size() == 4) return pgg::Value(glm::vec4(comps[0], comps[1], comps[2], comps[3]));
        return pgg::Value(v);
    }
    char* end = nullptr;
    const long long iv = std::strtoll(v.c_str(), &end, 10);
    if (end && *end == '\0' && end != v.c_str()) return pgg::Value(static_cast<int64_t>(iv));
    const float fv = std::strtof(v.c_str(), &end);
    if (end && *end == '\0' && end != v.c_str()) return pgg::Value(fv);
    return pgg::Value(v);
}

std::string jsonToParamText(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
    if (v.is_number_float()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v.get<double>());
        return buf;
    }
    if (v.is_array()) {
        std::string out = "(";
        for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + jsonToParamText(v[i]);
        return out + ")";
    }
    return v.dump();
}

std::string literalText(const pgg::Expr* e) {
    if (!e) return {};
    switch (e->kind) {
        case pgg::NodeKind::NumberLit: return static_cast<const pgg::NumberLit*>(e)->text;
        case pgg::NodeKind::StringLit: return static_cast<const pgg::StringLit*>(e)->value;
        case pgg::NodeKind::BoolLit:
            return static_cast<const pgg::BoolLit*>(e)->value ? "true" : "false";
        default: return {};
    }
}

CameraTargetSpec parseCameraTargetSpec(const std::string& text) {
    CameraTargetSpec spec;
    spec.raw = text;
    if (text.rfind("group:", 0) == 0) {
        const std::string rest = text.substr(6);
        const size_t at = rest.find('@');
        if (at != std::string::npos) {
            spec.kind = CameraTargetSpec::Kind::GroupOnBinding;
            spec.name = rest.substr(0, at);
            spec.binding = slashToDot(rest.substr(at + 1));
        } else {
            spec.kind = CameraTargetSpec::Kind::Group;
            spec.name = rest;
        }
    } else if (text.rfind("binding:", 0) == 0) {
        spec.kind = CameraTargetSpec::Kind::Binding;
        spec.name = slashToDot(text.substr(8));
    } else {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (std::sscanf(text.c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
            spec.kind = CameraTargetSpec::Kind::Point;
            spec.point = glm::vec3(x, y, z);
        }
    }
    if (spec.kind == CameraTargetSpec::Kind::GroupOnBinding &&
        (spec.name.empty() || spec.binding.empty()))
        spec.kind = CameraTargetSpec::Kind::None;
    if (spec.kind != CameraTargetSpec::Kind::Point && spec.kind != CameraTargetSpec::Kind::GroupOnBinding &&
        spec.name.empty())
        spec.kind = CameraTargetSpec::Kind::None;
    return spec;
}

std::string currentFrameKey(const std::string& node, const GeometryPreview& preview,
                             const PreviewBuildOptions& opts) {
    const nlohmann::json k = {{"node", node},
                              {"frame", "preview"},
                              {"chrome", "off"},
                              {"center", vec3Json(preview.center())},
                              {"radius", preview.fitRadius()},
                              {"distance", preview.distance()},
                              {"yaw", preview.yawDeg()},
                              {"pitch", preview.pitchDeg()},
                              {"projection", static_cast<int>(preview.projection())},
                              {"fit", preview.fitMode() == PreviewFitMode::Target ? "target" : "all"},
                              {"has_target", preview.hasTarget()},
                              {"highlight", opts.highlightGroup},
                              {"shading", static_cast<int>(opts.shading)},
                              {"colors", opts.vertexColors},
                              {"sdf_res", opts.sdfResolution},
                              {"wire", preview.wireframe()}};
    return k.dump();
}

nlohmann::json cameraJson(const GeometryPreview& preview, bool hasBBox, const glm::vec3& bmin,
                          const glm::vec3& bmax) {
    nlohmann::json cam = {{"center", vec3Json(preview.center())},
                          {"radius", preview.fitRadius()},
                          {"distance", preview.distance()}};
    if (hasBBox) {
        const glm::vec3 c = (bmin + bmax) * 0.5f;
        cam["target_bbox"] = {{"min", vec3Json(bmin)}, {"max", vec3Json(bmax)}, {"center", vec3Json(c)}};
    }
    return cam;
}

nlohmann::json renderStateJson(const GeometryPreview& preview, const CameraTargetSpec& target,
                              const std::string& chromeEcho) {
    std::string ortho = "off";
    switch (preview.projection()) {
        case PreviewProjection::OrthoFront: ortho = "front"; break;
        case PreviewProjection::OrthoSide: ortho = "side"; break;
        case PreviewProjection::OrthoTop: ortho = "top"; break;
        default: break;
    }
    return {{"wire", preview.wireframe()},
            {"chrome", chromeEcho},
            {"ortho", ortho},
            {"target", target.raw},
            {"zoom", preview.fitZoom()},
            {"fit", preview.fitMode() == PreviewFitMode::Target ? "target" : "all"}};
}

DocumentSession::DocumentSession(std::string canonicalPath)
    : m_canonical(std::move(canonicalPath)), m_cache(std::make_unique<pgg::MemoryCache>(kCacheCapacity)) {}

void DocumentSession::clearVisualState() {
    m_baselineFrames.clear();
    lastFrameKey.clear();
    lastFrameW = lastFrameH = 0;
    lastFramePixels.clear();
}

std::vector<std::string> DocumentSession::importRoots() const {
    std::vector<std::string> roots = m_extraRoots;
    pgg::appendImportRoot(roots, pgg::findProductLibRoot(m_resolvedPath.empty() ? serveRepoRoot().string()
                                                                               : m_resolvedPath));
    pgg::appendImportRoot(roots, pgg::findProductLibRoot(serveRepoRoot().string()));
    if (!m_resolvedPath.empty())
        pgg::appendImportRoot(roots, std::filesystem::path(m_resolvedPath).parent_path().string());
    return roots;
}

pgg::RunParams DocumentSession::makeRunParams() const {
    pgg::RunParams rp;
    for (const auto& [name, text] : m_paramValues)
        if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
    rp.importRoots = importRoots();
    rp.cache = m_cache.get();
    rp.profile = true;
    return rp;
}

std::vector<std::string> DocumentSession::boundParamNames() const {
    std::vector<std::string> names;
    for (const auto& [name, text] : m_paramValues)
        if (!text.empty()) names.push_back(name);
    return names;
}

nlohmann::json DocumentSession::paramsJson() const {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, text] : m_paramValues) out[name] = text;
    return out;
}

void DocumentSession::setParams(const nlohmann::json& args, nlohmann::json& unknown) {
    unknown = nlohmann::json::array();
    for (const auto& [name, val] : args.items()) {
        if (name == "file") continue;
        bool found = false;
        for (auto& [pname, ptext] : m_paramValues)
            if (pname == name) {
                ptext = jsonToParamText(val);
                found = true;
            }
        if (!found) unknown.push_back(name);
    }
}

nlohmann::json DocumentSession::viewsJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const NamedView& v : m_namedViews) arr.push_back(v.fields);
    return {{"file", m_canonical}, {"views", arr}, {"session", sessionEcho()}};
}

nlohmann::json DocumentSession::mergeNamedViewArgs(const nlohmann::json& args, std::string& err) const {
    nlohmann::json out = args;
    if (!args.contains("view")) return out;
    const std::string name = args.value("view", std::string{});
    const NamedView* found = nullptr;
    for (const NamedView& v : m_namedViews)
        if (v.name == name) {
            found = &v;
            break;
        }
    if (!found) {
        std::string known;
        for (const NamedView& v : m_namedViews) known += (known.empty() ? "" : ", ") + v.name;
        err = "unknown view '" + name + "' (views: " + (known.empty() ? std::string("none") : known) + ")";
        return {};
    }
    for (auto it = found->fields.begin(); it != found->fields.end(); ++it) {
        if (it.key() == "name") continue;
        if (!args.contains(it.key()) || args[it.key()].is_null()) out[it.key()] = it.value();
    }
    out.erase("view");
    return out;
}

void DocumentSession::loadNamedViews(const std::string& pggPath) {
    m_namedViews.clear();
    const std::filesystem::path p(pggPath);
    const std::filesystem::path viewsPath = p.parent_path() / (p.stem().string() + ".views.json");
    std::ifstream in(viewsPath, std::ios::binary);
    if (!in) return;
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        spdlog::warn("PggServe: cannot parse {}: {}", viewsPath.string(), e.what());
        return;
    }
    if (!j.is_array()) {
        spdlog::warn("PggServe: {} must be a JSON array of view objects", viewsPath.string());
        return;
    }
    for (const nlohmann::json& item : j) {
        if (!item.is_object()) continue;
        const std::string name = item.value("name", std::string{});
        if (name.empty()) continue;
        NamedView v;
        v.name = name;
        v.fields = item;
        m_namedViews.push_back(std::move(v));
    }
}

void DocumentSession::recordMtimes() {
    m_fileMtimes.clear();
    m_fileMtimes[m_canonical] = fileMtimeNs(m_canonical);
    if (m_closure)
        for (const pgg::ModuleInfo* m : m_closure->modules)
            m_fileMtimes[m->canonicalPath] = fileMtimeNs(m->canonicalPath);
}

bool DocumentSession::loadFromDisk(const std::string& resolvedPath, const std::vector<std::string>& extraRoots,
                                   bool fromRpcSource, bool explicitLoad) {
    std::ifstream in(resolvedPath, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();

    m_closure.reset();
    m_doc = pgg::Document{};
    m_doc = pgg::parse(ss.str(), resolvedPath);
    m_resolvedPath = resolvedPath;
    m_extraRoots = extraRoots;
    m_allDiags = m_doc.diagnostics;
    if (m_doc.file && pgg::hasImports(*m_doc.file)) {
        std::vector<pgg::Diagnostic> diags;
        m_closure = std::make_unique<pgg::ModuleClosure>(
            pgg::loadModuleClosure(*m_doc.file, importRoots(), diags));
        m_allDiags.insert(m_allDiags.end(), diags.begin(), diags.end());
    }
    m_paramValues.clear();
    if (m_doc.file) {
        for (const pgg::Node* item : m_doc.file->items) {
            if (item->kind != pgg::NodeKind::ParamDecl) continue;
            const auto* p = static_cast<const pgg::ParamDecl*>(item);
            m_paramValues.push_back({p->name, p->hasDefault ? literalText(p->def) : std::string{}});
        }
    }
    mainFileFromRpcSource = fromRpcSource;
    m_bindingTargetResolved = false;
    m_bindingTargetPath.clear();
    m_bindingTargetGeo = PreviewGeometry{};
    previewHasValue = false;
    previewTarget.clear();
    lastNode.clear();
    recordMtimes();
    loadNamedViews(resolvedPath);
    if (explicitLoad) {
        clearVisualState();
        resetDiffSnapshot();
        stickyYawDeg.reset();
        stickyPitchDeg.reset();
        stickyDistance.reset();
    }
    return true;
}

nlohmann::json DocumentSession::staticCheck() const {
    const double t0 = wallNowSec();
    std::vector<pgg::Diagnostic> diags = m_doc.diagnostics;
    if (m_doc.file && !m_doc.hasErrors()) {
        pgg::ModuleClosure closure;
        const pgg::ModuleClosure* closurePtr = nullptr;
        if (pgg::hasImports(*m_doc.file)) {
            std::vector<pgg::Diagnostic> importDiags;
            closure = pgg::loadModuleClosure(*m_doc.file, importRoots(), diags);
            closurePtr = &closure;
        }
        pgg::FlatProgram flat = pgg::expandProgram(*m_doc.file, closurePtr, diags);
        if (!diagsHaveErrors(diags)) {
            std::vector<size_t> runtimeContracts;
            pgg::typecheckFlat(flat, boundParamNames(), diags, runtimeContracts);
        }
    }
    return {{"diagnostics", diagnosticsJson(diags)},
            {"has_errors", diagsHaveErrors(diags)},
            {"ms", (wallNowSec() - t0) * 1000.0}};
}

AutoReloadResult DocumentSession::autoReloadIfChanged() {
    AutoReloadResult out;
    if (mainFileFromRpcSource || m_resolvedPath.empty() || m_fileMtimes.empty()) return out;
    bool changed = false;
    for (const auto& [path, mtime] : m_fileMtimes)
        if (fileMtimeNs(path) != mtime) {
            changed = true;
            break;
        }
    if (!changed) return out;
    spdlog::info("PggServe: auto-reload {} (a watched file changed on disk)", m_canonical);
    if (!loadFromDisk(m_resolvedPath, m_extraRoots, false, false))
        ServeRpcServer::fail("io_error", "auto-reload: cannot open " + m_resolvedPath);
    out.reloaded = true;
    out.diags = m_allDiags;
    if (diagsHaveErrors(m_allDiags)) {
        std::string why = "auto-reload of " + m_canonical + " has errors (run skipped):";
        for (const pgg::Diagnostic& d : m_allDiags)
            if (!d.isWarning) why += "\n" + pgg::formatDiagnostic(d, m_canonical);
        ServeRpcServer::fail("run_errors", why);
    }
    return out;
}

bool DocumentSession::runOutputsFingerprints(
    std::vector<std::pair<std::string, std::optional<uint64_t>>>& outFps, double& outMs,
    std::string& why) {
    pgg::RunParams rp = makeRunParams();
    const double t0 = wallNowSec();
    pgg::RunResult r = pgg::runFile(m_resolvedPath, rp);
    outMs = (wallNowSec() - t0) * 1000.0;
    lastCacheHits = r.stats.cacheHits;
    lastCacheMisses = r.stats.cacheMisses;
    lastProfile = r.stats.profile;
    if (r.hasErrors()) {
        for (const pgg::Diagnostic& d : r.diagnostics)
            if (!d.isWarning) why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
        return false;
    }
    outFps.clear();
    for (const pgg::RunOutput& o : r.outputs) {
        uint64_t fp = 0;
        std::optional<uint64_t> v;
        if (pgg::fingerprintValue(o.value, fp)) v = fp;
        outFps.push_back({o.name, v});
    }
    return true;
}

DocumentSession::PullResult DocumentSession::pullGeometry(const std::string& node) {
    PullResult out;
    pgg::RunParams rp = makeRunParams();
    rp.pulls = {node};
    const double t0 = wallNowSec();
    pgg::RunResult r = pgg::runFile(m_resolvedPath, rp);
    out.ms = (wallNowSec() - t0) * 1000.0;
    out.diags = r.diagnostics;
    out.profile = r.stats.profile;
    out.cacheHits = r.stats.cacheHits;
    out.cacheMisses = r.stats.cacheMisses;
    lastCacheHits = out.cacheHits;
    lastCacheMisses = out.cacheMisses;
    lastProfile = out.profile;
    lastNode = node;
    previewTarget = node;
    for (const pgg::RunOutput& o : r.pulled) {
        const pgg::ScalarType base = pgg::valueBase(o.value);
        if (base == pgg::ScalarType::Geo || base == pgg::ScalarType::Sdf) {
            out.value = o.value;
            out.ok = true;
            previewHasValue = true;
            return out;
        }
    }
    previewHasValue = false;
    std::string why;
    for (const pgg::Diagnostic& d : r.diagnostics) {
        if (d.isWarning) continue;
        why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
    }
    if (why.empty())
        why = r.pulled.empty()
                  ? "no value"
                  : "value has no geometry (" +
                        std::string(pgg::scalarName(pgg::valueBase(r.pulled[0].value))) + ")";
    out.error = why;
    return out;
}

pgg::RunResult DocumentSession::runProbes(const std::string& spec, double& ms) {
    pgg::RunParams rp = makeRunParams();
    rp.probes = {spec};
    const double t0 = wallNowSec();
    pgg::RunResult r = pgg::runFile(m_resolvedPath, rp);
    ms = (wallNowSec() - t0) * 1000.0;
    lastCacheHits = r.stats.cacheHits;
    lastCacheMisses = r.stats.cacheMisses;
    lastProfile = r.stats.profile;
    return r;
}

bool DocumentSession::resolveCameraTarget(const CameraTargetSpec& spec, const PreviewGeometry& geo,
                                            glm::vec3& outCenter, float& outRadius, bool& hasBBox,
                                            glm::vec3& bmin, glm::vec3& bmax, std::string& err) {
    hasBBox = false;
    err.clear();
    if (spec.kind == CameraTargetSpec::Kind::None) return true;

    auto remember = [&](const glm::vec3& mn, const glm::vec3& mx) {
        bmin = mn;
        bmax = mx;
        hasBBox = true;
        outCenter = (mn + mx) * 0.5f;
        outRadius = std::max(1e-3f, glm::length(mx - mn) * 0.5f);
    };

    if (spec.kind == CameraTargetSpec::Kind::Point) {
        outCenter = spec.point;
        outRadius = std::max(1e-3f, glm::length(geo.bmax - geo.bmin) * 0.5f);
        bmin = spec.point;
        bmax = spec.point;
        hasBBox = true;
        return true;
    }

    auto pullBinding = [&](const std::string& path) -> bool {
        if (m_bindingTargetResolved && m_bindingTargetPath == path) return true;
        m_bindingTargetResolved = false;
        m_bindingTargetPath = path;
        PullResult pulled = pullGeometry(path);
        if (!pulled.ok) {
            err = "binding '" + path + "' gave no geometry value" +
                  (pulled.error.empty() ? std::string{} : " (" + pulled.error + ")");
            return false;
        }
        m_bindingTargetGeo = buildPreviewGeometry(pulled.value, PreviewBuildOptions{});
        if (!m_bindingTargetGeo.ok) {
            err = "binding '" + path + "' has nothing to bound (" + m_bindingTargetGeo.summary + ")";
            return false;
        }
        m_bindingTargetResolved = true;
        return true;
    };

    if (spec.kind == CameraTargetSpec::Kind::Group) {
        glm::vec3 mn, mx;
        if (!resolveGroupBBox(geo.groupBBoxes, spec.name, mn, mx, err)) return false;
        remember(mn, mx);
        return true;
    }
    if (spec.kind == CameraTargetSpec::Kind::Binding) {
        if (!pullBinding(spec.name)) return false;
        remember(m_bindingTargetGeo.bmin, m_bindingTargetGeo.bmax);
        return true;
    }
    if (spec.kind == CameraTargetSpec::Kind::GroupOnBinding) {
        if (!pullBinding(spec.binding)) return false;
        glm::vec3 mn, mx;
        if (!resolveGroupBBox(m_bindingTargetGeo.groupBBoxes, spec.name, mn, mx, err)) {
            std::string known;
            for (const auto& [key, bb] : m_bindingTargetGeo.groupBBoxes)
                known += (known.empty() ? "" : ", ") + key;
            err = "group '" + spec.name + "' is not on binding '" + spec.binding +
                  "' (groups: " + (known.empty() ? std::string("none") : known) + ")";
            return false;
        }
        remember(mn, mx);
        return true;
    }
    err = "target '" + spec.raw + "' not resolved";
    return false;
}
