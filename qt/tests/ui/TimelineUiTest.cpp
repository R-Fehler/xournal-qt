/*
 * xournal-qt: the replay of a document's timeline in the real window (qt/docs/features/timeline.md, "Replay"): ⋮ → View
 * → Replay the writing, the play bar at the bottom (play, the slider, the speed, ✕ and Esc), read-only (the pen writes
 * nothing, a tap on ink goes to its moment), the document exactly as it was afterwards; with a recording (the fake
 * speaker), the recording is heard where it is on the bar, and the playback pill starts the replay at its moment; the
 * play bar's speaker switches the recordings on and off (a setting, the key A), and what is heard follows seeks,
 * pauses, the slider and the boundaries between recordings.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>
#include <shared_mutex>

#include <gtest/gtest.h>

#include "audio/AudioDevice.h"
#include "audio/AudioFiles.h"
#include "audio/FakeAudio.h"
#include "canvas/CanvasView.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/ElementTimes.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "shell/ToolboxModel.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "UiFixture.h"

namespace fs = std::filesystem;

namespace {
constexpr int64_t T0 = 1791100800000;  // 2026-10-04 08:00:00 UTC

class TimelineUiTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        xqt::audio::useFakeDevices(true);
        xqt::audio::fake::reset();
        xqt::audio::setAppFolder(fs::path(tmp.filePath("audio").toStdString()));
        xqt::timeline::setClock([this] { return clock; });
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        settings()->set("replayHintSeen", true);  // (the hint's own test shows it)
        settings()->set("touchProfile", "auto");
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1600, 1000)}));
        wait(100);
    }
    void TearDown() override {
        settings()->resetLayoutChoices();
        settings()->set("touchProfile", "auto");
        closeApp();
        xqt::timeline::setClock({});
        xqt::audio::setAppFolder({});
        xqt::audio::fake::reset();
        xqt::audio::useFakeDevices(false);
    }
    /// By its objectName: among the window's objects, else in the tree of items (a Repeater's delegates)
    template <typename T = QObject>
    T* find(const char* name) const {
        if (T* t = window->findChild<T*>(name)) {
            return t;
        }
        if constexpr (std::is_base_of_v<QQuickItem, T>) {
            std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
                if (i->objectName() == QLatin1String(name)) {
                    return i;
                }
                for (QQuickItem* c: i->childItems()) {
                    if (QQuickItem* f = walk(c)) {
                        return f;
                    }
                }
                return nullptr;
            };
            return qobject_cast<T*>(walk(window->contentItem()));
        }
        return nullptr;
    }
    /// Shown: visible all the way up, and of some size
    static bool shown(QQuickItem* item) { return item && item->isVisible() && item->width() > 0 && item->height() > 0; }
    static QRectF rectOf(QQuickItem* item) { return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())); }
    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }
    void resize(int w, int h) {
        window->resize(w, h);
        until([&] { return window->width() == w && window->height() == h; });
        wait(300);  // (the size class, then the plans settle)
    }
    /// Tools outside the toolbox (qt/docs/features/toolbox.md): undo and redo in the command bar, the phone dock's own
    /// buttons (a text document's). None of it is there beside the toolbox.
    QStringList classicToolsShown() const {
        QStringList out;
        for (const char* name: {"toolUndoButton", "toolRedoButton", "dockToolsButton", "dockUndoButton"}) {
            if (shown(find<QQuickItem>(name))) {
                out << name;
            }
        }
        return out;
    }
    QObject* timeline() const { return controller->property("timeline").value<QObject*>(); }
    QObject* audio() const { return controller->property("audio").value<QObject*>(); }
    xqt::DocumentSession* current() const { return controller->tabManager().currentSession(); }
    xqt::CanvasView* view() const { return controller->tabManager().currentView(); }
    QPoint onPage(double x, double y) const {
        auto* canvas = find<QQuickItem>("canvas");
        const QPointF at = view()->pageViewRect(0).topLeft() + QPointF(x, y) * view()->getViewController().zoom();
        return canvas->mapToScene(at).toPoint();
    }
    void drag(QPointF from, QPointF to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onPage(from.x(), from.y()));
        for (int k = 1; k <= 12; ++k) {
            const QPointF p = from + (to - from) * k / 12.0;
            QTest::mouseMove(window, onPage(p.x(), p.y()));
            wait(5);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, onPage(to.x(), to.y()));
        wait(100);
    }
    size_t elementCount() const {
        std::shared_lock lock(*current()->getDocument());
        return current()->getDocument()->getPage(0)->getSelectedLayer()->getElementsView().size();
    }
    /// The document as a .xopp (unpacked): what it is, to the byte
    QByteArray xml(const char* name) {
        const fs::path file = fs::path(tmp.filePath(name).toStdString());
        EXPECT_TRUE(xqt::DocumentSession::writeDocument(*current()->getDocument(), file).ok);
        QProcess gz;
        gz.start("gzip", {"-dc", QString::fromStdString(file.string())});
        gz.waitForFinished();
        return gz.readAllStandardOutput();
    }
    static void click(QObject* button) { QMetaObject::invokeMethod(button, "clicked"); }
    /// Starts the replay (⋮ → View → Replay the writing) and waits for its bar
    void startReplay() {
        QMetaObject::invokeMethod(timeline(), "start");
        ASSERT_TRUE(timeline()->property("active").toBool());
        ASSERT_TRUE(until([&] { return shown(find<QQuickItem>("timelinePane")); }));
        wait(200);  // (the layout settles)
    }
    /// The bar's controls a finger uses
    static constexpr const char* CONTROLS[] = {"timelinePlay", "timelinePreviousMark", "timelineNextMark",
                                               "timelineSpeed", "timelineClose"};
    qint64 position() const { return timeline()->property("position").toLongLong(); }
    int shown() const { return timeline()->property("shownCount").toInt(); }
    void seek(qint64 ms) { QMetaObject::invokeMethod(timeline(), "seek", Q_ARG(qint64, ms)); }

    /// Two strokes, the second 3 s after the first
    void writeTwoStrokes() {
        controller->newDocument();
        view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 500, 500));
        wait(200);
        controller->selectTool("pen");
        clock = T0;
        drag({100, 150}, {300, 150});
        clock = T0 + 3000;
        drag({100, 250}, {300, 250});
        ASSERT_EQ(elementCount(), 2u);
    }

    QTemporaryDir tmp;
    int64_t clock = T0;
};
}  // namespace

// ⋮ → View → Replay the writing: the play bar; the slider and play go through the writing; the pen writes nothing;
// ✕ (and Esc) bring the document back exactly as it was, not modified
TEST_F(TimelineUiTest, enterScrubPlayAndLeave) {
    writeTwoStrokes();
    const QByteArray before = xml("before.xopp");
    const bool modified = current()->isModified();
    const bool canUndo = current()->getUndoRedoHandler()->canUndo();
    const std::string undoText = current()->getUndoRedoHandler()->undoDescription();
    const auto revision = current()->pageRevision(0);

    auto* bar = find<QQuickItem>("timelineBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisible());
    QObject* item = find<QObject>("replayItem");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    ASSERT_TRUE(until([&] { return timeline()->property("active").toBool(); }));
    EXPECT_TRUE(until([&] { return bar->isVisible(); }));
    EXPECT_TRUE(view()->isReadingOnly());
    EXPECT_TRUE(current()->isReadOnly());
    EXPECT_FALSE(controller->property("canUndo").toBool()) << "nothing to undo while replaying";
    EXPECT_EQ(timeline()->property("elementCount").toInt(), 2);
    EXPECT_EQ(position(), 0);
    EXPECT_FALSE(timeline()->property("timeText").toString().isEmpty());
    EXPECT_EQ(timeline()->property("marks").toList().size(), 1) << "one session";

    // The slider: between the two strokes, one is there
    auto* slider = find<QQuickItem>("timelineSlider");
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->property("to").toLongLong(), timeline()->property("duration").toLongLong());
    slider->setProperty("value", 2500);
    QMetaObject::invokeMethod(slider, "moved");
    EXPECT_EQ(position(), 2500);
    EXPECT_EQ(shown(), 1);
    seek(0);
    EXPECT_EQ(shown(), 0);

    // Play: the second stroke comes
    click(find<QObject>("timelineSpeed"));
    EXPECT_EQ(timeline()->property("speed").toDouble(), 2.0);
    click(find<QObject>("timelinePlay"));
    EXPECT_TRUE(timeline()->property("playing").toBool());
    EXPECT_TRUE(until([&] { return shown() == 2; }, 6000));
    click(find<QObject>("timelinePlay"));
    EXPECT_FALSE(timeline()->property("playing").toBool());

    // The pen writes nothing
    drag({100, 400}, {300, 400});
    EXPECT_EQ(elementCount(), 2u);

    // ✕: the document as it was
    click(find<QObject>("timelineClose"));
    EXPECT_FALSE(timeline()->property("active").toBool());
    EXPECT_TRUE(until([&] { return !bar->isVisible(); }));
    EXPECT_FALSE(view()->isReadingOnly());
    EXPECT_FALSE(current()->isReadOnly());
    EXPECT_EQ(current()->isModified(), modified);
    EXPECT_EQ(current()->getUndoRedoHandler()->canUndo(), canUndo);
    EXPECT_EQ(current()->getUndoRedoHandler()->undoDescription(), undoText);
    EXPECT_EQ(current()->pageRevision(0), revision);
    EXPECT_EQ(xml("after.xopp"), before) << "nothing changed, to the byte";

    // Again, left with Esc; the pen writes again afterwards
    QMetaObject::invokeMethod(item, "triggered");
    ASSERT_TRUE(until([&] { return timeline()->property("active").toBool(); }));
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(until([&] { return !timeline()->property("active").toBool(); }));
    drag({100, 400}, {300, 400});
    EXPECT_EQ(elementCount(), 3u);
}

// A tap on ink goes to the moment it was written
TEST_F(TimelineUiTest, aTapOnInkGoesToItsMoment) {
    writeTwoStrokes();
    QMetaObject::invokeMethod(timeline(), "start");
    ASSERT_TRUE(timeline()->property("active").toBool());
    seek(timeline()->property("duration").toLongLong());
    wait(100);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, onPage(200, 251));
    EXPECT_TRUE(until([&] { return position() == 3000; })) << position();
    EXPECT_EQ(shown(), 1) << "the second stroke is written from there";
    // Another tab: the replay ends
    controller->newDocument();
    EXPECT_TRUE(until([&] { return !timeline()->property("active").toBool(); }));
}

// With a recording: it is heard where it is on the bar; the playback pill starts the replay at its moment
TEST_F(TimelineUiTest, theRecordingIsHeardWhereItIs) {
    controller->newDocument();
    view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 500, 500));
    wait(200);
    clock = T0;
    QMetaObject::invokeMethod(audio(), "startRecording");
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 400; }));
    controller->selectTool("pen");
    clock = T0 + audio()->property("recordedMs").toLongLong();
    drag({100, 150}, {300, 150});
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 1200; }));
    QMetaObject::invokeMethod(audio(), "stopRecording");
    ASSERT_FALSE(audio()->property("recording").toBool());

    QMetaObject::invokeMethod(timeline(), "start");
    ASSERT_TRUE(timeline()->property("active").toBool());
    const QVariantList tracks = timeline()->property("tracks").toList();
    ASSERT_EQ(tracks.size(), 1);
    EXPECT_TRUE(tracks[0].toMap()["found"].toBool());
    EXPECT_EQ(tracks[0].toMap()["at"].toLongLong(), 0);
    click(find<QObject>("timelinePlay"));
    EXPECT_TRUE(until([&] { return timeline()->property("hearing").toBool(); }));
    EXPECT_TRUE(audio()->property("playing").toBool());
    EXPECT_FALSE(find<QQuickItem>("playbackPill")->isVisible()) << "the play bar is the one";
    EXPECT_TRUE(until([&] { return shown() == 1; }));
    click(find<QObject>("timelineClose"));
    EXPECT_TRUE(until([&] { return !audio()->property("playing").toBool(); })) << "what the replay played stops";

    // The playback pill: the replay from the moment heard
    QMetaObject::invokeMethod(audio(), "play", Q_ARG(QString, tracks[0].toMap()["title"].toString() + ".ogg"),
                              Q_ARG(qint64, 600));
    ASSERT_TRUE(until([&] { return find<QQuickItem>("playbackPill")->isVisible(); }));
    click(find<QObject>("playbackReplay"));
    ASSERT_TRUE(until([&] { return timeline()->property("active").toBool(); }));
    EXPECT_GE(position(), 600);
    EXPECT_LT(position(), 1500);
    EXPECT_TRUE(timeline()->property("playing").toBool());
    QMetaObject::invokeMethod(timeline(), "stop");
}

// The replay's audio (the author: "for the replay button I want to be able to toggle the recorded audio"): the speaker
// on the play bar is shown only when the document has recordings; on, what is heard follows the replay (a seek into
// the other recording plays that one at its moment, a gap between them is silent, pause and the held slider are
// silent); off (the button or A), nothing is heard and the choice is kept as a setting
TEST_F(TimelineUiTest, theRecordingsAreSwitchedOnAndOffInThePlayBar) {
    writeTwoStrokes();
    startReplay();
    EXPECT_FALSE(timeline()->property("hasRecordings").toBool());
    EXPECT_FALSE(find<QQuickItem>("timelineAudio")->isVisible()) << "no recordings: no speaker";
    QMetaObject::invokeMethod(timeline(), "stop");

    // Two recordings a minute apart, each with a stroke written 400 ms into it
    controller->newDocument();
    view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 500, 500));
    wait(200);
    controller->selectTool("pen");
    for (int k = 0; k < 2; ++k) {
        clock = T0 + k * 60000;
        QMetaObject::invokeMethod(audio(), "startRecording");
        ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 400; }));
        clock = T0 + k * 60000 + audio()->property("recordedMs").toLongLong();
        drag({100, 150.0 + 100 * k}, {300, 150.0 + 100 * k});
        ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 1500; }));
        QMetaObject::invokeMethod(audio(), "stopRecording");
        ASSERT_FALSE(audio()->property("recording").toBool());
    }
    startReplay();
    ASSERT_TRUE(timeline()->property("hasRecordings").toBool());
    QQuickItem* speaker = find<QQuickItem>("timelineAudio");
    ASSERT_TRUE(shown(speaker));
    EXPECT_TRUE(timeline()->property("audioOn").toBool()) << "on by default";
    const QVariantList tracks = timeline()->property("tracks").toList();
    ASSERT_EQ(tracks.size(), 2);
    const qint64 first = tracks[0].toMap()["at"].toLongLong(), second = tracks[1].toMap()["at"].toLongLong();
    const qint64 firstEnd = first + tracks[0].toMap()["length"].toLongLong();
    ASSERT_GT(second, firstEnd + 500) << "a gap between them";
    const auto heard = [&] { return timeline()->property("hearing").toBool() && audio()->property("playing").toBool(); };
    const auto playName = [&] { return audio()->property("playName").toString(); };

    click(find<QObject>("timelinePlay"));
    ASSERT_TRUE(until(heard));
    const QString firstName = playName();
    // A seek into the second recording: that one, at its moment
    seek(second + 300);
    ASSERT_TRUE(until([&] { return heard() && playName() != firstName; }));
    EXPECT_NEAR(audio()->property("playPositionMs").toLongLong(), 300, 250);
    // The gap between them: silent
    seek(firstEnd + 100);
    EXPECT_TRUE(until([&] { return !audio()->property("playing").toBool(); }));
    EXPECT_FALSE(timeline()->property("hearing").toBool());
    // Paused: silent; played again: heard
    seek(first + 200);
    ASSERT_TRUE(until(heard));
    click(find<QObject>("timelinePlay"));
    EXPECT_TRUE(until([&] { return !audio()->property("playing").toBool(); }));
    click(find<QObject>("timelinePlay"));
    ASSERT_TRUE(until(heard));
    // The slider held: silent; let go: heard from there
    QMetaObject::invokeMethod(timeline(), "setScrubbing", Q_ARG(bool, true));
    EXPECT_FALSE(audio()->property("playing").toBool());
    seek(second + 100);
    EXPECT_FALSE(audio()->property("playing").toBool());
    QMetaObject::invokeMethod(timeline(), "setScrubbing", Q_ARG(bool, false));
    ASSERT_TRUE(until([&] { return heard() && playName() != firstName; }));

    // Off: the button
    click(speaker);
    EXPECT_FALSE(timeline()->property("audioOn").toBool());
    EXPECT_TRUE(until([&] { return !audio()->property("playing").toBool(); }));
    EXPECT_FALSE(timeline()->property("hearing").toBool());
    seek(first + 100);
    wait(150);
    EXPECT_FALSE(audio()->property("playing").toBool()) << "off: nothing is heard";
    EXPECT_TRUE(timeline()->property("playing").toBool()) << "the ink goes on";
    EXPECT_FALSE(QSettings().value("replay/audio").toBool()) << "remembered";
    // On again: A
    QTest::keyClick(window, Qt::Key_A);
    EXPECT_TRUE(timeline()->property("audioOn").toBool());
    EXPECT_TRUE(until(heard));
    EXPECT_TRUE(QSettings().value("replay/audio").toBool());
    QMetaObject::invokeMethod(timeline(), "stop");
    EXPECT_TRUE(until([&] { return !audio()->property("playing").toBool(); }));
    QSettings().remove("replay/audio");
}

// The author's test of 0.6.0: "when replay is on the classic toolbar appears again". A replay hides the tools and
// brings back nothing but the toolbox, neither while it replays nor after it: in a window, in full screen and in a
// phone's chrome (its dock)
TEST_F(TimelineUiTest, theToolboxModeShowsNoClassicToolBarDuringOrAfterAReplay) {
    settings()->resetLayoutChoices();
    controller->toolboxModel()->reset();
    writeTwoStrokes();
    auto* toolbox = find<QQuickItem>("toolbox");
    ASSERT_TRUE(until([&] { return shown(toolbox); }));
    ASSERT_EQ(classicToolsShown().join(", ").toStdString(), "") << "the toolbox mode, before";

    auto replayAndLeave = [&](const char* where) {
        QMetaObject::invokeMethod(find<QObject>("replayItem"), "triggered");
        ASSERT_TRUE(until([&] { return timeline()->property("active").toBool(); })) << where;
        wait(300);  // (the tool bar's plan follows after the bindings settle)
        EXPECT_TRUE(shown(find<QQuickItem>("timelineBar"))) << where;
        EXPECT_EQ(classicToolsShown().join(", ").toStdString(), "") << where << ": no classic tool bar while it replays";
        EXPECT_FALSE(shown(toolbox)) << where << ": no tools while it replays";
        for (const char* name: {"topTools", "sideTools", "toolbarToggle", "toolbarShow", "phoneDock"}) {
            EXPECT_FALSE(shown(find<QQuickItem>(name))) << where << ": " << name << " is put away while it replays";
        }
        click(find<QObject>("timelineClose"));
        ASSERT_TRUE(until([&] { return !timeline()->property("active").toBool(); })) << where;
        EXPECT_TRUE(until([&] { return shown(toolbox); })) << where << ": the toolbox again";
        wait(300);
        EXPECT_EQ(classicToolsShown().join(", ").toStdString(), "") << where << ": no classic tool bar after it";
    };
    replayAndLeave("a window");

    window->setProperty("fullScreenMode", true);
    until([&] { return window->size() == window->screen()->size(); });
    wait(300);
    ASSERT_TRUE(until([&] { return shown(toolbox) && toolbox->property("floating").toBool(); }));
    replayAndLeave("full screen");
    window->setProperty("fullScreenMode", false);
    until([&] { return !toolbox->property("floating").toBool(); });

    resize(412, 915);
    ASSERT_TRUE(until([&] { return window->property("phoneChrome").toBool(); }));
    ASSERT_TRUE(until([&] { return shown(toolbox) && toolbox->property("compact").toBool(); })) << "in the dock";
    replayAndLeave("a phone");
    EXPECT_TRUE(toolbox->property("compact").toBool()) << "the phone's dock again";
}

// The play bar on a desktop (the author's test of 0.6.0: "not easy to see and understand for the first time user"): a
// title, the time as "12:04 · 3 Oct, 14:20", one row with the slider between the buttons, a visible track with its
// elapsed part filled, the session marks; 40 px buttons with the mouse, 48 px with the touch profile
TEST_F(TimelineUiTest, thePlayBarSaysWhatItIsAndItsControlsAreSizedForTheInput) {
    writeTwoStrokes();
    startReplay();
    auto* bar = find<QQuickItem>("timelineBar");
    auto* slider = find<QQuickItem>("timelineSlider");
    EXPECT_FALSE(bar->property("twoRows").toBool()) << "one row on a desktop";
    EXPECT_TRUE(shown(find<QQuickItem>("timelineTitle")));
    auto* time = find<QQuickItem>("timelineTime");
    ASSERT_TRUE(shown(time));
    seek(2500);
    wait(50);
    const QString text = time->property("text").toString();
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^0:02 · \\d{1,2} \\S+, \\d\\d:\\d\\d$")).match(text).hasMatch())
            << text.toStdString() << ": the bar's time and the clock time of the moment";
    EXPECT_FALSE(time->property("truncated").toBool());
    // The track and its elapsed part
    auto* track = find<QQuickItem>("timelineTrack");
    auto* elapsed = find<QQuickItem>("timelineElapsed");
    ASSERT_TRUE(shown(track));
    ASSERT_TRUE(shown(elapsed));
    EXPECT_NEAR(elapsed->width() / track->width(), 2500.0 / timeline()->property("duration").toDouble(), 0.03);
    EXPECT_TRUE(shown(find<QQuickItem>("timelineMark"))) << "the session's mark";
    // The mouse: 40 px; the slider between the buttons, in the same row
    for (const char* name: CONTROLS) {
        auto* c = find<QQuickItem>(name);
        ASSERT_TRUE(shown(c)) << name;
        EXPECT_GE(c->width(), 40) << name;
        EXPECT_GE(c->height(), 40) << name;
        EXPECT_NEAR(rectOf(c).center().y(), rectOf(slider).center().y(), 6) << name << ": one row";
    }
    EXPECT_GT(rectOf(slider).width(), 300);
    // The touch profile: 48 px, a larger handle
    settings()->set("touchProfile", "on");
    ASSERT_TRUE(until([&] { return find<QQuickItem>("timelineClose")->width() >= 48; }));
    wait(100);  // (the rows are laid out again)
    for (const char* name: CONTROLS) {
        auto* c = find<QQuickItem>(name);
        EXPECT_GE(c->width(), 48) << name;
        EXPECT_GE(c->height(), 48) << name;
    }
    EXPECT_GE(slider->height(), 48) << "the slider takes the finger in the row's whole height";
    EXPECT_GE(find<QQuickItem>("timelineHandle")->width(), 24);
    // Inside the page, above its bottom
    auto* canvas = find<QQuickItem>("canvas");
    EXPECT_LE(rectOf(bar).bottom(), rectOf(canvas).bottom() - 8);
    EXPECT_GE(rectOf(bar).left(), rectOf(canvas).left());
    EXPECT_LE(rectOf(bar).right(), rectOf(canvas).right());
}

// The first replay explains itself, once: a card above the bar (what replay is, how it is used), dismissed with
// "Got it"; remembered in the settings; the title shows it again
TEST_F(TimelineUiTest, theFirstReplayShowsAHintOnce) {
    settings()->set("replayHintSeen", false);
    writeTwoStrokes();
    startReplay();
    auto* hint = find<QQuickItem>("timelineHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_TRUE(shown(hint)) << "the first replay";
    EXPECT_TRUE(settings()->get("replayHintSeen").toBool()) << "remembered";
    EXPECT_LE(rectOf(hint).bottom(), rectOf(find<QQuickItem>("timelinePane")).top()) << "above the bar";
    click(find<QObject>("timelineHintClose"));
    EXPECT_TRUE(until([&] { return !shown(hint); }));
    click(find<QObject>("timelineClose"));
    ASSERT_TRUE(until([&] { return !timeline()->property("active").toBool(); }));
    startReplay();
    wait(100);
    EXPECT_FALSE(shown(hint)) << "once";
    click(find<QObject>("timelineTitle"));
    EXPECT_TRUE(until([&] { return shown(hint); })) << "the title shows it again";
    click(find<QObject>("timelineClose"));
    ASSERT_TRUE(until([&] { return !timeline()->property("active").toBool(); }));
    startReplay();
    wait(100);
    EXPECT_FALSE(shown(hint));
    QMetaObject::invokeMethod(timeline(), "stop");
}

namespace {
/// On a phone's screen (TimelinePhone.ui@phone: an off-screen screen of 412 × 915, so full screen stays a phone), with
/// the touch profile and a navigation bar at the bottom
class TimelinePhoneTest: public TimelineUiTest {
protected:
    void SetUp() override {
        if (QGuiApplication::primaryScreen()->size() != QSize(412, 915)) {
            GTEST_SKIP() << "needs a phone's screen (TimelinePhone.ui@phone)";
        }
        TimelineUiTest::SetUp();
        settings()->set("touchProfile", "on");
        resize(412, 915);
        ASSERT_TRUE(until([&] { return window->property("phoneChrome").toBool(); }));
        find<QObject>("safeInsets")->setProperty("bottom", 40);
    }
    void TearDown() override {
        if (controller) {
            TimelineUiTest::TearDown();
        }
    }
    /// The bar on a phone: two rows (the slider on its own), touch-sized, above the navigation bar, off the edges
    void checkTheBar(const char* where) {
        auto* bar = find<QQuickItem>("timelineBar");
        auto* pane = find<QQuickItem>("timelinePane");
        auto* slider = find<QQuickItem>("timelineSlider");
        EXPECT_TRUE(bar->property("twoRows").toBool()) << where;
        for (const char* name: CONTROLS) {
            auto* c = find<QQuickItem>(name);
            ASSERT_TRUE(shown(c)) << where << " " << name;
            EXPECT_GE(c->width(), 48) << where << " " << name;
            EXPECT_GE(c->height(), 48) << where << " " << name;
            EXPECT_GE(rectOf(c).top(), rectOf(slider).bottom() - 1) << where << " " << name << ": the slider's own row";
            EXPECT_LE(rectOf(c).right(), rectOf(pane).right()) << where << " " << name;
            EXPECT_LE(rectOf(c).bottom(), rectOf(pane).bottom()) << where << " " << name << ": inside the bar";
        }
        EXPECT_GE(slider->height(), 48) << where;
        EXPECT_GE(rectOf(slider).top(), rectOf(pane).top()) << where << ": inside the bar";
        EXPECT_GT(slider->width(), 250) << where;
        EXPECT_FALSE(find<QQuickItem>("timelineTime")->property("truncated").toBool())
                << where << ": " << find<QQuickItem>("timelineTime")->property("text").toString().toStdString();
        const double safeBottom = window->property("safeBottom").toDouble();
        EXPECT_LE(rectOf(pane).bottom(), window->height() - safeBottom - 8) << where << ": above the navigation bar";
        EXPECT_GE(rectOf(pane).left(), 12) << where << ": off the edge (the back gesture)";
        EXPECT_LE(rectOf(pane).right(), window->width() - 12) << where;
        EXPECT_FALSE(shown(find<QQuickItem>("phoneDock"))) << where << ": the dock is put away";
        EXPECT_FALSE(shown(find<QQuickItem>("viewPill"))) << where;
    }
    /// A finger drags the handle: the replay's time moves, the page does not scroll
    void dragTheHandle(const char* where) {
        static QPointingDevice* finger = QTest::createTouchDevice();
        auto* canvas = find<QQuickItem>("canvas");
        auto* handle = find<QQuickItem>("timelineHandle");
        seek(0);
        wait(50);
        const double x0 = canvas->property("contentX").toDouble();
        const double y0 = canvas->property("contentY").toDouble();
        const QPoint from = rectOf(handle).center().toPoint();
        QTest::touchEvent(window, finger).press(1, from);
        for (int k = 1; k <= 10; ++k) {
            wait(16);
            QTest::touchEvent(window, finger).move(1, from + QPoint(16 * k, -3 * k));
        }
        wait(16);
        EXPECT_TRUE(shown(find<QQuickItem>("timelineBubble"))) << where << ": the time above the finger";
        QTest::touchEvent(window, finger).release(1, from + QPoint(160, -30));
        wait(100);
        EXPECT_GT(position(), 0) << where << ": the replay's time moved";
        EXPECT_NEAR(canvas->property("contentX").toDouble(), x0, 0.5) << where << ": the page did not scroll";
        EXPECT_NEAR(canvas->property("contentY").toDouble(), y0, 0.5) << where << ": the page did not scroll";
        EXPECT_FALSE(shown(find<QQuickItem>("timelineBubble"))) << where;
    }
};
}  // namespace

TEST_F(TimelinePhoneTest, onAPhoneThePlayBarIsTwoTouchSizedRowsAboveTheNavigationBar) {
    writeTwoStrokes();
    // (a longer replay: the handle has room to go)
    clock = T0 + 60000;
    drag({100, 350}, {300, 350});
    startReplay();
    checkTheBar("the phone's chrome");
    dragTheHandle("the phone's chrome");
    // Full screen on the phone
    QMetaObject::invokeMethod(timeline(), "stop");
    window->setProperty("fullScreenMode", true);
    until([&] { return window->size() == window->screen()->size(); });
    wait(300);
    ASSERT_TRUE(window->property("phoneLayout").toBool());
    startReplay();
    checkTheBar("full screen");
    dragTheHandle("full screen");
    QMetaObject::invokeMethod(timeline(), "stop");
    window->setProperty("fullScreenMode", false);
}
