/*
 * xournal-qt: stops a plugin's JavaScript that runs too long (qt/docs/decisions/0008-js-plugins.md).
 *
 * A helper thread waits while a call into a plugin runs on the UI thread; when the call is not done after the limit
 * (2 s), it interrupts the engine (QJSEngine::setInterrupted, thread-safe): the script stops with an error at once and
 * the host rolls back what it changed. The time a dialog or a permission question is open does not count (pause and
 * resume around the nested event loop).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class QJSEngine;

namespace xqt::plugins {

class Watchdog {
public:
    Watchdog();
    ~Watchdog();
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    /// Watches `engine` for `limit` from now (one call at a time; a nested arm is ignored and counted)
    void arm(QJSEngine* engine, std::chrono::milliseconds limit);
    /// The call is done. True when it was interrupted (the engine's flag is cleared again).
    bool disarm();
    /// Stops the clock (a dialog is open) and starts it again with the time that was left
    void pause();
    void resume();
    /// It interrupted the call being watched
    bool fired() const;

private:
    void run();

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread thread;
    QJSEngine* engine = nullptr;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::duration left{};
    int depth = 0;
    bool paused = false;
    bool hasFired = false;
    bool quit = false;
};

}  // namespace xqt::plugins
