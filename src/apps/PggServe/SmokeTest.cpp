#include "pch.h"

#include "SmokeTest.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ServeRpcServer.h"
#include "ServeRuntime.h"

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
#endif

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    if (ok) {
        spdlog::info("TEST PASS: {}", name);
    } else {
        spdlog::error("TEST FAIL: {}", name);
        ++g_failures;
    }
}

std::string findRepoRoot() { return serveRepoRoot().string(); }

#if defined(_WIN32)
using SocketFd = SOCKET;
static constexpr SocketFd kBadSock = INVALID_SOCKET;
static void closeFd(SocketFd fd) { closesocket(fd); }
#else
using SocketFd = int;
static constexpr SocketFd kBadSock = -1;
static void closeFd(SocketFd fd) { close(fd); }
#endif

SocketFd connectClient(const std::string& host, uint16_t port) {
#if defined(_WIN32)
    SOCKET cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd == INVALID_SOCKET) return kBadSock;
    const DWORD recvTimeoutMs = 30000;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
               sizeof(recvTimeoutMs));
#else
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) return kBadSock;
    const timeval recvTimeout{30, 0};
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
#if defined(SO_NOSIGPIPE)
    const int one = 1;
    setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeFd(cfd);
        return kBadSock;
    }
    return cfd;
}

nlohmann::json rpcCall(SocketFd fd, const nlohmann::json& req, std::string& inbuf) {
    const std::string line = req.dump() + "\n";
    if (send(fd, line.data(), static_cast<int>(line.size()), 0) < 0) return nlohmann::json{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
        const size_t nl = inbuf.find('\n');
        if (nl != std::string::npos) {
            const std::string resp = inbuf.substr(0, nl);
            inbuf.erase(0, nl + 1);
            return nlohmann::json::parse(resp, nullptr, false);
        }
        char buf[65536];
#if defined(_WIN32)
        const int n = recv(fd, buf, sizeof(buf), 0);
#else
        const ssize_t n = recv(fd, buf, sizeof(buf), 0);
#endif
        if (n > 0) {
            inbuf.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        }
    }
    return nlohmann::json{};
}

}  // namespace

