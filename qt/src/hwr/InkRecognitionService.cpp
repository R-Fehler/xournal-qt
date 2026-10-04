#include "InkRecognitionService.h"

#include <algorithm>

#include <QCoreApplication>
#include <QElapsedTimer>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "render/RenderService.h"

namespace xqt::hwr {

InkRecognitionService::InkRecognitionService(QObject* parent): QObject(parent) {
    worker = std::thread([this] {
#ifdef __linux__
        sched_param param{};
        param.sched_priority = 0;
        pthread_setschedparam(pthread_self(), SCHED_IDLE, &param);  // (only when a core is idle)
#elif defined(_WIN32)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#endif
        run();
    });
}

InkRecognitionService::~InkRecognitionService() {
    std::shared_ptr<Recognizer> r;
    {
        std::lock_guard lock(mtx);
        stopping = true;
        cancelRunning = true;
        queue.clear();
        r = rec;
    }
    if (r) {
        r->interrupt();
    }
    wake.notify_all();
    worker.join();
}

void InkRecognitionService::setRecognizer(std::shared_ptr<Recognizer> recognizer) {
    std::shared_ptr<Recognizer> before;
    {
        std::lock_guard lock(mtx);
        before = rec;
        rec = std::move(recognizer);
    }
    const QString idBefore = before ? before->capabilities().id : QString();
    if (idBefore != recognizerId()) {
        std::lock_guard lock(cacheMtx);
        cache.clear();
        uses.clear();
        bytes = 0;
    }
    wake.notify_all();
    Q_EMIT recognizerChanged();
}

std::shared_ptr<Recognizer> InkRecognitionService::recognizer() const {
    std::lock_guard lock(mtx);
    return rec;
}

QString InkRecognitionService::recognizerId() const {
    const auto r = recognizer();
    return r ? r->capabilities().id : QString();
}

bool InkRecognitionService::ready() const {
    const auto r = recognizer();
    return r && r->ready();
}

void InkRecognitionService::noteActivity() {
    std::lock_guard lock(mtx);
    pausedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(activityPauseMs);
}

void InkRecognitionService::setActivityPause(int ms) {
    std::lock_guard lock(mtx);
    activityPauseMs = ms;
}

void InkRecognitionService::setUnloadAfter(int ms) {
    std::lock_guard lock(mtx);
    unloadAfterMs = ms;
    wake.notify_all();
}

// --- the cache -----------------------------------------------------------------------------------------------------

std::shared_ptr<const ink::LineResult> InkRecognitionService::known(quint64 hash) const {
    std::lock_guard lock(cacheMtx);
    auto it = cache.find(hash);
    if (it == cache.end()) {
        return nullptr;
    }
    uses.splice(uses.begin(), uses, it->second.use);
    return it->second.result;
}

void InkRecognitionService::remember(quint64 hash, std::shared_ptr<const ink::LineResult> result) {
    if (!result) {
        return;
    }
    std::lock_guard lock(cacheMtx);
    auto it = cache.find(hash);
    if (it != cache.end()) {
        bytes -= it->second.result->bytes();
        bytes += result->bytes();
        it->second.result = std::move(result);
        uses.splice(uses.begin(), uses, it->second.use);
    } else {
        bytes += result->bytes();
        uses.push_front(hash);
        cache.emplace(hash, Cached{std::move(result), uses.begin()});
    }
    trim();
}

void InkRecognitionService::trim() {
    while (bytes > limit && uses.size() > 1) {
        const quint64 last = uses.back();
        uses.pop_back();
        auto it = cache.find(last);
        bytes -= it->second.result->bytes();
        cache.erase(it);
    }
}

size_t InkRecognitionService::cacheBytes() const {
    std::lock_guard lock(cacheMtx);
    return bytes;
}

size_t InkRecognitionService::cachedLines() const {
    std::lock_guard lock(cacheMtx);
    return cache.size();
}

void InkRecognitionService::setCacheLimit(size_t b) {
    std::lock_guard lock(cacheMtx);
    limit = b;
    trim();
}

// --- jobs ----------------------------------------------------------------------------------------------------------

quint64 InkRecognitionService::submit(QObject* owner, Job job, Done done) {
    std::lock_guard lock(mtx);
    const quint64 id = nextId++;
    queue.push_back({id, owner, owner, std::move(job), std::move(done)});
    wake.notify_all();
    return id;
}

void InkRecognitionService::cancel(QObject* owner) {
    std::shared_ptr<Recognizer> r;
    std::unique_lock lock(mtx);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Queued& q) { return q.ownerKey == owner; }),
                queue.end());
    if (running && runningFor == owner) {
        cancelRunning = true;
        r = rec;
        lock.unlock();
        if (r) {
            r->interrupt();
        }
        lock.lock();
        idle.wait(lock, [&] { return !(running && runningFor == owner); });
    }
}

