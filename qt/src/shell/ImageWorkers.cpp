#include "ImageWorkers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>

#include <QCoreApplication>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include "AsyncImage.h"

namespace xqt {

namespace {
/// How many threads each pool has: the machine's threads (`ideal`) are shared by the canvas's renders, the page
/// sketches, the thumbnails and the rest.
int threadCount(ImageWorkers::Pool pool, int ideal) {
    using P = ImageWorkers::Pool;
    switch (pool) {
        case P::Thumbnails:
            return std::max(1, std::min(4, ideal - 1));
        case P::Sketches:
            return 2;  // (each with a PDF instance of its own)
        case P::Covers:
            return std::max(1, std::min(3, ideal / 2));
        case P::HitPages:
            return std::max(2, std::min(4, ideal / 2));  // (different documents in parallel)
        case P::Snippets:
            return std::max(1, std::min(2, ideal / 2));
        case P::SketchFiles:
        case P::CoverFiles:
        case P::Annotations:
        case P::AnnotationPictures:
        case P::Count:
            break;
    }
    return 1;
}

constexpr size_t COUNT = static_cast<size_t>(ImageWorkers::Pool::Count);

struct Workers {
    std::mutex mtx;  ///< start() against shutdown()
    bool closed = false;
    std::array<QThreadPool*, COUNT> pools{};
    std::atomic<int> jobs{0};  ///< running or queued

    QThreadPool& pool(ImageWorkers::Pool p) {  // (under mtx)
        QThreadPool*& made = pools[static_cast<size_t>(p)];
        if (!made) {
            if (std::none_of(pools.begin(), pools.end(), [](QThreadPool* p) { return p != nullptr; })) {
                // Also when nobody called shutdown() (tests, an early exit): before the program's statics go, which a
                // job still running would use (poppler and glib, Qt's image plugins)
                qAddPostRoutine(&ImageWorkers::shutdown);
            }
            made = new QThreadPool;  // (never destroyed: shutdown() stops it, the process ends)
            made->setMaxThreadCount(threadCount(p, QThread::idealThreadCount()));
            made->setThreadPriority(QThread::IdlePriority);
        }
        return *made;
    }
};
Workers& workers() {
    static Workers w;
    return w;
}

/// A job that counts itself while it is queued or runs (deleted unrun by shutdown's clear())
class Job final: public QRunnable {
public:
    explicit Job(std::function<void()> f): fn(std::move(f)) { ++workers().jobs; }
    ~Job() override { --workers().jobs; }
    void run() override { fn(); }

private:
    std::function<void()> fn;
};
}  // namespace

bool ImageWorkers::start(Pool pool, std::function<void()> job, int priority) {
    auto& w = workers();
    std::lock_guard lock(w.mtx);
    if (w.closed) {
        return false;
    }
    w.pool(pool).start(new Job(std::move(job)), priority);
    return true;
}

void ImageWorkers::respond(Pool pool, AsyncImageResponse* response, std::function<QImage()> make, int priority) {
    const bool started = start(
            pool,
            [response, make = std::move(make)] {
                QImage img;
                if (!response->isCancelled()) {  // (else scrolled away meanwhile: not drawn)
                    img = make();
                }
                response->finish(std::move(img));
            },
            priority);
    if (!started) {
        response->finish({});
    }
}

void ImageWorkers::waitForDone(Pool pool) {
    QThreadPool* p = nullptr;
    {
        auto& w = workers();
        std::lock_guard lock(w.mtx);
        p = &w.pool(pool);
    }
    p->waitForDone();
}

void ImageWorkers::shutdown() {
    auto& w = workers();
    std::array<QThreadPool*, COUNT> pools{};
    {
        std::lock_guard lock(w.mtx);
        w.closed = true;
        pools = w.pools;
    }
    // (not under the lock: a running job may try to start another one, which is refused)
    for (QThreadPool* p: pools) {
        if (p) {
            p->clear();
        }
    }
    for (QThreadPool* p: pools) {
        if (p) {
            p->waitForDone();
        }
    }
}

bool ImageWorkers::isShutDown() {
    auto& w = workers();
    std::lock_guard lock(w.mtx);
    return w.closed;
}

int ImageWorkers::busy() { return workers().jobs.load(); }

void ImageWorkers::reopen() {
    auto& w = workers();
    std::lock_guard lock(w.mtx);
    w.closed = false;
}

int ImageWorkers::threadsOf(Pool pool) {
    auto& w = workers();
    std::lock_guard lock(w.mtx);
    return w.pool(pool).maxThreadCount();
}

}  // namespace xqt
