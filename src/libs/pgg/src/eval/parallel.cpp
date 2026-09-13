#include "../../pch.h"

#include "parallel.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace pgg {
namespace {

// True while this thread executes parallelFor chunk code — pool workers
// permanently, the calling thread for the duration of its dispatch. A nested
// parallelFor on such a thread must run inline: re-entering the pool from a
// chunk would deadlock the parent job (the caller is already draining it).
thread_local bool tInParallelChunk = false;

// Lazily created global pool, sized once from hardware_concurrency. Several
// host threads may dispatch at once (PggServe document slots); each dispatch
// is a Job whose chunks are stolen independently. Workers + the submitting
// caller drain that job; chunk bodies still write disjoint ranges.
class ThreadPool {
public:
    static ThreadPool& instance() {
        static ThreadPool pool;
        return pool;
    }

    unsigned laneCount() const { return lanes_; }  // workers + the calling thread

    void run(size_t chunkCount, const std::function<void(size_t)>& fn) {
        auto job = std::make_shared<Job>();
        job->fn = fn;
        job->chunkCount = chunkCount;
        job->next.store(0, std::memory_order_relaxed);
        job->remaining.store(chunkCount, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(m_);
            jobs_.push_back(job);
        }
        cv_.notify_all();
        drain(*job);
        std::unique_lock<std::mutex> lk(job->doneM);
        job->doneCv.wait(lk, [&] { return job->remaining.load(std::memory_order_acquire) == 0; });
        {
            std::lock_guard<std::mutex> lkJobs(m_);
            for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
                if (it->get() == job.get()) {
                    jobs_.erase(it);
                    break;
                }
            }
        }
    }

private:
    struct Job {
        std::function<void(size_t)> fn;
        size_t chunkCount = 0;
        std::atomic<size_t> next{0};
        std::atomic<size_t> remaining{0};
        std::mutex doneM;
        std::condition_variable doneCv;
    };

    ThreadPool() {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 1;
        lanes_ = hw;
        for (unsigned i = 1; i < hw; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (std::thread& t : workers_) t.join();
    }

    std::shared_ptr<Job> claimableLocked() const {
        for (const auto& j : jobs_) {
            if (j->next.load(std::memory_order_relaxed) < j->chunkCount) return j;
        }
        return nullptr;
    }

    void workerLoop() {
        tInParallelChunk = true;
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || claimableLocked() != nullptr; });
                if (stop_) return;
                job = claimableLocked();
            }
            if (!job) continue;
            stealOne(*job);
        }
    }

    void drain(Job& job) {
        for (;;) {
            const size_t c = job.next.fetch_add(1, std::memory_order_relaxed);
            if (c >= job.chunkCount) break;
            job.fn(c);
            finishChunk(job);
        }
    }

    void stealOne(Job& job) {
        const size_t c = job.next.fetch_add(1, std::memory_order_relaxed);
        if (c >= job.chunkCount) return;
        job.fn(c);
        finishChunk(job);
    }

    void finishChunk(Job& job) {
        if (job.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(job.doneM);
            job.doneCv.notify_all();
        }
    }

    std::vector<std::thread> workers_;
    unsigned lanes_ = 1;
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<Job>> jobs_;
    bool stop_ = false;
};

}  // namespace

// Shared chunked dispatch: ~4 chunks per lane over the pool, the calling
// thread drains and is marked for anti-nesting for the whole dispatch.
void dispatch(size_t count, unsigned lanes, const std::function<void(size_t, size_t)>& fn) {
    ThreadPool& pool = ThreadPool::instance();
    size_t chunkCount = std::min<size_t>(count, static_cast<size_t>(std::min(lanes, pool.laneCount())) * 4);
    const size_t chunkSize = (count + chunkCount - 1) / chunkCount;
    chunkCount = (count + chunkSize - 1) / chunkSize;
    // The caller drains chunks too, so mark it for the whole dispatch: chunk
    // code calling parallelFor again must take the inline path here as well.
    tInParallelChunk = true;
    pool.run(chunkCount, [&](size_t c) {
        const size_t begin = c * chunkSize;
        fn(begin, std::min(count, begin + chunkSize));
    });
    tInParallelChunk = false;
}

unsigned resolveThreadCount(unsigned threads) {
    if (threads > 0) return threads;
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : hw;
}

void parallelFor(size_t count, unsigned threads, const std::function<void(size_t, size_t)>& fn) {
    if (count == 0) return;
    const unsigned lanes = resolveThreadCount(threads);
    if (lanes <= 1 || count < kParallelThreshold || tInParallelChunk) {
        fn(0, count);
        return;
    }
    dispatch(count, lanes, fn);
}

void parallelForPieces(size_t count, unsigned threads, const std::function<void(size_t, size_t)>& fn) {
    if (count == 0) return;
    const unsigned lanes = resolveThreadCount(threads);
    if (lanes <= 1 || count < 2 || tInParallelChunk) {
        fn(0, count);
        return;
    }
    dispatch(count, lanes, fn);
}

}  // namespace pgg
