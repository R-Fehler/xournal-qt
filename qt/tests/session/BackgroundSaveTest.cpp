/*
 * xournal-qt: saving in the background (DocumentSession::saveInBackground). The window's event loop keeps running
 * while a big document is written; edits made meanwhile are neither lost nor saved half; a failed save keeps the
 * document modified; saves asked for while one runs follow it; a closed document finishes its save first.
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/PdfPageKeeper.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

using namespace xqt;
using namespace std::chrono_literals;

namespace {
/// Pages with generated backgrounds and many pressure strokes: seconds of writing for the tests' machine.
std::unique_ptr<Document> bigDocument(size_t pages, int strokesPerPage, int pointsPerStroke) {
    auto doc = std::make_unique<Document>(nullptr);
    for (size_t i = 0; i < pages; ++i) {
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Ruled));
        Layer* layer = page->getSelectedLayer();
        for (int k = 0; k < strokesPerPage; ++k) {
            auto s = std::make_unique<Stroke>();
            s->setWidth(1.41);
            for (int j = 0; j < pointsPerStroke; ++j) {
                s->addPoint(Point(40 + j * 500.0 / pointsPerStroke, 60 + k * 12 + 4 * std::sin(j / 3.0),
                                  1 + (j % 10) / 5.0));
            }
            s->getBoundingBox();  // (as loadFile does, before any other thread sees it)
            layer->addElement(std::move(s));
        }
        doc->addPage(std::move(page));
    }
    return doc;
}

size_t strokesIn(const fs::path& file) {
    auto loaded = DocumentSession::loadFile(file);
    if (!loaded.document) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < loaded.document->getPageCount(); ++i) {
        for (const Layer* l: loaded.document->getPage(i)->getLayers()) {
            n += l->getElementsView().size();
        }
    }
    return n;
}

/// Add a stroke through the undo machinery, like the stroke tool does.
void addStroke(DocumentSession& s, size_t pageNo) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->addPoint(Point(50, 50, 1.0));
    stroke->addPoint(Point(90, 70, 0.8));
    stroke->getBoundingBox();
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

/// Holds a save on its worker just before it writes the .xopp (PdfPageKeeper::stopSaveAt, step 1) until released.
struct HeldSave {
    HeldSave() {
        PdfPageKeeper::stopSaveAt = [this](int step) {
            if (step == 1) {
                ++entered;
                released.wait_for(5s);  // (never forever: a broken test fails instead of hanging)
            }
            return false;
        };
    }
    ~HeldSave() {
        release();
        PdfPageKeeper::stopSaveAt = nullptr;
    }
    void release() {
        if (!done.exchange(true)) {
            promise.set_value();
        }
    }
    std::atomic<int> entered{0};
    std::promise<void> promise;
    std::shared_future<void> released = promise.get_future().share();
    std::atomic<bool> done{false};
};

/// Runs the event loop until `done` (false: it took too long).
bool waitFor(const std::function<bool()>& done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

/// Runs the event loop until `finished`, and measures the longest time it did not run (the window was blocked),
/// including the call that starts the work.
struct LoopWatch {
    double longestMs = 0;
    double totalMs = 0;
    void run(const std::function<void()>& start, const std::function<bool()>& finished) {
        QElapsedTimer total, gap;
        total.start();
        gap.start();
        start();
        auto tick = [&] {
            longestMs = std::max(longestMs, static_cast<double>(gap.nsecsElapsed()) / 1e6);
            gap.restart();
        };
        tick();
        while (!finished() && total.elapsed() < 120000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            tick();
        }
        totalMs = static_cast<double>(total.nsecsElapsed()) / 1e6;
    }
};

class BackgroundSaveTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    void TearDown() override { PdfPageKeeper::stopSaveAt = nullptr; }
    fs::path tmpPath(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    std::unique_ptr<DocumentSession> bigSession(size_t pages, int strokes, int points) {
        auto s = std::make_unique<DocumentSession>(*app, bigDocument(pages, strokes, points));
        addStroke(*s, 0);  // (modified)
        return s;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

// The event loop keeps running while a big document is written as .xopp: the window is blocked only while its pages
// are copied, a small part of the save.
TEST_F(BackgroundSaveTest, theEventLoopKeepsRunningDuringAnXoppSave) {
    auto s = bigSession(200, 60, 100);
    bool finished = false;
    DocumentSession::SaveResult result;
    LoopWatch watch;
    watch.run(
            [&] {
                s->saveInBackground({DocumentSession::SaveKind::SaveAs, tmpPath("big.xopp"), {},
                                     [&](const DocumentSession::SaveResult& r) {
                                         result = r;
                                         finished = true;
                                     }});
            },
            [&] { return finished; });
    ASSERT_TRUE(finished);
    ASSERT_TRUE(result.ok) << result.error;
    std::cout << "xopp save: " << watch.totalMs << " ms, the event loop blocked at most " << watch.longestMs
              << " ms\n";
    EXPECT_LT(watch.longestMs, 0.35 * watch.totalMs) << "the save blocks the window";
    EXPECT_FALSE(s->isModified());
    EXPECT_EQ(strokesIn(tmpPath("big.xopp")), 200u * 60 + 1);
}

// The same for a hybrid PDF, whose drawing and qpdf write take longest.
TEST_F(BackgroundSaveTest, theEventLoopKeepsRunningDuringAHybridSave) {
    auto s = bigSession(50, 40, 60);
    bool finished = false;
    DocumentSession::SaveResult result;
    LoopWatch watch;
    watch.run(
            [&] {
                s->saveInBackground({DocumentSession::SaveKind::Hybrid, tmpPath("big.pdf"), {},
                                     [&](const DocumentSession::SaveResult& r) {
                                         result = r;
                                         finished = true;
                                     }});
            },
            [&] { return finished; });
    ASSERT_TRUE(finished);
    ASSERT_TRUE(result.ok) << result.error;
    std::cout << "hybrid save: " << watch.totalMs << " ms, the event loop blocked at most " << watch.longestMs
              << " ms\n";
    EXPECT_LT(watch.longestMs, 0.35 * watch.totalMs) << "the save blocks the window";
    EXPECT_FALSE(s->isModified());
    EXPECT_EQ(strokesIn(tmpPath("big.pdf")), 50u * 40 + 1);
}

// An edit made while the file is written is not in the file, and the document stays modified; undoing it gets back
// to what the file holds.
TEST_F(BackgroundSaveTest, anEditDuringASaveIsNotLostAndTheDocumentStaysModified) {
    DocumentSession s(*app);
    addStroke(s, 0);
    QSignalSpy saving(&s, &DocumentSession::savingChanged);
    HeldSave held;
    bool finished = false;
    DocumentSession::SaveResult result;
    s.saveInBackground({DocumentSession::SaveKind::SaveAs, tmpPath("edit.xopp"), {},
                        [&](const DocumentSession::SaveResult& r) {
                            result = r;
                            finished = true;
                        }});
    ASSERT_TRUE(waitFor([&] { return held.entered == 1; }));
    EXPECT_TRUE(s.isSaving());
    EXPECT_TRUE(s.isModified()) << "modified until the file is written";
    addStroke(s, 0);  // while the worker writes
    held.release();
    ASSERT_TRUE(waitFor([&] { return finished; }));
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(s.isSaving());
    EXPECT_EQ(saving.count(), 2) << "saving, then not";
    EXPECT_EQ(strokesIn(tmpPath("edit.xopp")), 1u) << "the file holds the document as it was when the save started";
    EXPECT_TRUE(s.isModified()) << "the edit made meanwhile is not saved";

    s.getUndoRedoHandler()->undo();
    EXPECT_FALSE(s.isModified()) << "back to what the file holds";
    s.getUndoRedoHandler()->redo();
    EXPECT_TRUE(s.isModified());
    ASSERT_TRUE(s.save().ok);
    EXPECT_EQ(strokesIn(tmpPath("edit.xopp")), 2u);
    EXPECT_FALSE(s.isModified());
}

// A save that cannot write its file reports why, and the document stays modified, also after undo and redo.
TEST_F(BackgroundSaveTest, aFailedSaveKeepsTheDocumentModifiedAndReportsTheError) {
    DocumentSession s(*app);
    addStroke(s, 0);
    ASSERT_TRUE(s.saveAs(tmpPath("fine.xopp")).ok);
    addStroke(s, 0);
    QSignalSpy modified(&s, &DocumentSession::modifiedChanged);
    bool finished = false;
    DocumentSession::SaveResult result;
    s.saveInBackground({DocumentSession::SaveKind::SaveAs, tmpPath("missing folder/x.xopp"), {},
                        [&](const DocumentSession::SaveResult& r) {
                            result = r;
                            finished = true;
                        }});
    ASSERT_TRUE(waitFor([&] { return finished; }));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(s.isModified());
    EXPECT_EQ(modified.count(), 0) << "never shown as saved";
    s.getUndoRedoHandler()->undo();
    s.getUndoRedoHandler()->redo();
    EXPECT_TRUE(s.isModified()) << "nothing was written for this state";

    // A folder that cannot be written
    fs::create_directories(tmpPath("locked"));
    fs::permissions(tmpPath("locked"), fs::perms::owner_read | fs::perms::owner_exec);
    const auto r = s.saveAs(tmpPath("locked") / "x.xopp");
    fs::permissions(tmpPath("locked"), fs::perms::owner_all);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(s.isModified());
    // Written after all: saved
    ASSERT_TRUE(s.saveAs(tmpPath("fine.xopp")).ok);
    EXPECT_FALSE(s.isModified());
    EXPECT_EQ(strokesIn(tmpPath("fine.xopp")), 2u);
}

// Ctrl+S while a save runs: one more save follows it (several in a row are one), with the edits made meanwhile.
TEST_F(BackgroundSaveTest, aSaveAskedForWhileOneRunsFollowsIt) {
    DocumentSession s(*app);
    addStroke(s, 0);
    ASSERT_TRUE(s.saveAs(tmpPath("twice.xopp")).ok);
    addStroke(s, 0);
    HeldSave held;
    int finished = 0;
    auto done = [&](const DocumentSession::SaveResult& r) {
        EXPECT_TRUE(r.ok) << r.error;
        ++finished;
    };
    s.saveInBackground({DocumentSession::SaveKind::Save, {}, {}, done});
    ASSERT_TRUE(waitFor([&] { return held.entered == 1; }));
    addStroke(s, 0);
    s.saveInBackground({DocumentSession::SaveKind::Save, {}, {}, done});
    s.saveInBackground({DocumentSession::SaveKind::Save, {}, {}, done});
    held.release();
    ASSERT_TRUE(waitFor([&] { return finished == 3; }));
    EXPECT_EQ(held.entered, 2) << "one more save, not two";
    EXPECT_FALSE(s.isSaving());
    EXPECT_FALSE(s.isModified());
    EXPECT_EQ(strokesIn(tmpPath("twice.xopp")), 3u);
}

// A document closed while it is saved finishes the save first: the file is complete.
TEST_F(BackgroundSaveTest, closingWaitsForARunningSave) {
    auto s = bigSession(40, 60, 100);
    HeldSave held;
    bool called = false;
    s->saveInBackground({DocumentSession::SaveKind::SaveAs, tmpPath("closed.xopp"), {},
                         [&](const DocumentSession::SaveResult&) { called = true; }});
    ASSERT_TRUE(waitFor([&] { return held.entered == 1; }));
    std::thread releaser([&] {
        std::this_thread::sleep_for(300ms);
        held.release();
    });
    s.reset();  // (waits for the save)
    releaser.join();
    EXPECT_EQ(strokesIn(tmpPath("closed.xopp")), 40u * 60 + 1);
}
