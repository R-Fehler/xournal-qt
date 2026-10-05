/*
 * xournal-qt: recording and playing in the real window (qt/docs/audio.md, "In the app"), with the fake microphone and
 * speaker (their timers run as real devices would): the record button in the tool bar, the recording pill, ink tied
 * to the recording, the play tool and the playback pill, the list of recordings.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>
#include <shared_mutex>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "audio/AudioDevice.h"
#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "audio/FakeAudio.h"
#include "audio/OggVorbis.h"
#include "canvas/CanvasView.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {
class AudioUiTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        xqt::audio::useFakeDevices(true);
        xqt::audio::fake::reset();
        xqt::audio::setAppFolder(fs::path(tmp.filePath("audio").toStdString()));
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
        window->resize(1920, 1080);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();
        engine.reset();
        controller.reset();
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
    static QObject* entryOf(QObject* menu, const char* name) {
        const int n = menu ? menu->property("count").toInt() : 0;
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it && it->objectName() == name) {
                return it;
            }
        }
        return nullptr;
    }
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
    std::vector<const Element*> elements() const {
        std::vector<const Element*> out;
        std::shared_lock lock(*current()->getDocument());
        for (const Element* e: current()->getDocument()->getPage(0)->getSelectedLayer()->getElementsView()) {
            out.push_back(e);
        }
        return out;
    }
    static void click(QObject* button) { QMetaObject::invokeMethod(button, "clicked"); }
    /// An item by its name among the visual children (a Repeater's delegates have no QObject parent)
    static QQuickItem* itemIn(QQuickItem* root, const QString& name) {
        if (!root) {
            return nullptr;
        }
        if (root->objectName() == name) {
            return root;
        }
        for (QQuickItem* c: root->childItems()) {
            if (QQuickItem* found = itemIn(c, name)) {
                return found;
            }
        }
        return nullptr;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

// The record button records for this document (a memo of its page), the pill shows the time and stops it; ink
// written meanwhile plays its moment with the play tool; the playback pill plays and closes
TEST_F(AudioUiTest, recordWriteAndPlayTheMoment) {
    controller->newDocument();
    view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
    wait(200);
    auto* record = find<QQuickItem>("recordButton");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->property("offered").toBool());
    EXPECT_TRUE(until([&] { return record->isVisible(); })) << "in the tool bar of a wide window";
    auto* pill = find<QQuickItem>("recordingPill");
    ASSERT_NE(pill, nullptr);
    EXPECT_FALSE(pill->isVisible());

    click(record);
    ASSERT_TRUE(until([&] { return audio()->property("recording").toBool(); }));
    EXPECT_TRUE(audio()->property("recordingHere").toBool());
    EXPECT_TRUE(record->property("checked").toBool());
    EXPECT_TRUE(until([&] { return pill->isVisible(); }));
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 300; }));
    EXPECT_GT(audio()->property("level").toDouble(), 0.1) << "the fake tone";

    controller->selectTool("pen");
    drag({100, 200}, {300, 200});
    const auto e = elements();
    ASSERT_EQ(e.size(), 1u);
    const auto* stamped = xqt::audio::audioOf(e[0]);
    ASSERT_NE(stamped, nullptr) << "the stroke is tied to the recording";
    const std::string name = xqt::audio::nameOf(*stamped);
    EXPECT_GE(stamped->getTimestamp(), 300u);
    {
        std::shared_lock lock(*current()->getDocument());
        EXPECT_EQ(xqt::audio::memosOf(*current()->getDocument()->getPage(0)), std::vector<std::string>{name});
    }

    auto* stop = find<QObject>("recordingStop");
    ASSERT_NE(stop, nullptr);
    click(stop);
    ASSERT_TRUE(until([&] { return !audio()->property("recording").toBool(); }));
    EXPECT_TRUE(until([&] { return !pill->isVisible(); }));
    const fs::path file = fs::path(tmp.filePath("audio").toStdString()) / name;
    EXPECT_GE(xqt::audio::durationMsOf(file), 300);

    // The play tool on the stroke: from the lead-in (2 s) before its moment
    controller->selectTool("playObject");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, onPage(200, 202));
    ASSERT_TRUE(until([&] { return audio()->property("playing").toBool(); }));
    EXPECT_EQ(audio()->property("playName").toString().toStdString(), name);
    EXPECT_EQ(audio()->property("playTicks").toList().size(), 1);
    EXPECT_LE(audio()->property("playPositionMs").toLongLong(),
              static_cast<qint64>(stamped->getTimestamp()) + 500);  // (from max(0, ts - 2 s))
    auto* playback = find<QQuickItem>("playbackPill");
    ASSERT_NE(playback, nullptr);
    EXPECT_TRUE(until([&] { return playback->isVisible(); }));
    click(find<QObject>("playbackPlayPause"));
    EXPECT_TRUE(until([&] { return audio()->property("playPaused").toBool(); }));
    click(find<QObject>("playbackClose"));
    EXPECT_TRUE(until([&] { return !audio()->property("playing").toBool(); }));
    EXPECT_TRUE(until([&] { return !playback->isVisible(); }));
}

// Held, the button offers the play tool and the list of recordings; removing one there unties the ink (undoable)
TEST_F(AudioUiTest, theListOfRecordings) {
    controller->newDocument();
    wait(200);
    QMetaObject::invokeMethod(audio(), "startRecording");
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 100; }));
    QMetaObject::invokeMethod(audio(), "stopRecording");
    auto* record = find<QQuickItem>("recordButton");
    auto* menu = find<QObject>("audioMenu");
    ASSERT_NE(menu, nullptr);
    QMetaObject::invokeMethod(record, "pressAndHold");
    ASSERT_TRUE(until([&] { return menu->property("visible").toBool(); }));
    QObject* entry = entryOf(menu, "recordingsItem");
    ASSERT_NE(entry, nullptr);
    QMetaObject::invokeMethod(entry, "triggered");
    auto* dialog = find<QObject>("recordingsDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(until([&] { return dialog->property("visible").toBool(); }));
    EXPECT_EQ(dialog->property("items").toList().size(), 1);
    auto* remove = itemIn(dialog->property("contentItem").value<QQuickItem*>(), "recordingRemove0");
    ASSERT_NE(remove, nullptr);
    click(remove);
    EXPECT_TRUE(until([&] { return dialog->property("items").toList().isEmpty(); }));
    controller->undo();
    QVariantList list;
    QMetaObject::invokeMethod(audio(), "recordings", Q_RETURN_ARG(QVariantList, list));
    EXPECT_EQ(list.size(), 1) << "undo brings it back";
}

// A recording belongs to its tab: in another tab the button does not stop it, the pill says whose it is; closing its
// tab ends it
TEST_F(AudioUiTest, aRecordingBelongsToItsTab) {
    controller->newDocument();
    wait(100);
    QMetaObject::invokeMethod(audio(), "startRecording");
    ASSERT_TRUE(audio()->property("recording").toBool());
    controller->newDocument();
    wait(100);
    EXPECT_TRUE(audio()->property("recording").toBool());
    EXPECT_FALSE(audio()->property("recordingHere").toBool());
    EXPECT_TRUE(until([&] { return find<QQuickItem>("recordingElsewhere")->isVisible(); }));
    controller->selectTool("pen");
    drag({100, 200}, {300, 200});
    ASSERT_EQ(elements().size(), 1u);
    EXPECT_EQ(xqt::audio::audioOf(elements()[0]), nullptr) << "ink of another tab is not tied to it";
    // Its tab closes: the recording ends
    controller->closeTab(0);
    EXPECT_TRUE(until([&] { return !audio()->property("recording").toBool(); }));
}
