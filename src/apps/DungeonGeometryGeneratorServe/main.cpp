// DungeonGeometryGeneratorServe: CPU RPC daemon of the dungeon_geometry_generator pipeline (docs/dungeon_geometry_generator/mcp_v1.md) — project
// slots with the warm F8 unit cache; ops ping/status/load/validate/layout/ir/
// fill/check/export/units/provenance/asset_check.
//   DungeonGeometryGeneratorServe [--port N] [--host 127.0.0.1] [--assets <dir>]
//   DungeonGeometryGeneratorServe --smoke
// Default port 9879; env DUNGEON_GEOMETRY_GENERATOR_SERVE_PORT overrides the default (--port wins).
// No signal handlers (same convention as PggServe): the MCP layer manages the
// process.

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "ProjectSession.h"
#include "ServeRpcServer.h"
#include "ServeRuntime.h"
#include "SmokeTest.h"
#include "assets.h"

int main(int argc, char** argv) {
    uint16_t port = ServeRpcServer::kDefaultPort;
    std::string host = "127.0.0.1";
    std::string assets;
    bool smoke = false;
    bool portGiven = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        auto takeValue = [&](const char* flag, std::string& dst) -> bool {
            if (i + 1 >= argc) {
                spdlog::error("DungeonGeometryGeneratorServe: {} expects a value", flag);
                return false;
            }
            dst = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--smoke") {
            smoke = true;
        } else if (arg.rfind("--port=", 0) == 0) {
            port = static_cast<uint16_t>(std::atoi(arg.substr(7).c_str()));
            portGiven = true;
        } else if (arg == "--port") {
            if (!takeValue("--port", value)) return 2;
            port = static_cast<uint16_t>(std::atoi(value.c_str()));
            portGiven = true;
        } else if (arg.rfind("--host=", 0) == 0) {
            host = arg.substr(7);
        } else if (arg == "--host") {
            if (!takeValue("--host", host)) return 2;
        } else if (arg.rfind("--assets=", 0) == 0) {
            assets = arg.substr(9);
        } else if (arg == "--assets") {
            if (!takeValue("--assets", assets)) return 2;
        } else if (arg == "--help" || arg == "-h") {
            spdlog::info("DungeonGeometryGeneratorServe [--port N] [--host 127.0.0.1] [--assets <dir>] | DungeonGeometryGeneratorServe --smoke");
            return 0;
        } else {
            spdlog::error("DungeonGeometryGeneratorServe: unknown argument \"{}\"", arg);
            return 2;
        }
    }
    if (!portGiven) {
        if (const char* env = std::getenv("DUNGEON_GEOMETRY_GENERATOR_SERVE_PORT"); env && *env)
            port = static_cast<uint16_t>(std::atoi(env));
    }
    // The assets dir resolves once at startup.
    if (assets.empty()) assets = dungeon_geometry_generator::find_dungeon_geometry_generator_assets(argc > 0 ? argv[0] : "", "");

    spdlog::set_level(spdlog::level::info);
    if (smoke) return runDungeonGeometryGeneratorServeSmokeTest(argc > 0 ? argv[0] : "", assets) ? 0 : 1;

    ServeRpcServer rpc;
    ServeRuntime runtime(rpc);
    runtime.setAssetsDir(assets);
    runtime.startTimeSec = wallNowSec();
    runtime.startWorkers();
    runtime.registerHandlers();
    if (!rpc.start(host, port)) {
        spdlog::error("DungeonGeometryGeneratorServe: cannot listen on {}:{} (port busy?)", host, port);
        return 1;
    }
    if (assets.empty())
        spdlog::warn("DungeonGeometryGeneratorServe: no dungeon_geometry_generator assets dir found; fill/asset_check answer D100 "
                     "(pass --assets <dir>)");
    else
        spdlog::info("DungeonGeometryGeneratorServe: assets dir {}", assets);
    while (rpc.running()) {
        rpc.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return 0;
}
