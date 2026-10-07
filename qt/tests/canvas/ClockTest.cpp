/*
 * xournal-qt: the canvas's clock (Clock.h): timers on a ManualClock fire as it is moved on, in order, each with the
 * clock at its time; on the steady clock they are QTimers.
 *
 * @license GNU GPLv2 or later
 */
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include "Clock.h"

using namespace xqt;

TEST(ClockTest, timersOnAManualClockFireInOrderAtTheirTime) {
    ManualClock clock(1000);
    ClockTimer once;
    ClockTimer repeating;
    once.setClock(clock);
    repeating.setClock(clock);
    once.setSingleShot(true);
    std::vector<std::pair<char, double>> fired;
    QObject::connect(&once, &ClockTimer::timeout, [&] { fired.emplace_back('o', clock.nowMs()); });
    QObject::connect(&repeating, &ClockTimer::timeout, [&] { fired.emplace_back('r', clock.nowMs()); });
    once.start(25);
    repeating.start(10);
    EXPECT_TRUE(once.isActive());
    EXPECT_TRUE(clock.hasActiveTimers());

    clock.advance(9);
    EXPECT_TRUE(fired.empty()) << "nothing is due yet";
    clock.advance(21);  // (to 1030)
    const std::vector<std::pair<char, double>> expected{{'r', 1010}, {'r', 1020}, {'o', 1025}, {'r', 1030}};
    EXPECT_EQ(fired, expected);
    EXPECT_DOUBLE_EQ(clock.nowMs(), 1030);
    EXPECT_FALSE(once.isActive()) << "a single shot fires once";
    EXPECT_TRUE(repeating.isActive());

    repeating.stop();
    fired.clear();
    clock.advance(100);
    EXPECT_TRUE(fired.empty());
    EXPECT_FALSE(clock.hasActiveTimers());
}

TEST(ClockTest, aTimerStartedWhileTheClockMovesOnFiresInTheSameMove) {
    ManualClock clock(0);
    ClockTimer first;
    ClockTimer second;
    first.setClock(clock);
    second.setClock(clock);
    first.setSingleShot(true);
    second.setSingleShot(true);
    double secondAt = -1;
    QObject::connect(&first, &ClockTimer::timeout, [&] { second.start(5); });
    QObject::connect(&second, &ClockTimer::timeout, [&] { secondAt = clock.nowMs(); });
    first.start(10);
    clock.advance(20);
    EXPECT_DOUBLE_EQ(secondAt, 15) << "started at 10, due 5 later";
}

TEST(ClockTest, theSteadyClockRunsItsTimersAsQTimers) {
    ClockTimer timer;
    timer.setSingleShot(true);
    bool fired = false;
    QObject::connect(&timer, &ClockTimer::timeout, [&] { fired = true; });
    const double before = Clock::steady().nowMs();
    timer.start(1);
    EXPECT_TRUE(timer.isActive());
    QElapsedTimer waited;
    waited.start();
    while (!fired && waited.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    EXPECT_TRUE(fired);
    EXPECT_GE(Clock::steady().nowMs(), before + 1);
}
