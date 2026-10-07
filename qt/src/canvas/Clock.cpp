#include "Clock.h"

#include <algorithm>
#include <chrono>

namespace xqt {

namespace {
class SteadyClock final: public Clock {
public:
    double nowMs() const override {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
};
}  // namespace

Clock& Clock::steady() {
    static SteadyClock clock;
    return clock;
}

// --- ManualClock ---------------------------------------------------------------------------------------------------

ManualClock::~ManualClock() {
    // (timers that outlive it go back to the steady clock, stopped)
    for (ClockTimer* timer: std::vector<ClockTimer*>(timers)) {
        timer->setClock(Clock::steady());
    }
}

void ManualClock::attach(ClockTimer* timer) { timers.push_back(timer); }

void ManualClock::detach(ClockTimer* timer) { timers.erase(std::remove(timers.begin(), timers.end(), timer), timers.end()); }

bool ManualClock::hasActiveTimers() const {
    return std::any_of(timers.begin(), timers.end(), [](const ClockTimer* t) { return t->active; });
}

void ManualClock::advance(double ms) {
    const double until = now + std::max(0.0, ms);
    for (;;) {
        // The next timer due (the earliest; of two at the same time, the one started first stays first: stable)
        ClockTimer* next = nullptr;
        for (ClockTimer* t: timers) {
            if (t->active && t->dueMs <= until && (!next || t->dueMs < next->dueMs)) {
                next = t;
            }
        }
        if (!next) {
            break;
        }
        now = std::max(now, next->dueMs);
        if (next->singleShot) {
            next->active = false;
        } else {
            next->dueMs = now + std::max(1, next->intervalMs);  // (a 0 interval would never let the clock move on)
        }
        Q_EMIT next->timeout();  // (it may start, stop or delete timers, this one too)
    }
    now = until;
}

// --- ClockTimer ----------------------------------------------------------------------------------------------------

ClockTimer::ClockTimer(QObject* parent): QObject(parent), clock(&Clock::steady()) {
    connect(&timer, &QTimer::timeout, this, &ClockTimer::timeout);
}

ClockTimer::~ClockTimer() { clock->detach(this); }

void ClockTimer::setClock(Clock& to) {
    stop();
    clock->detach(this);
    clock = &to;
    clock->attach(this);
}

void ClockTimer::setSingleShot(bool s) {
    singleShot = s;
    timer.setSingleShot(s);
}

void ClockTimer::setInterval(int ms) {
    intervalMs = ms;
    timer.setInterval(ms);
}

void ClockTimer::start() {
    if (clock->firesTimers()) {
        active = true;
        dueMs = clock->nowMs() + intervalMs;
    } else {
        timer.start();
    }
}

void ClockTimer::start(int ms) {
    setInterval(ms);
    start();
}

void ClockTimer::stop() {
    active = false;
    timer.stop();
}

bool ClockTimer::isActive() const { return clock->firesTimers() ? active : timer.isActive(); }

}  // namespace xqt
