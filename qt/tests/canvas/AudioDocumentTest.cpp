/*
 * xournal-qt: recordings in the document (qt/docs/audio.md, "In the document"): pen strokes, shapes and new texts are
 * tied to the running recording as upstream does it (fn, ts); voice memos as the page attribute xqt-audio; removing a
 * recording as one undo step; the .xopp round trip; what the play tool finds.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPointingDevice>
#include <QProcess>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "audio/DocumentAudio.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "TextEditor.h"
#include "config-test.h"

using namespace xqt;

namespace {
class AudioDocumentTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        input = std::make_unique<CanvasInput>(*view);
        tools()->selectTool(TOOL_PEN);
        tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }
    ToolHandler* tools() const { return app->getToolHandler(); }
    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    QPointF viewPos(QPointF pagePoint) const {
        return view->pageViewRect(0).topLeft() + pagePoint * view->getViewController().zoom();
    }
    void tablet(QEvent::Type type, QPointF pos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->tabletEvent(&e, pos);
    }
    void drawLine(QPointF from, QPointF to, int steps = 20) {
        tablet(QEvent::TabletPress, viewPos(from), 0.6, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= steps; ++i) {
            tablet(QEvent::TabletMove, viewPos(from + (to - from) * (static_cast<double>(i) / steps)), 0.6,
                   Qt::NoButton, Qt::LeftButton);
        }
        tablet(QEvent::TabletRelease, viewPos(to), 0.0, Qt::LeftButton, Qt::NoButton);
        processEvents();
    }
    std::vector<const Element*> elements(size_t page = 0) const {
        std::vector<const Element*> out;
        for (const Element* e: session->getDocument()->getPage(page)->getSelectedLayer()->getElementsView()) {
            out.push_back(e);
        }
        return out;
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"pen", 1, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    quint64 timestamp = 1000;
    size_t clock = 0;  ///< the time in the fake recording
};

constexpr const char* NAME = "2026-10-04_10-00-00.ogg";
}  // namespace

// While a recording runs for the document, pen strokes and pen shapes get its name and the time; the highlighter's
// strokes do not (as upstream), nor strokes after it stopped
TEST_F(AudioDocumentTest, penStrokesWhileRecordingAreTiedToIt) {
    session->setRecording(NAME, [this] { return clock; });
    clock = 1234;
    drawLine({100, 100}, {300, 100});
    clock = 5678;
    tools()->setDrawingType(DRAWING_TYPE_LINE);
    drawLine({100, 200}, {300, 250});
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    tools()->selectTool(TOOL_HIGHLIGHTER);
    drawLine({100, 300}, {300, 300});
    tools()->selectTool(TOOL_PEN);
    session->setRecording({}, {});
    drawLine({100, 400}, {300, 400});

    const auto e = elements();
    ASSERT_EQ(e.size(), 4u);
    const auto* first = audio::audioOf(e[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(audio::nameOf(*first), NAME);
    EXPECT_EQ(first->getTimestamp(), 1234u);
    const auto* shape = audio::audioOf(e[1]);
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->getTimestamp(), 5678u);
    EXPECT_EQ(audio::audioOf(e[2]), nullptr) << "the highlighter";
    EXPECT_EQ(audio::audioOf(e[3]), nullptr) << "after the recording";

    // Undo and redo keep the stroke's recording (the same stroke comes back)
    session->getUndoRedoHandler()->undo();
    session->getUndoRedoHandler()->undo();
    session->getUndoRedoHandler()->undo();
    session->getUndoRedoHandler()->redo();
    session->getUndoRedoHandler()->redo();
    ASSERT_NE(audio::audioOf(elements()[1]), nullptr);
    EXPECT_EQ(audio::audioOf(elements()[1])->getTimestamp(), 5678u);
}

// A recording is a voice memo of the page it started on (one undo step); removing a recording takes it from the
// strokes and the memos in one undo step, which brings both back
TEST_F(AudioDocumentTest, voiceMemosAndRemovingARecording) {
    QSignalSpy changed(session.get(), &DocumentSession::audioChanged);
    ASSERT_TRUE(session->addVoiceMemo(0, NAME));
    EXPECT_FALSE(session->addVoiceMemo(0, NAME)) << "already one";
    ASSERT_TRUE(session->addVoiceMemo(0, "2026-10-04_11-00-00.ogg"));
    EXPECT_EQ(changed.count(), 2);
    auto page = session->getDocument()->getPage(0);
    EXPECT_EQ(page->getAudioMemos(), std::string(NAME) + "|2026-10-04_11-00-00.ogg");
    EXPECT_EQ(audio::memosOf(*page), (std::vector<std::string>{NAME, "2026-10-04_11-00-00.ogg"}));
    EXPECT_TRUE(session->isModified());
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(audio::memosOf(*page), (std::vector<std::string>{NAME}));
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(audio::memosOf(*page).size(), 2u);

    session->setRecording(NAME, [this] { return clock; });
    clock = 100;
    drawLine({100, 100}, {300, 100});
    session->setRecording({}, {});
    {
        const auto recs = audio::recordingsOf(*session->getDocument());
        ASSERT_EQ(recs.size(), 2u);
        EXPECT_EQ(recs[0].name, NAME);
        EXPECT_EQ(recs[0].elements, 1u);
        EXPECT_EQ(recs[0].pages, std::vector<size_t>{0});
        EXPECT_EQ(recs[0].memoPages, std::vector<size_t>{0});
        EXPECT_EQ(recs[1].elements, 0u);
    }

    changed.clear();
    EXPECT_EQ(session->removeRecording(NAME), 2u);  // (the stroke and the memo)
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(audio::audioOf(elements()[0]), nullptr);
    EXPECT_EQ(audio::memosOf(*page), (std::vector<std::string>{"2026-10-04_11-00-00.ogg"}));
    EXPECT_EQ(session->removeRecording(NAME), 0u);
    session->getUndoRedoHandler()->undo();
    ASSERT_NE(audio::audioOf(elements()[0]), nullptr);
    EXPECT_EQ(audio::audioOf(elements()[0])->getTimestamp(), 100u);
    EXPECT_EQ(audio::memosOf(*page).size(), 2u);
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(audio::audioOf(elements()[0]), nullptr);
    EXPECT_EQ(audio::memosOf(*page).size(), 1u);
}

// Saved as upstream writes it (fn, ts on the stroke) and the memos as the page attribute; read back the same; a copy
// of a page keeps them
TEST_F(AudioDocumentTest, aXoppKeepsThemAsUpstreamDoes) {
    ASSERT_TRUE(session->addVoiceMemo(0, NAME));
    session->setRecording(NAME, [this] { return clock; });
    clock = 2500;
    drawLine({100, 100}, {300, 100});
    session->setRecording({}, {});
    session->duplicatePage();
    ASSERT_TRUE(session->saveAs(path("notes.xopp")).ok);
    auto loaded = DocumentSession::loadFile(path("notes.xopp"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.warnings.empty());
    const auto recs = audio::recordingsOf(*loaded.document);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].name, NAME);
    EXPECT_EQ(recs[0].pages, (std::vector<size_t>{0, 1}));
    EXPECT_EQ(recs[0].memoPages, (std::vector<size_t>{0, 1}));
    EXPECT_EQ(recs[0].elements, 2u);
    EXPECT_EQ(recs[0].firstTs, 2500u);
    QProcess gz;
    gz.start("gzip", {"-dc", tmp.filePath("notes.xopp")});
    ASSERT_TRUE(gz.waitForFinished());
    const QString xml = QString::fromUtf8(gz.readAllStandardOutput());
    EXPECT_EQ(xml.count(QString("xqt-audio=\"%1\"").arg(NAME)), 2) << xml.toStdString();
    EXPECT_TRUE(xml.contains(QString("ts=\"2500\" fn=\"%1\"").arg(NAME))) << xml.toStdString();
}

// Upstream's own files: the recording of their strokes is found (bare name), and the moments are in time order
TEST_F(AudioDocumentTest, upstreamFilesWithRecordings) {
    auto loaded = DocumentSession::loadFile(fs::path(GET_TESTFILE(u8"packaged_xopp/audioAttachment/old.xopp")));
    ASSERT_TRUE(loaded.document) << loaded.error;
    const auto recs = audio::recordingsOf(*loaded.document);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].name, "test.ogg");
    EXPECT_GT(recs[0].elements, 0u);
    const auto moments = audio::momentsOf(*loaded.document, "test.ogg");
    ASSERT_EQ(moments.size(), recs[0].elements);
    for (size_t i = 1; i < moments.size(); ++i) {
        EXPECT_LE(moments[i - 1].ts, moments[i].ts);
    }
}

// The play tool's hit: the nearest element with a recording within 15 points, on a visible layer
TEST_F(AudioDocumentTest, whatThePlayToolFinds) {
    session->setRecording(NAME, [this] { return clock; });
    clock = 1000;
    drawLine({100, 100}, {300, 100});
    clock = 2000;
    drawLine({100, 120}, {300, 120});
    session->setRecording({}, {});
    drawLine({100, 300}, {300, 300});
    auto page = session->getDocument()->getPage(0);
    auto hit = audio::hitAt(*page, 200, 103);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->name, NAME);
    EXPECT_EQ(hit->ts, 1000u);
    hit = audio::hitAt(*page, 200, 118);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->ts, 2000u);
    EXPECT_FALSE(audio::hitAt(*page, 200, 160)) << "too far";
    EXPECT_FALSE(audio::hitAt(*page, 200, 300)) << "a stroke without a recording";
    page->getSelectedLayer()->setVisible(false);
    EXPECT_FALSE(audio::hitAt(*page, 200, 103)) << "a hidden layer";
}

TEST(AudioNames, newRecordingsAreNamedAsUpstreamNamesThem) {
    const QDateTime t(QDate(2026, 10, 4), QTime(14, 3, 22));
    EXPECT_EQ(audio::newRecordingName(t), "2026-10-04_14-03-22.ogg");
    std::set<std::string> taken{"2026-10-04_14-03-22.ogg", "2026-10-04_14-03-22-2.ogg"};
    EXPECT_EQ(audio::newRecordingName(t, [&](const std::string& n) { return taken.count(n) > 0; }),
              "2026-10-04_14-03-22-3.ogg");
    EXPECT_EQ(audio::parseMemos("a.ogg||b.ogg|"), (std::vector<std::string>{"a.ogg", "b.ogg"}));
    EXPECT_EQ(audio::formatMemos({"a.ogg", "", "b.ogg"}), "a.ogg|b.ogg");
    EXPECT_TRUE(audio::parseMemos("").empty());
}
