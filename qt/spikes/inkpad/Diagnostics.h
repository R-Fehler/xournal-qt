/*
 * xournal-qt M0 spike: input event logging (JSONL), event-rate and latency statistics.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <deque>
#include <mutex>
#include <vector>

#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QEvent;
class QInputDevice;

/// Monotonic clock in milliseconds (CLOCK_MONOTONIC on Linux, the clock Wayland event times use on KWin).
double monotonicMs();

QString deviceTypeName(int type);
QString pointerTypeName(int type);
QString eventTypeName(int type);

/// Writes one JSON object per line. Thread-safe.
class EventLog {
public:
    explicit EventLog(const QString& path);
    bool isOpen() const { return file.isOpen(); }
    QString path() const { return file.fileName(); }
    void write(const QJsonObject& obj);
    /// Logs any input event (tablet, touch, mouse, wheel, native gesture, proximity, enter/leave).
    void logEvent(const QEvent* e, const char* receiver);

private:
    std::mutex mtx;
    QFile file;
};

/// Rolling latency samples (ms) with average / 95th percentile.
class LatencyStats {
public:
    void add(double ms);
    QString summary() const;

private:
    mutable std::mutex mtx;
    std::deque<double> samples;
};

/// Counts events in a sliding one-second window.
class RateCounter {
public:
    void tick();
    double hz() const;

private:
    std::deque<double> times;
};

/// Collects everything the HUD shows and writes the startup environment report.
QJsonObject environmentReport();
QString environmentSummary();
