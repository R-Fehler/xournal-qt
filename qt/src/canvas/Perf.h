/*
 * xournal-qt: counters of the canvas work, for finding what makes it slow (XQT_PERF=1).
 *
 * Off unless the environment variable XQT_PERF is set to 1, and then a line goes to the standard error output once a
 * second while something happens:
 *
 *   xqt-perf 1.0 s: input mouse 312 (claimed 0, hit test 0.05/0.31 ms) touch 0 pen 0 | scroll 312 -> visibility 61
 *   (0.42/2.10 ms) | frames 59 sync 3.1/18.4 ms | tiles 480 previews 7 | geometry 0 (displays 0) | sharp 2 after
 *   180/240 ms
 *
 * Counts per second: the events the canvas filter saw (and how long the hit test of the item under the pointer took,
 * average/worst), the scroll changes and how many of them led to a visibility update (the current page, the models,
 * the sidebar), the frames of the canvas with the time of their scene graph sync, the page tiles and previews
 * uploaded in them, the pictures of the setsquare or compass drawn (and of its angle display), and the pages in view
 * that got their render at the zoom they are shown at, with how long they waited for it after the view last asked
 * (after a zoom, this includes the wait for the zoom to be stable).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>

#include <QObject>
#include <QTimer>

namespace xqt {

class Perf;
/// The counters, or nullptr when they are off
Perf* perfInstance();

class Perf final: public QObject {
    Q_OBJECT
public:
    enum Counter {
        MouseEvents,
        MouseClaimed,
        TouchEvents,
        PenEvents,
        Scrolls,
        Visibility,
        Frames,
        Tiles,
        Previews,
        GeometryPictures,  ///< the setsquare or compass drawn anew (its body; on a new size or zoom)
        GeometryDisplays,  ///< its angle display drawn anew (while it turns)
        CounterCount
    };
    enum Timing { HitTest, VisibilityTime, CurrentPageTime, SyncTime, SharpTime, TimingCount };

    /// XQT_PERF=1
    static bool on();
    static void add(Counter c, qint64 n = 1);
    /// Microseconds of one such piece of work
    static void took(Timing t, qint64 micros);

private:
    friend Perf* perfInstance();
    Perf();
    void report();
    std::atomic<qint64> counters[CounterCount]{};
    struct Times {
        std::atomic<qint64> sum{0};
        std::atomic<qint64> worst{0};
        std::atomic<qint64> count{0};
    };
    Times times[TimingCount];
    QTimer timer;
};

/// Measures a piece of work while it is in scope (nothing when the counters are off).
class PerfScope {
public:
    explicit PerfScope(Perf::Timing t);
    ~PerfScope();

private:
    Perf::Timing what;
    qint64 start = 0;
};

}  // namespace xqt
