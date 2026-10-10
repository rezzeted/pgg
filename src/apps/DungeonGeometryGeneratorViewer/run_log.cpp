#include "pch.h"

#include "run_log.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <utility>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#if defined(__unix__) || defined(__APPLE__)
    #include <execinfo.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <unistd.h>
#else
    #include <process.h>
#endif

#include "level.h"    // resolve_dungeon_geometry_generator_assets
#include "project.h"  // Project, save_project

namespace {

// Newest files kept in the log dir (run logs + project snapshots together).
constexpr size_t kKeepFiles = 48;

std::string g_dir;
std::string g_path;
std::string g_stamp;  // run id shared by the log and its snapshots
#if defined(__unix__) || defined(__APPLE__)
int g_crashFd = -1;
std::string g_crashTail;  // preformatted " — run log <path>" (async-signal-safe reuse)
#endif

int pidOf() {
#if defined(_WIN32)
    return _getpid();
#else
    return (int)getpid();
#endif
}

std::string runStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d_%02d-%02d-%02d_pid%d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, pidOf());
    return buf;
}

// The repo root is the parent of the located assets dir (the codes.pgg
// walk-up); a portable build with no assets around falls back to temp.
std::string resolveLogDir(const std::string& argv0) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string assets = resolve_dungeon_geometry_generator_assets(argv0, "");
    fs::path base;
    if (!assets.empty()) {
        base = fs::path(assets).parent_path() / "logs" / "DungeonGeometryGeneratorViewer";
    } else {
        base = fs::temp_directory_path(ec) / "dgg-viewer-logs";
    }
    fs::create_directories(base, ec);
    if (ec) return {};
    return base.string();
}

// Bounded dir: drop everything past the newest kKeepFiles entries.
void pruneOld(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec)) files.emplace_back(e.last_write_time(ec), e.path());
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = kKeepFiles; i < files.size(); ++i) fs::remove(files[i].second, ec);
}

#if defined(__unix__) || defined(__APPLE__)
void writeAll(int fd, const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    while (n > 0) {
        const ssize_t w = write(fd, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

const char* sigName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGBUS: return "SIGBUS";
        default: return "signal";
    }
}

void onCrash(int sig, siginfo_t*, void*) {
    // backtrace()/snprintf are not async-signal-safe by the letter — the
    // accepted trade for a usable stack (backtrace's lazy init is warmed up
    // at install time). Only static buffers and preformatted strings here.
    void* frames[64];
    const int n = backtrace(frames, 64);
    if (g_crashFd >= 0) {
        writeAll(g_crashFd, "\n=== CRASH ");
        writeAll(g_crashFd, sigName(sig));
        writeAll(g_crashFd, g_crashTail.c_str());
        backtrace_symbols_fd(frames, n, g_crashFd);
    }
    writeAll(STDERR_FILENO, "\n=== CRASH ");
    writeAll(STDERR_FILENO, sigName(sig));
    writeAll(STDERR_FILENO, g_crashTail.c_str());
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    signal(sig, SIG_DFL);  // die properly (exit status, OS crash reporters)
    raise(sig);
}

void installCrashHandler() {
    void* warm[4];
    backtrace(warm, 4);  // resolve the lazy init outside the signal context
    g_crashFd = open(g_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    g_crashTail = " — run log " + g_path + "\n";
    struct sigaction sa = {};
    sa.sa_sigaction = onCrash;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (int sig : {SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS}) sigaction(sig, &sa, nullptr);
}
#else
void installCrashHandler() {}  // Windows: no in-process handler (WER/minidumps are a separate concern)
#endif

}  // namespace

void runlogInit(const std::string& argv0) {
    if (!g_dir.empty()) return;
    g_dir = resolveLogDir(argv0);
    if (g_dir.empty()) return;
    g_stamp = runStamp();
    g_path = g_dir + "/run_" + g_stamp + ".log";
    try {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(g_path, true);
        auto outSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>(
            "dgg-viewer", spdlog::sinks_init_list{outSink, fileSink});
        logger->flush_on(spdlog::level::trace);  // every line must survive a crash
        spdlog::set_default_logger(logger);
    } catch (const spdlog::spdlog_ex&) {
        g_dir.clear();
        return;  // stdout-only fallback
    }
    pruneOld(g_dir);
    installCrashHandler();
    spdlog::info("DungeonGeometryGeneratorViewer: run log {}", g_path);
}

const std::string& runlogPath() { return g_path; }
const std::string& runlogDir() { return g_dir; }

void runlogSnapshotProject(const dungeon_geometry_generator::Project& project, int genIndex) {
    if (g_stamp.empty()) return;
    char suffix[40];
    std::snprintf(suffix, sizeof(suffix), ".gen%02d.project.json", genIndex);
    const std::string snap = g_dir + "/run_" + g_stamp + suffix;
    std::string err;
    if (!dungeon_geometry_generator::save_project(snap, project, err)) {
        spdlog::warn("DungeonGeometryGeneratorViewer: generate snapshot failed: {}", err);
        return;
    }
    spdlog::info("DungeonGeometryGeneratorViewer: generate snapshot {}", snap);
}
