/*
 * xournal-qt: crash recovery and session restore (SessionRecovery, AppController::startSession/recover).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <future>
#include <memory>
#include <thread>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"
#include "session/PdfPageKeeper.h"
#include "shell/SessionRecovery.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"
#include "util/Util.h"

#include "AppController.h"
#include "config-test.h"

using namespace xqt;

namespace {
fs::path fixture(const char8_t* rel) { return GET_TESTFILE(rel); }

void scribble(DocumentSession& s) {
    auto page = s.getDocument()->getPage(0);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->addPoint(Point(10, 10, 1));
    stroke->addPoint(Point(50, 40, 1));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

size_t elementCount(DocumentSession& s) { return s.getDocument()->getPage(0)->getSelectedLayer()->getElements().size(); }

/// A pid that does not run.
qint64 deadPid() {
    for (qint64 pid = 4000000; pid > 1000; pid -= 7919) {
        if (!SessionRecovery::processAlive(pid) && ::kill(static_cast<pid_t>(pid), 0) != 0) {
            return pid;
        }
    }
    return 0;
}

class RecoveryTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        fs::remove(SessionRecovery::defaultJournalFile());
        // A document of our own to modify (never touch the fixtures).
        doc = fs::path(tmp.filePath("notes.xopp").toStdString());
        fs::copy_file(fixture(u8"load/strokes.xopp"), doc);
    }
    /// Pretend the run that wrote the journal crashed (its process is gone).
    static void markJournalCrashed() {
        auto j = SessionRecovery::readJournal(SessionRecovery::defaultJournalFile());
        ASSERT_TRUE(j);
        j->pid = deadPid();
        j->clean = false;
        ASSERT_TRUE(SessionRecovery::writeJournal(*j, SessionRecovery::defaultJournalFile()));
    }
    QTemporaryDir tmp;
    fs::path doc;
};
}  // namespace

TEST_F(RecoveryTest, journalRoundTrip) {
    SessionRecovery::Journal j{1234, false, 1, {{"/a/b.xopp", 1234, 3, 7}, {"", 1234, 4, 0}}};
    const fs::path file = tmp.filePath("j.json").toStdString();
    ASSERT_TRUE(SessionRecovery::writeJournal(j, file));
    auto r = SessionRecovery::readJournal(file);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->pid, 1234);
    EXPECT_FALSE(r->clean);
    EXPECT_EQ(r->current, 1);
    ASSERT_EQ(r->tabs.size(), 2u);
    EXPECT_EQ(r->tabs[0].file, fs::path("/a/b.xopp"));
    EXPECT_EQ(r->tabs[0].serial, 3u);
    EXPECT_EQ(r->tabs[0].page, 7);
    EXPECT_TRUE(r->tabs[1].file.empty());
    EXPECT_FALSE(SessionRecovery::readJournal(tmp.filePath("missing.json").toStdString()));
}

TEST_F(RecoveryTest, everyUnsavedTabHasItsOwnAutosaveFile) {
    AppController c;
    c.newDocument();
    c.newDocument();
    TabManager& tabs = c.tabManager();
    ASSERT_EQ(tabs.count(), 2);
    EXPECT_NE(tabs.session(0)->autosavePath(), tabs.session(1)->autosavePath());
    EXPECT_NE(tabs.session(0)->serial(), tabs.session(1)->serial());
}

TEST_F(RecoveryTest, candidatesAreNewerThanTheDocument) {
    const qint64 pid = deadPid();
    SessionRecovery::Journal j{pid, false, 0, {{doc, pid, 1, 0}, {"", pid, 2, 0}}};
    EXPECT_TRUE(SessionRecovery::findCandidates(j).empty());

    // An autosave older than the document is not offered; a newer emergency file is.
    const fs::path autosave = DocumentSession::namedAutosavePath(doc);
    fs::copy_file(doc, autosave);
    fs::last_write_time(autosave, fs::last_write_time(doc) - std::chrono::minutes(5));
    EXPECT_TRUE(SessionRecovery::findCandidates(j).empty());
    const fs::path emergency = DocumentSession::emergencyPath(pid, 2);
    fs::create_directories(emergency.parent_path());
    fs::copy_file(doc, emergency, fs::copy_options::overwrite_existing);
    auto c = SessionRecovery::findCandidates(j);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].tab, 1u);
    EXPECT_EQ(c[0].recoveryFile, emergency);
    EXPECT_TRUE(c[0].originalFile.empty());
    fs::remove(emergency);
}

TEST_F(RecoveryTest, documentsInAWindowOfTheirOwnStayInTheSession) {
    const fs::path second = tmp.filePath("second.xopp").toStdString();
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp"), second);
    {
        AppController a;
        a.startSession({});
        ASSERT_TRUE(a.openPath(QString::fromStdString(doc.string())));
        ASSERT_TRUE(a.openPath(QString::fromStdString(second.string())));
        a.undockTab(0);  // one document gets a window of its own
        ASSERT_EQ(a.tabManager().count(), 1);
        ASSERT_EQ(a.documentWindows().size(), 1u);
        a.shutdown();
    }
    // Both are opened again (as tabs of the main window)
    AppController b;
    b.startSession({});
    ASSERT_EQ(b.tabCount(), 2);
    std::vector<fs::path> files{b.tabManager().session(0)->getFilePath(), b.tabManager().session(1)->getFilePath()};
    EXPECT_NE(std::find(files.begin(), files.end(), doc), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), second), files.end());
}

TEST_F(RecoveryTest, lastTabsAreReopenedAfterANormalExit) {
    const fs::path second = tmp.filePath("second.xopp").toStdString();
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp"), second);
    {
        AppController a;
        a.startSession({});
        ASSERT_TRUE(a.openPath(QString::fromStdString(doc.string())));
        ASSERT_TRUE(a.openPath(QString::fromStdString(second.string())));
        a.goToPage(1);
        a.setCurrentTab(0);
        a.shutdown();
    }
    AppController b;
    b.startSession({});
    EXPECT_TRUE(b.recoveryItems().isEmpty());
    ASSERT_EQ(b.tabCount(), 2) << "the start document is replaced";
    EXPECT_EQ(b.tabManager().session(0)->getFilePath(), doc);
    EXPECT_EQ(b.tabManager().session(1)->getFilePath(), second);
    EXPECT_EQ(b.tabManager().session(1)->getCurrentPageNo(), 1u) << "page restored";
    EXPECT_EQ(b.currentTab(), 0) << "current tab restored";
}

TEST_F(RecoveryTest, unsavedChangesAreRecoveredAfterACrash) {
    {
        AppController a;
        a.startSession({});
        ASSERT_TRUE(a.openPath(QString::fromStdString(doc.string())));
        a.newDocument();
        scribble(*a.tabManager().session(0));
        scribble(*a.tabManager().session(1));
        EXPECT_EQ(SessionRecovery::emergencySaveAll(), 2);
        // "crash": no shutdown(), the journal stays unclean
    }
    markJournalCrashed();
    const size_t savedElements = [&] {
        auto r = DocumentSession::loadFile(doc);
        return r.document->getPage(0)->getSelectedLayer()->getElements().size();
    }();

    AppController b;
    b.startSession({});
    ASSERT_EQ(b.recoveryItems().size(), 2);
    EXPECT_EQ(b.recoveryItems()[0].toMap()["title"].toString(), "notes.xopp");
    b.recover(true);
    EXPECT_TRUE(b.recoveryItems().isEmpty());
    ASSERT_EQ(b.tabCount(), 2);
    DocumentSession* recovered = b.tabManager().session(0);
    EXPECT_EQ(recovered->getFilePath(), doc) << "saving writes to the original file";
    EXPECT_TRUE(recovered->isModified());
    EXPECT_EQ(elementCount(*recovered), savedElements + 1);
    EXPECT_FALSE(b.tabManager().session(1)->hasFilePath());
    EXPECT_EQ(elementCount(*b.tabManager().session(1)), 1u);
    EXPECT_TRUE(SessionRecovery::findCandidates(*SessionRecovery::readJournal(SessionRecovery::defaultJournalFile()))
                        .empty());
}

TEST_F(RecoveryTest, discardingReopensTheSavedFiles) {
    {
        AppController a;
        a.startSession({});
        ASSERT_TRUE(a.openPath(QString::fromStdString(doc.string())));
        scribble(*a.tabManager().session(0));
        EXPECT_EQ(SessionRecovery::emergencySaveAll(), 1);
    }
    markJournalCrashed();
    AppController b;
    b.startSession({});
    ASSERT_EQ(b.recoveryItems().size(), 1);
    const fs::path emergency = [&] {
        auto j = SessionRecovery::readJournal(SessionRecovery::defaultJournalFile());
        return SessionRecovery::findCandidates(*j).at(0).recoveryFile;
    }();
    b.recover(false);
    ASSERT_EQ(b.tabCount(), 1);
    EXPECT_FALSE(b.tabManager().session(0)->isModified());
    EXPECT_FALSE(fs::exists(emergency)) << "discarded changes are deleted";
}

// The fatal signal handler writes the modified documents (in a forked child that is then terminated).
TEST_F(RecoveryTest, fatalSignalSavesModifiedDocuments) {
    GTEST_FLAG_SET(death_test_style, "fast");
    AppController a;
    ASSERT_TRUE(a.openPath(QString::fromStdString(doc.string())));
    const quint64 serial = a.tabManager().session(0)->serial();
    const QDir dir(QString::fromStdString(Util::getAutosaveFilepath().parent_path().string()));
    const QString pattern = QString("*-%1.emergency.xopp").arg(serial);
    for (const QString& f: dir.entryList({pattern})) {
        QFile::remove(dir.filePath(f));
    }
    EXPECT_EXIT(
            {
                scribble(*a.tabManager().session(0));
                SessionRecovery::registerSession(a.tabManager().session(0));
                SessionRecovery::installCrashHandlers();
                std::raise(SIGTERM);
            },
            ::testing::KilledBySignal(SIGTERM), "saving unsaved documents");
    const QStringList saved = dir.entryList({pattern});
    ASSERT_EQ(saved.size(), 1);
    auto r = DocumentSession::loadFile(fs::path(dir.filePath(saved[0]).toStdString()));
    ASSERT_TRUE(r.document);
    QFile::remove(dir.filePath(saved[0]));
}

namespace {
/// A one-page PDF with a word on it, as text.
void makeWordPdf(const fs::path& p, const char* word) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    cairo_move_to(cr, 72, 100);
    cairo_show_text(cr, word);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// A run that pasted a PDF page into `doc` and crashed: the merged PDF of the pasted page is left in the cache.
fs::path crashAfterPasting(const fs::path& doc, const fs::path& other) {
    fs::path cached;
    fs::path kept = fs::path(other).replace_extension(".kept");
    {
        AppController a;
        a.startSession({});
        EXPECT_TRUE(a.openPath(QString::fromStdString(other.string())));
        a.copyPages({0});
        EXPECT_TRUE(a.openPath(QString::fromStdString(doc.string())));
        EXPECT_EQ(a.pastePages(0), 1);
        a.tabManager().currentSession()->waitForSaves();  // (the merged PDF is written in the background)
        cached = a.tabManager().currentSession()->getDocument()->getPdfFilepath();
        EXPECT_TRUE(MergedPdf::inCache(cached));
        EXPECT_EQ(SessionRecovery::emergencySaveAll(), 1);
        fs::copy_file(cached, kept);  // (a crash does not close the tabs, which would remove it)
    }
    fs::rename(kept, cached);
    return cached;
}
}  // namespace

// A page pasted from another PDF before a crash is recovered with its PDF text; the merged PDF in the cache goes when
// the recovered document is closed without saving, or when the recovery is declined.
TEST_F(RecoveryTest, pastedPdfPagesAreRecoveredAndTheirCachedPdfCleanedUp) {
    const fs::path other = fs::path(tmp.filePath("other.pdf").toStdString());
    makeWordPdf(other, "pastedalpha");
    fs::path cached = crashAfterPasting(doc, other);
    markJournalCrashed();
    {
        AppController b;
        b.startSession({});
        ASSERT_EQ(b.recoveryItems().size(), 1);
        b.recover(true);
        DocumentSession* recovered = nullptr;
        for (int i = 0; i < b.tabCount(); ++i) {
            if (b.tabManager().session(i)->getFilePath() == doc) {
                recovered = b.tabManager().session(i);
            }
        }
        ASSERT_NE(recovered, nullptr);
        EXPECT_FALSE(DocumentSearch::findOnPage(*recovered->getDocument(), 0, "pastedalpha").empty());
        EXPECT_TRUE(fs::exists(cached));
        for (int i = b.tabCount() - 1; i >= 0; --i) {
            b.closeTab(i);
        }
        EXPECT_FALSE(fs::exists(cached)) << "closed without saving";
    }
    fs::remove(SessionRecovery::defaultJournalFile());

    cached = crashAfterPasting(doc, other);
    markJournalCrashed();
    AppController c;
    c.startSession({});
    ASSERT_EQ(c.recoveryItems().size(), 1);
    c.recover(false);
    EXPECT_FALSE(fs::exists(cached)) << "the recovery was declined";
}

// A crash while a document is saved in the background: the emergency save still writes it (it is modified until the
// file is written), and the save itself finishes.
TEST_F(RecoveryTest, emergencySaveDuringABackgroundSave) {
    using namespace std::chrono_literals;
    AppController a;
    a.startSession({});
    ASSERT_TRUE(a.openPath(QString::fromStdString(doc.string())));
    DocumentSession& s = *a.tabManager().session(0);
    const size_t before = elementCount(s);
    scribble(s);
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<int> held{0};
    PdfPageKeeper::stopSaveAt = [&](int step) {
        if (step == 1) {
            ++held;
            released.wait_for(10s);
        }
        return false;
    };
    bool finished = false;
    s.saveInBackground({DocumentSession::SaveKind::Save, {}, {}, [&](const DocumentSession::SaveResult& r) {
                            EXPECT_TRUE(r.ok) << r.error;
                            finished = true;
                        }});
    auto waitFor = [](const std::function<bool()>& done) {
        const auto until = std::chrono::steady_clock::now() + 20s;
        while (!done() && std::chrono::steady_clock::now() < until) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            std::this_thread::sleep_for(1ms);
        }
        return done();
    };
    ASSERT_TRUE(waitFor([&] { return held == 1; })) << "the save is writing";
    scribble(s);  // (edited meanwhile)
    EXPECT_EQ(SessionRecovery::emergencySaveAll(), 1);
    const fs::path emergency = DocumentSession::emergencyPath(Util::getPid(), s.serial());
    auto saved = DocumentSession::loadFile(emergency);
    ASSERT_TRUE(saved.document) << saved.error;
    EXPECT_EQ(saved.document->getPage(0)->getSelectedLayer()->getElements().size(), before + 2)
            << "the document as it is now";
    release.set_value();
    const bool done = waitFor([&] { return finished; });
    PdfPageKeeper::stopSaveAt = nullptr;
    ASSERT_TRUE(done);
    auto file = DocumentSession::loadFile(doc);
    ASSERT_TRUE(file.document);
    EXPECT_EQ(file.document->getPage(0)->getSelectedLayer()->getElements().size(), before + 1)
            << "the save wrote the state it started with";
    EXPECT_TRUE(s.isModified());
    fs::remove(emergency);
}