bool runPggServeSmokeTest() {
    g_failures = 0;
    const std::string corpus = findRepoRoot() + "/src/tests/pgg/corpus";

    ServeRpcServer server;
    ServeRuntime runtime(server);
    runtime.startTimeSec = wallNowSec();
    runtime.startWorkers();
    runtime.registerHandlers();
    check(server.start("127.0.0.1", 0), "rpc server starts on an ephemeral port");
    const uint16_t port = server.listenPort();
    check(port != 0, "ephemeral listen port is non-zero");

    std::atomic<bool> polling{true};
    std::thread poller([&] {
        while (polling.load()) {
            server.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::string inbuf;
    SocketFd cfd = connectClient("127.0.0.1", port);
    check(cfd != kBadSock, "rpc client connects");

    auto call = [&](const nlohmann::json& req) { return rpcCall(cfd, req, inbuf); };

    if (cfd != kBadSock) {
        const nlohmann::json pong = call({{"op", "ping"}, {"args", nlohmann::json::object()}});
        check(pong.value("ok", false) && pong["data"].value("pong", false) &&
                  pong["data"].value("app", std::string{}) == "PggServe",
              "rpc ping -> pong app=PggServe");

        const nlohmann::json status0 = call({{"op", "status"}, {"args", nlohmann::json::object()}});
        check(status0.value("ok", false) && status0["data"].contains("slots") &&
                  status0["data"].contains("gpu"),
              "rpc status lists slots");

        const nlohmann::json bad =
            call({{"op", "load"}, {"args", {{"source", "= definitely not pgg (\n"}}}});
        check(bad.value("ok", false) && bad["data"].value("has_errors", false) &&
                  !bad["data"]["diagnostics"].empty() && bad["data"].contains("session"),
              "rpc load of an invalid source answers diagnostics without a run");

        const nlohmann::json e609 = call(
            {{"op", "load"},
             {"args",
              {{"source",
                "def part(grp: string) -> (out: geo<mesh>) {\n"
                "    out = mark(box(size = vec3(1.0)), grp, where = true, domain = faces)\n"
                "}\n"
                "pts = mesh_line(count = 3, length = 2.0, dir = (1, 0, 0))\n"
                "bars = realize(instance_on_points(pts, source = part(grp = \"iron\")))\n"
                "pier = set(box(size = vec3(2.0)), \"tint\", vec3(0.5, 0.5, 0.5), domain = faces)\n"
                "scene = merge(pier, bars)\n"
                "output scene\n"}}}});
        bool e609seen = false;
        if (e609.value("ok", false) && e609["data"].contains("diagnostics"))
            for (const nlohmann::json& d : e609["data"]["diagnostics"])
                e609seen = e609seen || d.value("code", std::string{}) == "E609";
        check(e609seen && e609["data"].value("has_errors", false) && e609["data"].value("ms", 1000.0) < 1000.0,
              "rpc load catches E609 through a def instance source (static, ms-budget)");

        const nlohmann::json good = call({{"op", "load"}, {"args", {{"path", corpus + "/e1_rock.pgg"}}}});
        check(good.value("ok", false) && !good["data"].value("has_errors", true) &&
                  good["data"]["session"].contains("file"),
              "rpc load of a corpus file");
        const std::string rockFile = good["data"]["session"].value("file", std::string{});

        const nlohmann::json prm = call({{"op", "params"}, {"args", {{"seed", 7}}}});
        check(prm.value("ok", false) && prm["data"]["params"].value("seed", std::string{}) == "7",
              "rpc params sets a launch param");

        const nlohmann::json probe = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
        bool probeOk = probe.value("ok", false) && !probe["data"]["records"].empty();
        if (probeOk)
            probeOk = probe["data"]["records"][0]["text"].get<std::string>().find("mesh") != std::string::npos;
        check(probeOk, "rpc probe base:schema returns a record");

        const nlohmann::json render = call({{"op", "render"}, {"args", {{"node", "base"}}}});
        check(!render.value("ok", true) && render["error"].value("kind", std::string{}) == "no_gpu",
              "rpc render fails headless with no_gpu");

        const nlohmann::json spire =
            call({{"op", "load"}, {"args", {{"path", findRepoRoot() + "/resources/AmberEstate/spire_house.pgg"}}}});
        check(spire.value("ok", false) && !spire["data"].value("has_errors", true),
              "rpc load of spire_house.pgg");
        const nlohmann::json views = call({{"op", "views"}, {"args", nlohmann::json::object()}});
        bool viewsOk = views.value("ok", false) && views["data"].contains("views") &&
                       views["data"]["views"].is_array() && views["data"]["views"].size() >= 3;
        if (viewsOk) {
            bool hasFront = false;
            for (const nlohmann::json& v : views["data"]["views"])
                hasFront = hasFront || v.value("name", std::string{}) == "front";
            viewsOk = hasFront;
        }
        check(viewsOk, "rpc views lists named views from spire_house.views.json");

        const nlohmann::json amber = call(
            {{"op", "load"},
             {"args",
              {{"path", findRepoRoot() + "/resources/AmberEstate/environment/umbrella_table.pgg"}}}});
        check(amber.value("ok", false) && !amber["data"].value("has_errors", true),
              "rpc load of AmberEstate/umbrella_table.pgg without lib_roots");

        const nlohmann::json bdoc = call({{"op", "docs"}, {"args", {{"symbol", "builtin:clip"}}}});
        check(bdoc.value("ok", false) && bdoc["data"].value("kind", std::string{}) == "builtin" &&
                  bdoc["data"].value("signature", std::string{}).rfind("clip(", 0) == 0,
              "rpc docs builtin:clip returns the registry signature + card");
        const nlohmann::json bdoc404 = call({{"op", "docs"}, {"args", {{"symbol", "builtin:nope"}}}});
        check(!bdoc404.value("ok", true) && bdoc404["error"].value("kind", std::string{}) == "not_found",
              "rpc docs builtin:nope -> not_found");
        const nlohmann::json clipBare = call({{"op", "docs"}, {"args", {{"symbol", "clip"}}}});
        check(clipBare.value("ok", false) && clipBare["data"].value("kind", std::string{}) == "builtin",
              "rpc docs clip without builtin: prefix falls back to the registry");

        // file= addresses a slot while this socket's current file is something else.
        call({{"op", "load"}, {"args", {{"path", corpus + "/e1_rock.pgg"}}}});
        const nlohmann::json probeFile =
            call({{"op", "probe"}, {"args", {{"spec", "base:schema"}, {"file", rockFile}}}});
        check(probeFile.value("ok", false) && probeFile["data"]["session"].value("file", std::string{}) == rockFile,
              "rpc probe file= hits the named slot");

        // F4 auto-reload
        {
            namespace fs = std::filesystem;
            const fs::path scratch = fs::path(findRepoRoot()) / "tmp" / "pgg_smoke_reload.pgg";
            const std::string scratchObj =
                (fs::path(findRepoRoot()) / "tmp" / "pgg_smoke_reload.obj").string();
            std::error_code ec;
            fs::create_directories(scratch.parent_path(), ec);
            const std::string srcA =
                "param size: int = 1\n"
                "base = ico_sphere(subdiv = size, radius = 1.0)\n"
                "output base\n";
            const std::string srcB =
                "param radius: int = 2\n"
                "orb = ico_sphere(subdiv = 2, radius = radius)\n"
                "output orb\n";
            int bump = 0;
            auto writeScratch = [&](const std::string& text) {
                {
                    std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
                    out << text;
                }
                fs::last_write_time(scratch, fs::file_time_type::clock::now() + std::chrono::seconds(++bump),
                                     ec);
            };
            writeScratch(srcA);
            const nlohmann::json ld = call({{"op", "load"}, {"args", {{"path", scratch.string()}}}});
            check(ld.value("ok", false) && !ld["data"].value("has_errors", true), "f4: load of the scratch file");
            const nlohmann::json p1 = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
            check(p1.value("ok", false) && !p1["data"].value("reloaded", true),
                  "f4: probe right after load answers reloaded:false");
            writeScratch(srcB);
            const nlohmann::json p2 = call({{"op", "probe"}, {"args", {{"spec", "orb:schema"}}}});
            bool f4reload = p2.value("ok", false) && p2["data"].value("reloaded", false) &&
                            p2["data"].contains("load_diagnostics");
            check(f4reload, "f4: on-disk edit -> next probe reloads");
            const nlohmann::json st = call({{"op", "status"}, {"args", nlohmann::json::object()}});
            check(st.value("ok", false) && st["data"]["params"].contains("radius") &&
                      !st["data"]["params"].contains("size"),
                  "f4: the reload refreshed the param set of the new file");
            const nlohmann::json e1 =
                call({{"op", "export"}, {"args", {{"node", "orb"}, {"obj_path", scratchObj}}}});
            check(e1.value("ok", false), "f4: export without edits");
            writeScratch(srcA);
            const nlohmann::json e2 =
                call({{"op", "export"}, {"args", {{"node", "base"}, {"obj_path", scratchObj}}}});
            check(e2.value("ok", false) && e2["data"].value("reloaded", false) &&
                      e2["data"]["stats"].value("pts", 0) == 42,
                  "f4: on-disk edit -> next export reloads (ico_sphere subdiv 1 = 42 pts)");
            writeScratch("= definitely not pgg (\n");
            const nlohmann::json p4 = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
            check(!p4.value("ok", true) && p4["error"].value("kind", std::string{}) == "run_errors",
                  "f4: broken edit -> run_errors without a run");
            writeScratch(srcA);
            const nlohmann::json p5 = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
            check(p5.value("ok", false) && p5["data"].value("reloaded", false),
                  "f4: fixed file -> reload recovers");
        }

        // RPC diff
        {
            namespace fs = std::filesystem;
            const fs::path scratch = fs::path(findRepoRoot()) / "tmp" / "pgg_smoke_diff.pgg";
            std::error_code ec;
            fs::create_directories(scratch.parent_path(), ec);
            const std::string srcC = "base = ico_sphere(subdiv = 1, radius = 1.0)\noutput base\n";
            const std::string srcC2 = "base = ico_sphere(subdiv = 2, radius = 1.0)\noutput base\n";
            const std::string srcD = "orb = ico_sphere(subdiv = 1, radius = 1.0)\noutput orb\n";
            int bump = 0;
            auto writeScratch = [&](const std::string& text) {
                {
                    std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
                    out << text;
                }
                fs::last_write_time(
                    scratch, fs::file_time_type::clock::now() + std::chrono::seconds(1000 + ++bump), ec);
            };
            writeScratch(srcC);
            call({{"op", "load"}, {"args", {{"path", scratch.string()}}}});
            const nlohmann::json d1 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
            check(d1.value("ok", false) && d1["data"].value("baseline_created", false),
                  "diff: first call records the baseline");
            const nlohmann::json d2 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
            check(d2.value("ok", false) && d2["data"].value("identical", false),
                  "diff: unchanged file is identical");
            writeScratch(srcC2);
            const nlohmann::json d3 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
            check(d3.value("ok", false) && d3["data"].value("reloaded", false) &&
                      d3["data"]["outputs"][0].value("status", std::string{}) == "changed",
                  "diff: on-disk edit -> changed");
            const nlohmann::json d5 = call({{"op", "diff"}, {"args", {{"update", true}}}});
            check(d5.value("ok", false) && d5["data"].value("snapshot_updated", false),
                  "diff: update:true refreshes the snapshot");
            const nlohmann::json d6 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
            check(d6.value("ok", false) && d6["data"].value("identical", false),
                  "diff: identical after the update");
            writeScratch(srcD);
            const nlohmann::json d7 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
            bool sawAdded = false, sawRemoved = false;
            if (d7.value("ok", false))
                for (const auto& o : d7["data"]["outputs"]) {
                    sawAdded = sawAdded || o.value("status", std::string{}) == "added";
                    sawRemoved = sawRemoved || o.value("status", std::string{}) == "removed";
                }
            check(sawAdded && sawRemoved, "diff: renamed output -> added + removed");
            const nlohmann::json ld2 =
                call({{"op", "load"}, {"args", {{"path", scratch.string()}, {"snapshot", true}}}});
            check(ld2.value("ok", false) && ld2["data"].value("snapshot", false),
                  "diff: load with snapshot:true records the baseline");
        }
    }

    // Two TCP clients, two files, concurrent probes.
    {
        std::string bufA, bufB;
        SocketFd a = connectClient("127.0.0.1", port);
        SocketFd b = connectClient("127.0.0.1", port);
        check(a != kBadSock && b != kBadSock, "two rpc clients connect");
        if (a != kBadSock && b != kBadSock) {
            const nlohmann::json la =
                rpcCall(a, {{"op", "load"}, {"args", {{"path", corpus + "/e1_rock.pgg"}}}}, bufA);
            const nlohmann::json lb =
                rpcCall(b, {{"op", "load"}, {"args", {{"path", corpus + "/e4_mesh_sphere.pgg"}}}}, bufB);
            check(la.value("ok", false) && lb.value("ok", false), "two clients load different files");
            std::atomic<bool> aOk{false}, bOk{false};
            std::thread ta([&] {
                const nlohmann::json p =
                    rpcCall(a, {{"op", "probe"}, {"args", {{"spec", "base:schema"}}}}, bufA);
                aOk = p.value("ok", false) && !p["data"]["records"].empty();
            });
            std::thread tb([&] {
                const nlohmann::json p =
                    rpcCall(b, {{"op", "probe"}, {"args", {{"spec", "m:schema"}}}}, bufB);
                bOk = p.value("ok", false) && !p["data"]["records"].empty();
            });
            ta.join();
            tb.join();
            check(aOk && bOk, "two TCP clients concurrent probe of different files");
            const nlohmann::json st =
                rpcCall(cfd != kBadSock ? cfd : a, {{"op", "status"}, {"args", nlohmann::json::object()}},
                        cfd != kBadSock ? inbuf : bufA);
            bool twoSlots = st.value("ok", false) && st["data"]["slots"].is_array() &&
                            st["data"]["slots"].size() >= 2;
            check(twoSlots, "status lists at least two slots");
        }
        if (a != kBadSock) closeFd(a);
        if (b != kBadSock) closeFd(b);
    }

    if (cfd != kBadSock) closeFd(cfd);
    polling = false;
    poller.join();
    runtime.stopWorkers();
    server.stop();

#if defined(_WIN32)
    // ServeRpcServer stop already called WSACleanup if it started WSA.
#endif

    if (g_failures == 0) {
        spdlog::info("TEST PASS: PggServe smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: PggServe smoke, {} check(s) failed", g_failures);
    }
    return g_failures == 0;
}
