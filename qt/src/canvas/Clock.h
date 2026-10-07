/*
 * xournal-qt: the time the canvas goes by: input timing (taps, flicks, palm rejection, a pen held still), the
 * momentum and the page animations, the zoom settling, the visibility updates.
 *
 * By default the steady clock and real timers. A test gives the view and the input a ManualClock instead and moves
 * it on by hand: the timers on it fire as it passes their time, in order, each with the clock at its time. So the
 * canvas tests neither sleep nor depend on how fast the machine is (infra review 2026-10, §7.2).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QObject>
#include <QTimer>

namespace xqt {

class ClockTimer;

class Clock {
public:
    virtual ~Clock() = default;
    /// Milliseconds on a monotonic clock (only differences mean something)
    virtual double nowMs() const = 0;
    /// The steady clock (std::chrono::steady_clock): what the canvas runs on unless it is given another one
    static Clock& steady();

private:
    friend class ClockTimer;
    /// Whether this clock fires its timers itself (else each timer is a QTimer)
    virtual bool firesTimers() const { return false; }
    virtual void attach(ClockTimer*) {}
    virtual void detach(ClockTimer*) {}
};

/// A clock that stands still until it is moved on (tests). It starts well after 0, as a machine that has been up a
/// while does (times the canvas keeps from before anything happened are 0).
class ManualClock final: public Clock {
public:
    explicit ManualClock(double startMs = 1e6): now(startMs) {}
    ~ManualClock() override;
    ManualClock(const ManualClock&) = delete;
    ManualClock& operator=(const ManualClock&) = delete;

    double nowMs() const override { return now; }
    /// Moves on by `ms`: the timers due on the way fire in the order of their times, each with the clock at its time
    /// (a repeating timer as often as it falls due).
    void advance(double ms);
    /// Whether one of its timers runs
    bool hasActiveTimers() const;

private:
    bool firesTimers() const override { return true; }
    void attach(ClockTimer* timer) override;
    void detach(ClockTimer* timer) override;

    double now;
    std::vector<ClockTimer*> timers;
};

/// A QTimer on a Clock, with the part of QTimer's interface the canvas uses.
class ClockTimer: public QObject {
    Q_OBJECT
public:
    explicit ClockTimer(QObject* parent = nullptr);
    ~ClockTimer() override;
    ClockTimer(const ClockTimer&) = delete;
    ClockTimer& operator=(const ClockTimer&) = delete;

    /// The clock it runs on (stops it)
    void setClock(Clock& clock);
    void setSingleShot(bool singleShot);
    void setInterval(int ms);
    int interval() const { return intervalMs; }
    void setTimerType(Qt::TimerType type) { timer.setTimerType(type); }

    void start();
    void start(int ms);
    void stop();
    bool isActive() const;

Q_SIGNALS:
    void timeout();

private:
    friend class ManualClock;
    Clock* clock;
    QTimer timer;  ///< on the steady clock
    bool singleShot = false;
    int intervalMs = 0;
    bool active = false;  ///< on a manual clock
    double dueMs = 0;     ///< on a manual clock
};

}  // namespace xqt
