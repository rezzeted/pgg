#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "DocumentSession.h"
#include "GeometryPreview.h"
#include "ServeRpcServer.h"

class ServeRuntime {
public:
    static constexpr size_t kMaxSlots = 8;
    static constexpr size_t kMaxCpuQueue = 64;
    static constexpr size_t kMaxGpuQueue = 16;
    static constexpr int kDefaultFboW = 1280;
    static constexpr int kDefaultFboH = 720;

    explicit ServeRuntime(ServeRpcServer& rpc);
    ~ServeRuntime();

    ServeRuntime(const ServeRuntime&) = delete;
    ServeRuntime& operator=(const ServeRuntime&) = delete;

    void setGpuReady(bool ready) { m_gpuReady = ready; }
    bool gpuReady() const { return m_gpuReady; }

    void startWorkers();
    void stopWorkers();

    void registerHandlers();

    // Sokol thread: drain at most one GPU job, render, capture after sg_commit.
    void beginGpuFrame(GeometryPreview& preview);
    void finishGpuFrame(GeometryPreview& preview);

    nlohmann::json statusJson() const;

    std::shared_ptr<DocumentSession> findSlot(const std::string& canonical) const;
    std::shared_ptr<DocumentSession> getOrCreateSlot(const std::string& canonical, std::string& err);

    double startTimeSec = 0.0;

private:
    struct GpuJob;

    void defer(uint64_t clientId, std::function<nlohmann::json()> fn);
    bool postCpu(std::function<void()> fn);
    bool postGpuAndWait(const std::shared_ptr<GpuJob>& job);

    std::shared_ptr<DocumentSession> resolveSlot(uint64_t clientId, const nlohmann::json& args);

    nlohmann::json handlePing(const nlohmann::json& args);
    nlohmann::json handleStatus(const nlohmann::json& args);
    nlohmann::json handleLoad(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleParams(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleViews(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleRender(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleReference(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleProbe(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleExport(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleDiff(uint64_t clientId, const nlohmann::json& args);
    nlohmann::json handleDocs(uint64_t clientId, const nlohmann::json& args);

    void applyRenderArgs(const nlohmann::json& args, GpuJob& job, CameraTargetSpec& target,
                         std::string& chromeEcho, std::string& err);

    void workerLoop();

    ServeRpcServer& m_rpc;
    std::atomic<bool> m_gpuReady{false};
    std::atomic<bool> m_stop{false};

    mutable std::mutex m_slotMu;
    std::unordered_map<std::string, std::shared_ptr<DocumentSession>> m_slots;
    std::list<std::string> m_lru;  // front = most recently used
    std::unordered_map<std::string, std::list<std::string>::iterator> m_lruIt;
    void touchLocked(const std::string& canonical);

    std::mutex m_cpuMu;
    std::condition_variable m_cpuCv;
    std::deque<std::function<void()>> m_cpuQueue;
    std::vector<std::thread> m_workers;

    std::mutex m_gpuMu;
    std::condition_variable m_gpuCv;
    std::deque<std::shared_ptr<GpuJob>> m_gpuQueue;
    std::shared_ptr<GpuJob> m_gpuInflight;

    std::atomic<uint64_t> m_shotCounter{0};
    std::atomic<uint64_t> m_srcCounter{0};
    std::atomic<uint64_t> m_diffCounter{0};
    std::atomic<uint64_t> m_refCounter{0};
};
