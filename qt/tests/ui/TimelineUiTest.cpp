/*
 * xournal-qt: the replay of a document's timeline in the real window (qt/docs/timeline.md, "Replay"): ⋮ → View →
 * Replay the writing, the play bar at the bottom (play, the slider, the speed, ✕ and Esc), read-only (the pen writes
 * nothing, a tap on ink goes to its moment), the document exactly as it was afterwards; with a recording (the fake
 * speaker), the recording is heard where it is on the bar, and the playback pill starts the replay at its moment.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
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
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {
constexpr int64_t T0 = 1791100800000;  // 2026-10-04 08:00:00 UTC

class TimelineUiTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        xqt::audio::useFakeDevices(true);
        xqt::audio::fake::reset();
        xqt::audio::setAppFolder(fs::path(tmp.filePath("audio").toStdString()));
        xqt::timeline::setClock([this] { return clock; });
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        window->resize(1600, 1000);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();
        engine.reset();
        controller.reset();
        xqt::timeline::setClock({});
        xqt::audio::setAppFolder({});
        xqt::audio::fake::reset();
        xqt::audio::useFakeDevices(false);
    }
    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    static bool until(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
        return done();
    }
    template <typename T = QObject>
    T* find(const char* name) const {
        return window->findChild<T*>(name);
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
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
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
