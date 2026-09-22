#include "RenderService.h"

#include <algorithm>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

#include "PageRaster.h"

namespace xqt {

RenderService::RenderService(int threads, int background) {
    if (threads <= 0) {
        threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    }
    workers.reserve(static_cast<size_t>(threads + background));
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([this] { workerLoop(false); });
    }
    for (int i = 0; i < background; ++i) {
        workers.emplace_back([this] {
#ifdef __linux__
            sched_param param{};
            param.sched_priority = 0;
            pthread_setschedparam(pthread_self(), SCHED_IDLE, &param);  // (only when a core is idle)
#endif
            workerLoop(true);
        });
    }
}

RenderService::~RenderService() {
    {
        std::lock_guard lock(mtx);
        stopping = true;
        for (auto& q: queues) {
            q.clear();
        }
        queued.clear();
    }
    wakeWorkers.notify_all();
    for (auto& t: workers) {
        t.join();
    }
}

void RenderService::schedule(const std::shared_ptr<PageRaster>& raster, Priority priority) {
    {
        std::lock_guard lock(mtx);
        if (stopping) {
            return;
        }
        const auto p = static_cast<size_t>(priority);
        if (queued.count(raster.get())) {
            // Already queued: promote it if the new priority is higher.
            for (size_t lower = p + 1; lower < std::size(queues); ++lower) {
                auto& q = queues[lower];
                if (auto it = std::find(q.begin(), q.end(), raster); it != q.end()) {
                    q.erase(it);
                    queues[p].push_back(raster);
                    break;
                }
            }
            return;
        }
        queued.insert(raster.get());
        queues[p].push_back(raster);
    }
    wakeWorkers.notify_all();  // (a worker of the right kind)
}

void RenderService::cancel(const PageRaster* raster) {
    std::unique_lock lock(mtx);
    if (queued.erase(raster)) {
        for (auto& q: queues) {
            q.erase(std::remove_if(q.begin(), q.end(), [&](const auto& r) { return r.get() == raster; }), q.end());
        }
    }
    idle.wait(lock, [&] { return !running.count(raster); });
}

void RenderService::dropQueued(Priority priority) {
    std::lock_guard lock(mtx);
    auto& q = queues[static_cast<size_t>(priority)];
    for (const auto& r: q) {
        queued.erase(r.get());
    }
    q.clear();
}

bool RenderService::hasWork(Priority priority) {
    std::lock_guard lock(mtx);
    return !queues[static_cast<size_t>(priority)].empty() || (priority != Priority::Visible && runningBackground > 0);
}

void RenderService::blockRerenderZoom(std::chrono::milliseconds delay) {
    {
        std::lock_guard lock(mtx);
        blockedUntil = std::chrono::steady_clock::now() + delay;
    }
    wakeWorkers.notify_all();
}

void RenderService::waitForIdle() {
    std::unique_lock lock(mtx);
    idle.wait(lock, [&] {
        return running.empty() && std::all_of(std::begin(queues), std::end(queues), [](auto& q) { return q.empty(); });
    });
}

void RenderService::workerLoop(bool background) {
    std::unique_lock lock(mtx);
    while (!stopping) {
        if (const auto now = std::chrono::steady_clock::now(); now < blockedUntil) {
            wakeWorkers.wait_until(lock, blockedUntil);
            continue;
        }
        std::shared_ptr<PageRaster> job;
        // Visible workers: the visible pages; background workers: the others
        for (size_t p = background ? 1 : 0; p < (background ? std::size(queues) : 1); ++p) {
            auto& q = queues[p];
            auto it = std::find_if(q.begin(), q.end(), [&](const auto& r) { return !running.count(r.get()); });
            if (it != q.end()) {
                job = std::move(*it);
                q.erase(it);
                break;
            }
        }
        if (!job) {
            wakeWorkers.wait(lock);
            continue;
        }
        const PageRaster* raw = job.get();
        queued.erase(raw);
        running.insert(raw);
        runningBackground += background;

        lock.unlock();
        job->run(background);
        job.reset();
        lock.lock();

        running.erase(raw);
        runningBackground -= background;
        idle.notify_all();
        wakeWorkers.notify_all();  // a job for this raster may have been skipped while it was running
    }
}

}  // namespace xqt
