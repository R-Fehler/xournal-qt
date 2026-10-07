/*
 * xournal-qt: time in the canvas tests. The view and the input run on a ManualClock (Clock.h): the tests move it on
 * instead of sleeping, so a long press, a flick, the momentum, the zoom settling and the page animations take no real
 * time and do not depend on how busy the machine is.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>

#include <QCoreApplication>

#include "render/RenderService.h"
#include "session/AppContext.h"

#include "Clock.h"

namespace xqt::test {

/// The event loop runs until the renders and what they posted are done (no time passes on the canvas's clock). The
/// render service holds back renders while the zoom changes, for 300 ms of real time (RenderService::blockRerenderZoom):
/// that hold is lifted, as waiting it out did before the canvas had a clock of its own.
inline void drainEvents(AppContext& app) {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        app.getRenderService()->unblockRerenderZoom();
        app.getRenderService()->waitForIdle();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

/// `ms` pass on the canvas's clock, a frame at a time, the event loop drained after each step
inline void passTime(ManualClock& clock, AppContext& app, int ms, int stepMs = 8) {
    drainEvents(app);
    for (int done = 0; done < ms;) {
        const int step = std::min(stepMs, ms - done);
        clock.advance(step);
        done += step;
        drainEvents(app);
    }
}

}  // namespace xqt::test
