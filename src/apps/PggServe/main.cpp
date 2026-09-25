// PggServe: GPU daemon for agent RPC (docs/pgg/serve_rpc.md).
//   PggServe [--port=9878] [--host=127.0.0.1]
//   PggServe --smoke
// Tiny Sokol window is the GPU context only (no ImGui, no graph). Slots are
// keyed by the canonical .pgg path; Engine work on different files runs in
// parallel, PNG capture is serialized on the Sokol thread.

#include "pch.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "GeometryPreview.h"
#include "ServeRpcServer.h"
#include "ServeRuntime.h"
#include "SmokeTest.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>

#if defined(SOKOL_METAL) && defined(__APPLE__)
    #import <Foundation/Foundation.h>
#endif

namespace {

ServeRpcServer* g_rpc = nullptr;
ServeRuntime* g_runtime = nullptr;
GeometryPreview g_preview;
bool g_gfxOk = false;
std::atomic<bool> g_frameLoopAlive{false};
uint16_t g_port = ServeRpcServer::kDefaultPort;
std::string g_host = "127.0.0.1";

void init() {
    g_frameLoopAlive.store(true, std::memory_order_relaxed);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("PggServe: init()");
    stm_setup();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_gfxOk = sg_isvalid();
    if (!g_gfxOk) {
        spdlog::error("PggServe: sg_setup FAILED");
        return;
    }
    g_preview.init();
    if (g_runtime) {
        g_runtime->startTimeSec = wallNowSec();
        g_runtime->setGpuReady(true);
    }

    if (g_rpc && !g_rpc->running()) {
        if (!g_rpc->start(g_host, g_port)) {
            spdlog::error("PggServe: cannot listen on {}:{}", g_host, g_port);
            sapp_quit();
            return;
        }
    }
#if defined(SOKOL_METAL) && defined(__APPLE__)
    [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated
                                                     reason:@"PggServe RPC server"];
#endif
}

void frame() {
    if (g_rpc) g_rpc->poll();
    if (!g_gfxOk || !g_runtime) return;

    g_runtime->beginGpuFrame(g_preview);

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.1f, 0.11f, 0.13f, 1.0f};
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();

    g_runtime->finishGpuFrame(g_preview);
}

void cleanup() {
    if (g_runtime) {
        g_runtime->setGpuReady(false);
        g_runtime->stopWorkers();
    }
    if (g_rpc) g_rpc->stop();
    g_preview.shutdown();
    if (sg_isvalid()) sg_shutdown();
}

void event(const sapp_event*) {}

}  // namespace

int main(int argc, char* argv[]) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--smoke") {
            smoke = true;
        } else if (arg.rfind("--port=", 0) == 0) {
            g_port = static_cast<uint16_t>(std::atoi(arg.substr(7).c_str()));
        } else if (arg.rfind("--host=", 0) == 0) {
            g_host = arg.substr(7);
        } else if (arg == "--help" || arg == "-h") {
            spdlog::info("PggServe [--port=9878] [--host=127.0.0.1] | PggServe --smoke");
            return 0;
        }
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        return runPggServeSmokeTest() ? 0 : 1;
    }

    ServeRpcServer rpc;
    ServeRuntime runtime(rpc);
    runtime.startWorkers();
    runtime.registerHandlers();
    g_rpc = &rpc;
    g_runtime = &runtime;

    // Listen and pump RPC ahead of the Sokol run loop: the first frame (which
    // runs init()) may never arrive when the window cannot become visible
    // (background session, locked screen). CPU-side ops work without the GPU;
    // render/reference answer no_gpu until a frame sets gpuReady.
    if (!rpc.start(g_host, g_port)) {
        spdlog::error("PggServe: cannot listen on {}:{}", g_host, g_port);
        return 1;
    }
    std::thread rpcPump([&rpc] {
        while (!g_frameLoopAlive.load(std::memory_order_relaxed) && rpc.running()) {
            rpc.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 64;
    desc.height = 64;
    desc.sample_count = 1;
    desc.window_title = "PggServe";
    desc.high_dpi = false;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;
    sapp_run(&desc);
    if (rpcPump.joinable()) {
        rpcPump.join();
    }
    g_rpc = nullptr;
    g_runtime = nullptr;
    return 0;
}