void InkRecognitionService::reprioritize(quint64 id, double priority) {
    std::lock_guard lock(mtx);
    for (Queued& q: queue) {
        if (q.id == id) {
            q.job.priority = priority;
        }
    }
}

size_t InkRecognitionService::pending() const {
    std::lock_guard lock(mtx);
    return queue.size() + (running ? 1 : 0);
}

bool InkRecognitionService::waitForIdle(int ms) {
    QElapsedTimer t;
    t.start();
    while (pending() > 0 && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QCoreApplication::processEvents();
    return pending() == 0;
}

void InkRecognitionService::run() {
    auto lastWork = std::chrono::steady_clock::now();
    bool loaded = false;  ///< the recogniser read a line since it was last unloaded
    std::unique_lock lock(mtx);
    for (;;) {
        if (stopping) {
            return;
        }
        if (queue.empty()) {
            if (loaded && rec) {
                const auto unloadAt = lastWork + std::chrono::milliseconds(unloadAfterMs);
                if (std::chrono::steady_clock::now() >= unloadAt) {
                    auto r = rec;
                    lock.unlock();
                    r->unload();
                    lock.lock();
                    loaded = false;
                    continue;
                }
                wake.wait_until(lock, unloadAt);
            } else {
                wake.wait(lock);
            }
            continue;
        }
        auto best = std::min_element(queue.begin(), queue.end(), [](const Queued& a, const Queued& b) {
            return a.job.priority < b.job.priority || (a.job.priority == b.job.priority && a.id < b.id);
        });
        Queued q = std::move(*best);
        queue.erase(best);
        running = true;
        runningFor = q.ownerKey;
        cancelRunning = false;
        lock.unlock();
        const int read = process(q);
        lastWork = std::chrono::steady_clock::now();
        loaded = loaded || read > 0;
        lock.lock();
        running = false;
        runningFor = nullptr;
        idle.notify_all();
    }
}

int InkRecognitionService::process(Queued& q) {
    const Layout layout = hwr::layout(q.job.strokes);
    PageResult result;
    result.id = q.id;
    std::vector<ink::PlacedLine> placed;
    auto cancelled = [this] {
        std::lock_guard lock(mtx);
        return cancelRunning || stopping;
    };
    for (const InkLine& line: layout.lines) {
        LineRef ref{line.hash, line.origin(), known(line.hash)};
        if (!ref.result) {
            std::shared_ptr<Recognizer> r = recognizer();
            if (r && r->ready()) {
                // Not before the pages in view are drawn, nor while the user writes
                for (;;) {
                    RenderService::waitForVisiblePages(std::chrono::milliseconds(500));
                    std::chrono::steady_clock::time_point until;
                    {
                        std::lock_guard lock(mtx);
                        until = pausedUntil;
                    }
                    if (cancelled() || std::chrono::steady_clock::now() >= until) {
                        break;
                    }
                    std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
                            until - std::chrono::steady_clock::now(), std::chrono::milliseconds(100)));
                }
                if (cancelled()) {
                    return result.recognised;
                }
                Context context;
                context.cancelled = cancelled;
                if (auto read = r->recognizeLine(LineInput::of(q.job.strokes, layout, line), context)) {
                    ref.result = std::make_shared<const ink::LineResult>(std::move(*read));
                    remember(line.hash, ref.result);
                    ++result.recognised;
                    Q_EMIT lineRead();
                }
            }
            if (cancelled()) {
                return result.recognised;
            }
        }
        result.complete = result.complete && ref.result != nullptr;
        placed.push_back({ref.origin, ref.result});
        result.lines.push_back(std::move(ref));
    }
    result.text = ink::PageText::assemble(placed);
    const int read = result.recognised;
    if (cancelled() || !q.owner) {
        return read;
    }
    QMetaObject::invokeMethod(
            q.owner.data(),
            [owner = q.owner, done = std::move(q.done), r = std::move(result)]() mutable {
                if (owner && done) {
                    done(std::move(r));
                }
            },
            Qt::QueuedConnection);
    return read;
}

}  // namespace xqt::hwr
