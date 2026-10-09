#include "Watchdog.h"

#include <QJSEngine>

namespace xqt::plugins {

Watchdog::Watchdog(): thread([this] { run(); }) {}

Watchdog::~Watchdog() {
    {
        std::lock_guard lock(mutex);
        quit = true;
    }
    wake.notify_all();
    thread.join();
}

void Watchdog::arm(QJSEngine* e, std::chrono::milliseconds limit) {
    {
        std::lock_guard lock(mutex);
        if (depth++ > 0) {
            return;  // (a callback inside a call: the outer one's clock goes on)
        }
        engine = e;
        deadline = std::chrono::steady_clock::now() + limit;
        paused = false;
        hasFired = false;
    }
    wake.notify_all();
}

bool Watchdog::disarm() {
    QJSEngine* e = nullptr;
    bool interrupted = false;
    {
        std::lock_guard lock(mutex);
        if (depth == 0 || --depth > 0) {
            return false;
        }
        e = engine;
        engine = nullptr;
        interrupted = hasFired;
    }
    wake.notify_all();
    if (e) {
        e->setInterrupted(false);  // (also when it fired just as the call ended)
    }
    return interrupted;
}

void Watchdog::pause() {
    std::lock_guard lock(mutex);
    if (engine && !paused) {
        paused = true;
        left = deadline - std::chrono::steady_clock::now();
    }
}

void Watchdog::resume() {
    {
        std::lock_guard lock(mutex);
        if (!engine || !paused) {
            return;
        }
        paused = false;
        deadline = std::chrono::steady_clock::now() + std::max(left, std::chrono::steady_clock::duration(
                                                                             std::chrono::milliseconds(200)));
    }
    wake.notify_all();
}

bool Watchdog::fired() const {
    std::lock_guard lock(mutex);
    return hasFired;
}

void Watchdog::run() {
    std::unique_lock lock(mutex);
    while (!quit) {
        if (!engine || paused || hasFired) {
            wake.wait(lock);
            continue;
        }
        if (wake.wait_until(lock, deadline) == std::cv_status::timeout && engine && !paused && !hasFired &&
            std::chrono::steady_clock::now() >= deadline) {
            hasFired = true;
            engine->setInterrupted(true);
        }
    }
}

}  // namespace xqt::plugins
