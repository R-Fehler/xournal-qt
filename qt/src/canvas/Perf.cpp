#include "Perf.h"

#include <cstdio>

#include <QElapsedTimer>

namespace xqt {

Perf* perfInstance() {
    static Perf* perf = Perf::on() ? new Perf : nullptr;  // (a QObject with a timer: only when it is wanted)
    return perf;
}

namespace {
QElapsedTimer& clock() {
    static QElapsedTimer t = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return t;
}
}  // namespace

bool Perf::on() {
    static const bool wanted = qEnvironmentVariableIntValue("XQT_PERF") == 1;
    return wanted;
}

Perf::Perf() {
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, &Perf::report);
    timer.start();
}

void Perf::add(Counter c, qint64 n) {
    if (Perf* p = perfInstance()) {
        p->counters[c] += n;
    }
}

void Perf::took(Timing t, qint64 micros) {
    if (Perf* p = perfInstance()) {
        Times& times = p->times[t];
        times.sum += micros;
        ++times.count;
        for (qint64 worst = times.worst; micros > worst && !times.worst.compare_exchange_weak(worst, micros);) {
        }
    }
}

void Perf::report() {
    qint64 values[CounterCount];
    qint64 total = 0;
    for (int i = 0; i < CounterCount; ++i) {
        values[i] = counters[i].exchange(0);
        total += values[i];
    }
    struct {
        double average;
        double worst;
    } ms[TimingCount];
    qint64 sharp = 0;  // pages in view rendered at their zoom
    for (int i = 0; i < TimingCount; ++i) {
        const qint64 sum = times[i].sum.exchange(0), count = times[i].count.exchange(0);
        ms[i] = {count > 0 ? static_cast<double>(sum) / static_cast<double>(count) / 1000.0 : 0.0,
                 static_cast<double>(times[i].worst.exchange(0)) / 1000.0};
        if (i == SharpTime) {
            sharp = count;
        }
    }
    if (total + sharp == 0) {
        return;  // nothing happened
    }
    std::fprintf(stderr,
                 "xqt-perf 1.0 s: input mouse %lld (claimed %lld, hit test %.2f/%.2f ms) touch %lld pen %lld | "
                 "scroll %lld -> visibility %lld (%.2f/%.2f ms, of it the current page %.2f/%.2f ms) | frames %lld "
                 "sync %.2f/%.2f ms | tiles %lld previews %lld | sharp %lld after %.0f/%.0f ms\n",
                 static_cast<long long>(values[MouseEvents]), static_cast<long long>(values[MouseClaimed]),
                 ms[HitTest].average, ms[HitTest].worst, static_cast<long long>(values[TouchEvents]),
                 static_cast<long long>(values[PenEvents]), static_cast<long long>(values[Scrolls]),
                 static_cast<long long>(values[Visibility]), ms[VisibilityTime].average, ms[VisibilityTime].worst,
                 ms[CurrentPageTime].average, ms[CurrentPageTime].worst, static_cast<long long>(values[Frames]),
                 ms[SyncTime].average, ms[SyncTime].worst,
                 static_cast<long long>(values[Tiles]), static_cast<long long>(values[Previews]),
                 static_cast<long long>(sharp), ms[SharpTime].average, ms[SharpTime].worst);
    std::fflush(stderr);
}

PerfScope::PerfScope(Perf::Timing t): what(t), start(Perf::on() ? clock().nsecsElapsed() : 0) {}

PerfScope::~PerfScope() {
    if (start > 0) {
        Perf::took(what, (clock().nsecsElapsed() - start) / 1000);
    }
}

}  // namespace xqt
