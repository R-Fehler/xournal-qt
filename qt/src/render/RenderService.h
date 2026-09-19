/*
 * xournal-qt: background rendering of page rasters (replaces upstream's single-threaded Scheduler for rendering).
 *
 * - N worker threads; a raster is never rendered by two workers at the same time;
 * - de-duplication: a raster queued several times is rendered once (its dirty state is merged by PageRaster);
 * - priorities: visible pages before preloaded ones before thumbnails;
 * - blockRerenderZoom(): like upstream Scheduler::blockRerenderZoom, full re-renders wait until the zoom has been
 *   stable for 300 ms, so that pinch/zoom gestures only scale the existing buffers.
 * Qt-free: completion notifications go through Util::execInUiThread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace xqt {

class PageRaster;

class RenderService {
public:
    enum class Priority { Visible = 0, Preload = 1, Background = 2 };

    /// threads <= 0: hardware concurrency - 1 (at least 1)
    explicit RenderService(int threads = 0);
    ~RenderService();
    RenderService(const RenderService&) = delete;
    RenderService& operator=(const RenderService&) = delete;

    void schedule(const std::shared_ptr<PageRaster>& raster, Priority priority = Priority::Visible);
    /// Remove queued jobs of this raster and wait until it is not running any more.
    void cancel(const PageRaster* raster);
    /// Defer rendering until the zoom did not change for `delay` (default 300 ms, like upstream).
    void blockRerenderZoom(std::chrono::milliseconds delay = std::chrono::milliseconds(300));
    /// Block until the queue is empty and no job is running (tests, shutdown, export).
    void waitForIdle();

    int threadCount() const { return static_cast<int>(workers.size()); }

private:
    void workerLoop();

    std::mutex mtx;
    std::condition_variable wakeWorkers;
    std::condition_variable idle;
    std::deque<std::shared_ptr<PageRaster>> queues[3];
    std::unordered_set<const PageRaster*> queued;
    std::unordered_set<const PageRaster*> running;
    std::chrono::steady_clock::time_point blockedUntil{};
    bool stopping = false;
    std::vector<std::thread> workers;
};

}  // namespace xqt
