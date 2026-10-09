/*
 * xournal-qt: recording and playing in the real window (qt/docs/features/audio.md, "In the app"), with the fake
 * microphone and speaker (their timers run as real devices would): the record button in the tool bar, the recording
 * pill, ink tied to the recording, the play tool and the playback pill, the list of recordings; the microphone
 * permission (refused, asked) and what the platform's hook hears (Android's notification) with its commands.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <memory>
#include <shared_mutex>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"
#include "AudioControl.h"

namespace fs = std::filesystem;

namespace {
class AudioUiTest: public xqt::test::UiFixture {
protected:
    /// The fake microphone and speaker; false: as a build without any audio backend
    virtual bool withAudio() const { return true; }
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        if (withAudio()) {
            xqt::audio::useFakeDevices(true);
        } else {
            xqt::audio::useNoDevices(true);
        }
        xqt::audio::fake::reset();
        xqt::audio::setAppFolder(fs::path(tmp.filePath("audio").toStdString()));
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1920, 1080)}));
        wait(100);
    }
    void TearDown() override {
        xqt::AudioControl::setPermissionAccess({});
        xqt::AudioControl::setSettingsOpener({});
        xqt::AudioControl::setPlatformHook({});
        closeApp();
        xqt::audio::setAppFolder({});
        xqt::audio::fake::reset();
        xqt::audio::useFakeDevices(false);
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
};
/// The system's trash, faked: removes (never the user's trash)
struct FakeTrash: xqt::SystemApps {
    QStringList trashed;
    bool moveToTrash(const QString& path) override {
        trashed << path;
        return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    }
};
class NoAudioUiTest: public AudioUiTest {
protected:
    bool withAudio() const override { return false; }
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

// qt/docs/features/audio.md, "Storage": a document not saved yet records into the app's folder; saved as a .xopp
// while it records, the recording stays there until it ends and then goes into "name.audio" next to it; the next one
// is recorded there directly, and both play
TEST_F(AudioUiTest, aSavedDocumentKeepsItsRecordingsNextToIt) {
    controller->newDocument();
    view()->getViewController().scrollToPageRect(0, QRectF(0, 0, 400, 400));
    wait(200);
    ASSERT_TRUE(QMetaObject::invokeMethod(audio(), "startRecording"));
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 300; }));
    QVariantList recs;
    QMetaObject::invokeMethod(audio(), "recordings", Q_RETURN_ARG(QVariantList, recs));
    ASSERT_EQ(recs.size(), 1);
    const QString name1 = recs[0].toMap()["name"].toString();
    const fs::path app = fs::path(tmp.filePath("audio").toStdString());
    EXPECT_TRUE(fs::exists(app / name1.toStdString())) << "not saved yet: the app's folder";

    const fs::path xopp = fs::path(tmp.filePath("docs/lecture.xopp").toStdString());
    fs::create_directories(xopp.parent_path());
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString(xopp.string()))));
    EXPECT_TRUE(fs::exists(app / name1.toStdString())) << "still being recorded: where it is";
    QMetaObject::invokeMethod(audio(), "stopRecording");
    const fs::path sidecar = xopp.parent_path() / "lecture.audio";
    EXPECT_TRUE(fs::exists(sidecar / name1.toStdString())) << "ended: next to the document";
    EXPECT_FALSE(fs::exists(app / name1.toStdString()));

    wait(1100);  // (the next recording's name: another second)
    ASSERT_TRUE(QMetaObject::invokeMethod(audio(), "startRecording"));
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 300; }));
    QMetaObject::invokeMethod(audio(), "stopRecording");
    QMetaObject::invokeMethod(audio(), "recordings", Q_RETURN_ARG(QVariantList, recs));
    ASSERT_EQ(recs.size(), 2);
    for (const QVariant& r: recs) {
        const QString n = r.toMap()["name"].toString();
        EXPECT_TRUE(fs::exists(sidecar / n.toStdString())) << n.toStdString();
        EXPECT_TRUE(r.toMap()["found"].toBool());
        bool played = false;
        QMetaObject::invokeMethod(audio(), "play", Q_RETURN_ARG(bool, played), Q_ARG(QString, n), Q_ARG(qint64, 0));
        EXPECT_TRUE(played) << n.toStdString();
        QMetaObject::invokeMethod(audio(), "stopPlayback");
    }
}

// A .xopp with recordings saved as a PDF with notes, the .xopp to the trash with its "name.audio": the PDF carries the
// recordings, and they still play in the open document (a copy went into the app's folder, where a PDF with notes
// finds them)
TEST_F(AudioUiTest, aXoppSavedAsAPdfWithNotesKeepsPlayingItsRecordings) {
    FakeTrash trash;
    xqt::SystemApps::setInstance(&trash);
    controller->newDocument();
    ASSERT_TRUE(QMetaObject::invokeMethod(audio(), "startRecording"));
    ASSERT_TRUE(until([&] { return audio()->property("recordedMs").toLongLong() >= 300; }));
    QMetaObject::invokeMethod(audio(), "stopRecording");
    const fs::path xopp = fs::path(tmp.filePath("docs/talk.xopp").toStdString());
    fs::create_directories(xopp.parent_path());
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString(xopp.string()))));
    QVariantList recs;
    QMetaObject::invokeMethod(audio(), "recordings", Q_RETURN_ARG(QVariantList, recs));
    ASSERT_EQ(recs.size(), 1);
    const QString name = recs[0].toMap()["name"].toString();
    ASSERT_TRUE(fs::exists(xopp.parent_path() / "talk.audio" / name.toStdString()));

    const fs::path pdf = xopp.parent_path() / "talk.pdf";
    ASSERT_TRUE(controller->saveAsHybridInBackground(QUrl::fromLocalFile(QString::fromStdString(pdf.string())),
                                                     QJSValue(), "trash"));
    ASSERT_TRUE(until([&] { return !controller->anySaving() && !fs::exists(xopp); }, 20000));
    EXPECT_TRUE(trash.trashed.contains(QString::fromStdString((xopp.parent_path() / "talk.audio").string())))
            << "its recordings' folder went with it";
    EXPECT_FALSE(fs::exists(xopp.parent_path() / "talk.audio"));
    QMetaObject::invokeMethod(audio(), "recordings", Q_RETURN_ARG(QVariantList, recs));
    ASSERT_EQ(recs.size(), 1);
    EXPECT_TRUE(recs[0].toMap()["found"].toBool());
    bool played = false;
    QMetaObject::invokeMethod(audio(), "play", Q_RETURN_ARG(bool, played), Q_ARG(QString, name), Q_ARG(qint64, 0));
    EXPECT_TRUE(played);
    QMetaObject::invokeMethod(audio(), "stopPlayback");
    xqt::SystemApps::setInstance(nullptr);
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

// The system refuses the microphone (macOS, Android: asked earlier, or turned off in its settings): nothing records,
// a dialog says so plainly and where it is allowed, and opens that page of the settings
TEST_F(AudioUiTest, aRefusedMicrophoneSaysWhereToAllowIt) {
    using P = xqt::AudioControl::Permission;
    int asked = 0;
    xqt::AudioControl::setPermissionAccess({[] { return P::Denied; },
                                            [&asked](QObject*, std::function<void(bool)>) { ++asked; }});
    int opened = 0;
    xqt::AudioControl::setSettingsOpener([&opened] {
        ++opened;
        return true;
    });
    controller->newDocument();
    wait(200);
    auto* record = find<QQuickItem>("recordButton");
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(until([&] { return record->isVisible(); }));
    click(record);
    auto* dialog = find<QObject>("microphoneDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(until([&] { return dialog->property("visible").toBool(); }));
    EXPECT_FALSE(audio()->property("recording").toBool());
    EXPECT_FALSE(record->property("checked").toBool());
    EXPECT_EQ(asked, 0) << "refused once, the system does not ask again: the settings do";
    EXPECT_FALSE(xqt::audio::fake::inputRunning()) << "the microphone was not opened";
    auto* text = itemIn(dialog->property("contentItem").value<QQuickItem*>(), "microphoneDialogText");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->property("text").toString().contains(audio()->property("microphoneSettingsPath").toString()));
    auto* settings = itemIn(dialog->property("footer").value<QQuickItem*>(), "microphoneSettingsButton");
    ASSERT_NE(settings, nullptr);
    EXPECT_TRUE(settings->isVisible()) << "a way to the settings";
    click(settings);
    EXPECT_EQ(opened, 1);
    EXPECT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_FALSE(audio()->property("microphoneDenied").toBool());

    // Allowed in the settings meanwhile: the next tap records, without the dialog
    xqt::AudioControl::setPermissionAccess({[] { return P::Granted; }, {}});
    click(record);
    EXPECT_TRUE(until([&] { return audio()->property("recording").toBool(); }));
    EXPECT_FALSE(dialog->property("visible").toBool());
    QMetaObject::invokeMethod(audio(), "stopRecording");
}

// Not asked yet: the system's question comes first, the recording starts with a yes; a no opens the dialog (Linux,
// where there is no settings page to open: without its button)
TEST_F(AudioUiTest, theMicrophoneIsAskedForBeforeTheFirstRecording) {
    using P = xqt::AudioControl::Permission;
    P status = P::Undetermined;
    bool answer = true;
    std::function<void(bool)> pending;
    xqt::AudioControl::setPermissionAccess(
            {[&status] { return status; },
             [&pending](QObject*, std::function<void(bool)> a) { pending = std::move(a); }});
    controller->newDocument();
    wait(200);
    QMetaObject::invokeMethod(audio(), "startRecording");
    ASSERT_TRUE(pending) << "the system asks";
    EXPECT_FALSE(audio()->property("recording").toBool()) << "not before the answer";
    QMetaObject::invokeMethod(audio(), "startRecording");  // (a second tap while the question is open)
    status = P::Granted;
    std::exchange(pending, {})(answer);
    EXPECT_TRUE(audio()->property("recording").toBool()) << "yes: it records";
    QMetaObject::invokeMethod(audio(), "stopRecording");
    EXPECT_FALSE(pending) << "asked once";

    status = P::Undetermined;
    answer = false;
    QMetaObject::invokeMethod(audio(), "startRecording");
    ASSERT_TRUE(pending);
    status = P::Denied;
    std::exchange(pending, {})(answer);
    EXPECT_FALSE(audio()->property("recording").toBool());
    auto* dialog = find<QObject>("microphoneDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(until([&] { return dialog->property("visible").toBool(); }));
    auto* settings = itemIn(dialog->property("footer").value<QQuickItem*>(), "microphoneSettingsButton");
    ASSERT_NE(settings, nullptr);
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
    EXPECT_FALSE(settings->isVisible()) << "no settings page to open here";
#endif
    auto* close = itemIn(dialog->property("footer").value<QQuickItem*>(), "microphoneCloseButton");
    ASSERT_NE(close, nullptr);
    click(close);
    EXPECT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_FALSE(audio()->property("microphoneDenied").toBool());
}

// The platform's hook (Android: the foreground service and its notification) hears of the start, pause, resume and
// end, with the time and the document's title; the notification's Pause, Resume and Stop reach the recording
TEST_F(AudioUiTest, thePlatformHearsOfTheRecordingAndControlsIt) {
    using State = xqt::AudioControl::PlatformState;
    using Command = xqt::AudioControl::PlatformCommand;
    std::vector<State> heard;
    xqt::AudioControl::setPlatformHook([&heard](const State& s) { heard.push_back(s); });
    controller->newDocument();
    wait(200);
    auto* control = qobject_cast<xqt::AudioControl*>(audio());
    ASSERT_NE(control, nullptr);
    ASSERT_TRUE(control->startRecording());
    ASSERT_TRUE(until([&] { return heard.size() == 1; }));
    EXPECT_TRUE(heard[0].recording);
    EXPECT_FALSE(heard[0].paused);
    EXPECT_FALSE(heard[0].title.isEmpty()) << "the document's title";
    ASSERT_TRUE(until([&] { return control->recordedMs() >= 300; }));
    wait(100);
    EXPECT_EQ(heard.size(), 1u) << "the time alone is no news (the notification's clock runs by itself)";

    control->platformCommand(Command::Pause);
    EXPECT_TRUE(control->recordingPaused());
    ASSERT_TRUE(until([&] { return heard.size() == 2; }));
    EXPECT_TRUE(heard[1].recording);
    EXPECT_TRUE(heard[1].paused);
    EXPECT_GE(heard[1].recordedMs, 300) << "where it paused";
    const qint64 pausedAt = control->recordedMs();
    wait(200);
    EXPECT_EQ(control->recordedMs(), pausedAt);

    control->platformCommand(Command::Resume);
    EXPECT_FALSE(control->recordingPaused());
    ASSERT_TRUE(until([&] { return heard.size() == 3; }));
    EXPECT_FALSE(heard[2].paused);
    EXPECT_EQ(heard[2].recordedMs, pausedAt) << "the clock goes on from the recorded time";

    control->platformCommand(Command::Stop);
    EXPECT_FALSE(control->recording());
    ASSERT_TRUE(until([&] { return heard.size() == 4; }));
    EXPECT_FALSE(heard[3].recording);
    wait(100);
    EXPECT_EQ(heard.size(), 4u);

    // A recording that ends with its tab, or with the window, is reported ended too
    ASSERT_TRUE(control->startRecording());
    ASSERT_TRUE(until([&] { return heard.size() == 5 && heard[4].recording; }));
    controller->closeTab(0);
    ASSERT_TRUE(until([&] { return heard.size() == 6; }));
    EXPECT_FALSE(heard[5].recording);
}

// A build without any audio backend offers no recording: no record button on the toolbox's rail or the top bar, none
// in ⋮, Ctrl+Shift+R does nothing
TEST_F(NoAudioUiTest, nothingOffersRecording) {
    controller->newDocument();
    wait(200);
    ASSERT_FALSE(audio()->property("available").toBool());
    auto* record = find<QQuickItem>("recordButton");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->property("offered").toBool());
    EXPECT_FALSE(record->isVisible());
    EXPECT_FALSE(find<QObject>("moreCmd_record")->property("offered").toBool()) << "not in ⋮ → Tools";
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier);
    wait(100);
    EXPECT_FALSE(audio()->property("recording").toBool());
    EXPECT_FALSE(find<QQuickItem>("recordingPill")->isVisible());
    // A phone: not on its top bar either
    window->resize(412, 915);
    wait(400);
    EXPECT_FALSE(record->isVisible());
    EXPECT_NE(itemIn(window->contentItem(), "topApp_image"), nullptr);
    EXPECT_EQ(itemIn(window->contentItem(), "topApp_record"), nullptr);
}
