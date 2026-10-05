/*
 * xournal-qt: the worker that reads handwriting for the search, for all open documents and the library.
 *
 * One thread at idle priority (Linux: SCHED_IDLE, it only runs when a core has nothing else to do), shared by
 * everything that wants handwriting read (InkTextIndexer per open document, the library's job). A client submits a
 * page: a copy of its strokes, with a priority (the page in view of the document in front first, then the other pages
 * of that document from there outwards, then the other open documents, then the library). The worker
 *  1. lays the strokes out in lines and words (InkLayout.h);
 *  2. takes the lines it knows by their hash from its cache (a line moved, a page seen before, or the library's
 *     pack: remember()); reads the others with the recogniser, one line at a time, and those it knows but the
 *     recogniser finds not enough for the job's plan (Recognizer::enough: another model is to read it, LanguagePlan.h);
 *  3. hands the page's words (InkText.h: PageText) and its lines back to the client on the client's thread.
 * Before each line it waits while pages in view are being rendered (RenderService::waitForVisiblePages) and while the
 * user writes (noteActivity(): nothing is read until ACTIVITY_PAUSE_MS after the last change of a page). Without a
 * recogniser that is ready (no model), pages are still laid out and their known lines put together (the library's
 * results stay searchable); their other lines are left out, and the result says it is not complete. A client that
 * goes cancels its jobs (a line being read is interrupted); the model is unloaded after a minute without work.
 *
 * Memory: the line cache (results by line hash) has a limit (setCacheLimit, default 16 MB): the least recently used
 * lines go. Jobs hold copies of strokes: clients keep few of them queued (InkTextIndexer: two at a time).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <QObject>
#include <QPointer>

#include "InkLayout.h"
#include "Recognizer.h"

namespace xqt::hwr {

/// A line of a page read: where it is, and what was read in it (null: not read, no model).
struct LineRef {
    quint64 hash = 0;
    QPointF origin;
    std::shared_ptr<const ink::LineResult> result;
};

struct PageResult {
    quint64 id = 0;                              ///< the job's
    std::shared_ptr<const ink::PageText> text;   ///< the lines read, put together
    std::vector<LineRef> lines;                  ///< all lines of the page, top to bottom
    bool complete = true;                        ///< every line was read
    int recognised = 0;                          ///< lines read by the recogniser for it (not known before)
};

class InkRecognitionService final: public QObject {
    Q_OBJECT
public:
    static constexpr int ACTIVITY_PAUSE_MS = 2000;
    static constexpr int UNLOAD_AFTER_MS = 60000;
    static constexpr size_t DEFAULT_CACHE_BYTES = 16 * 1024 * 1024;

    explicit InkRecognitionService(QObject* parent = nullptr);
    ~InkRecognitionService() override;

    /// The recogniser (null: none). Its id names the results: the cache is emptied when it changes.
    void setRecognizer(std::shared_ptr<Recognizer> recognizer);
    std::shared_ptr<Recognizer> recognizer() const;
    /// Its id ("" without one).
    QString recognizerId() const;
    /// The recogniser can read now (its model is there).
    bool ready() const;

    /// A page changed (the pen writes): no line is read for ACTIVITY_PAUSE_MS from now.
    void noteActivity();
    /// How long noteActivity() pauses (tests; default ACTIVITY_PAUSE_MS).
    void setActivityPause(int ms);
    /// Unload the model after this long without work (tests; default UNLOAD_AFTER_MS).
    void setUnloadAfter(int ms);

    /// Results by line hash: what was read before (or came from the library's pack).
    std::shared_ptr<const ink::LineResult> known(quint64 hash) const;
    void remember(quint64 hash, std::shared_ptr<const ink::LineResult> result);
    size_t cacheBytes() const;
    size_t cachedLines() const;
    void setCacheLimit(size_t bytes);

    struct Job {
        double priority = 0;  ///< smaller first
        std::vector<InkStroke> strokes;
        /// The document's plan of which models read its lines (LanguagePlan.h; null: all)
        std::shared_ptr<LanguagePlan> plan;
    };
    using Done = std::function<void(PageResult)>;
    /// Read a page; `done` is called on `owner`'s thread (not if `owner` is gone or cancelled the job). Returns its id.
    quint64 submit(QObject* owner, Job job, Done done);
    /// Drop the jobs of `owner` (a job being worked on stops soon; its result is dropped). Waits until the worker is
    /// not working for it any more.
    void cancel(QObject* owner);
    /// Change the priority of a queued job.
    void reprioritize(quint64 id, double priority);
    /// Jobs queued or running.
    size_t pending() const;
    /// Wait until no job is queued or running (tests; at most `ms`, processing the events of this thread).
    bool waitForIdle(int ms = 10000);

Q_SIGNALS:
    /// The recogniser changed, or became ready (clients submit their incomplete pages again).
    void recognizerChanged();
    /// A line was read (progress).
    void lineRead();

private:
    struct Queued {
        quint64 id = 0;
        QPointer<QObject> owner;
        const QObject* ownerKey = nullptr;
        Job job;
        Done done;
    };
    void run();
    /// Returns the lines it read with the recogniser
    int process(Queued& q);
    void touch(quint64 hash) const;
    void trim();

    mutable std::mutex mtx;
    std::condition_variable wake, idle;
    std::deque<Queued> queue;
    const QObject* runningFor = nullptr;
    bool running = false;
    bool cancelRunning = false;
    bool stopping = false;
    quint64 nextId = 1;
    std::shared_ptr<Recognizer> rec;
    std::chrono::steady_clock::time_point pausedUntil{};
    int unloadAfterMs = UNLOAD_AFTER_MS;
    int activityPauseMs = ACTIVITY_PAUSE_MS;

    struct Cached {
        std::shared_ptr<const ink::LineResult> result;
        std::list<quint64>::iterator use;
    };
    mutable std::mutex cacheMtx;
    mutable std::list<quint64> uses;  ///< most recently used first
    std::unordered_map<quint64, Cached> cache;
    size_t bytes = 0;
    size_t limit = DEFAULT_CACHE_BYTES;

    std::thread worker;
};

}  // namespace xqt::hwr
