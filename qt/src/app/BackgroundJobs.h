/*
 * xournal-qt: the app's work off the UI thread (writing tags or to-dos into a file that is not open, rewriting links,
 * exports, shares, pages read from files): one pool with an owner (AppServices), so that quitting waits for the
 * documents being written (AppController::shutdown) instead of leaving them to the global pool's static destructor
 * after main() returned.
 *
 * Work nobody waits for runs at idle priority (AGENTS.md: "work that is not for right now goes to a background
 * worker at idle priority"); work whose result the reader waits for (an export, a share, pages read from a file)
 * runs at normal priority.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <utility>

#include <QThread>
#include <QThreadPool>

namespace xqt {

class BackgroundJobs {
public:
    enum class Priority {
        Idle,    ///< nobody waits for it (a write into a file that is not open, a warm-up)
        Normal,  ///< the reader waits for its result
    };

    BackgroundJobs();
    /// Waits for what runs.
    ~BackgroundJobs();
    BackgroundJobs(const BackgroundJobs&) = delete;
    BackgroundJobs& operator=(const BackgroundJobs&) = delete;

    /// Runs `job` on a worker. What it hands back to the UI thread goes through QMetaObject::invokeMethod on an
    /// object that may be gone by then (a QPointer, or the application).
    template <typename F>
    void start(F&& job, Priority priority) {
        pool.start([job = std::forward<F>(job), priority]() mutable {
            QThread::currentThread()->setPriority(priority == Priority::Idle ? QThread::IdlePriority
                                                                            : QThread::NormalPriority);
            job();
        });
    }

    /// Until every job started is done (quitting).
    void waitForDone();

private:
    QThreadPool pool;
};

}  // namespace xqt
