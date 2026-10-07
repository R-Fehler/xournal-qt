/*
 * xournal-qt: Quick note (qt/docs/quick-note.md): a new note in the library's "Inbox" named by the date and time, in
 * the format of new documents, opened in a tab with the pen in hand; and `xournal-qt --quick-note` handed to the
 * window that runs already (SingleInstance).
 *
 * @license GNU GPLv2 or later
 */
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "shell/SettingsModel.h"
#include "shell/SingleInstance.h"
#include "shell/TabManager.h"

#include "AppController.h"
#include "support/TestSupport.h"

using xqt::test::processEventsFor;

using namespace xqt;

namespace {

class QuickNoteTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    /// The way documents are kept, back to "not chosen" at the end (the tests share the config folder)
    struct Mode {
        AppController& c;
        explicit Mode(AppController& c, DocumentMode::Mode m): c(c) { set(m); }
        void set(DocumentMode::Mode m) {
            DocumentMode::store(*c.context().getSettings(), m);
            Q_EMIT c.context().settingsChanged();
        }
        ~Mode() { set(DocumentMode::Mode::Unset); }
    };
    fs::path currentFile(AppController& c) const { return c.tabManager().currentSession()->getFilePath(); }

    QTemporaryDir tmp;
    fs::path root;
    const QDateTime evening{QDate(2026, 10, 4), QTime(21, 30, 12)};
};
}  // namespace

// A new note in Inbox/ (made on first use), named by the date and time, saved at once and open in a tab with the pen
// in hand; a second one in the same minute gets the next free name.
TEST_F(QuickNoteTest, aNewNoteInTheInboxNamedByTheTime) {
    AppController c;
    Mode mode(c, DocumentMode::Mode::Xopp);
    c.setLibraryRoot(root);
    c.selectTool("eraser");
    c.setHomeVisible(true);
    ASSERT_FALSE(fs::exists(root / "Inbox"));

    ASSERT_TRUE(c.quickNoteAt(evening));
    const fs::path note = root / "Inbox" / "2026-10-04 21-30.xopp";
    EXPECT_TRUE(fs::is_regular_file(note));
    ASSERT_EQ(c.tabCount(), 1);
    EXPECT_EQ(currentFile(c), note);
    EXPECT_FALSE(c.homeVisible()) << "the note is shown at once";
    EXPECT_EQ(c.tool(), "pen") << "ready for the pen";

    ASSERT_TRUE(c.quickNoteAt(evening));
    EXPECT_EQ(c.tabCount(), 2);
    EXPECT_EQ(currentFile(c), root / "Inbox" / "2026-10-04 21-30 (2).xopp");
    EXPECT_TRUE(fs::exists(note)) << "the first note stays";
}

// In the PDF files mode a quick note is a PDF with notes, as every new document.
TEST_F(QuickNoteTest, aPdfWithNotesInPdfFilesMode) {
    AppController c;
    Mode mode(c, DocumentMode::Mode::Pdf);
    c.setLibraryRoot(root);
    ASSERT_TRUE(c.quickNoteAt(evening));
    const fs::path note = root / "Inbox" / "2026-10-04 21-30.pdf";
    EXPECT_EQ(currentFile(c), note);
    EXPECT_TRUE(HybridPdf::isHybrid(note));
    EXPECT_FALSE(fs::exists(root / "Inbox" / "2026-10-04 21-30.xopp"));
}

// Without a library there is nowhere to keep it: a new document, not saved yet.
TEST_F(QuickNoteTest, withoutALibraryANewDocument) {
    AppController c;
    ASSERT_TRUE(c.quickNoteAt(evening));
    ASSERT_EQ(c.tabCount(), 1);
    EXPECT_FALSE(c.tabManager().currentSession()->hasFilePath());
}

// Today's Markdown note (the setting "daily"), when it is not open: the line goes into the file, after a line break
// when the file does not end with one.
TEST_F(QuickNoteTest, dailyNoteGetsALineInTheFile) {
    AppController c;
    c.setLibraryRoot(root);
    auto* settings = qobject_cast<SettingsModel*>(c.settingsModel());
    ASSERT_NE(settings, nullptr);
    struct Back {
        SettingsModel* s;
        ~Back() { s->set("quickNote", "note"); }
    } back{settings};
    settings->set("quickNote", "daily");
    ASSERT_EQ(settings->get("quickNote").toString(), "daily");

    fs::create_directories(root / "Inbox");
    {
        QFile f(QString::fromStdString((root / "Inbox" / "2026-10-04.md").string()));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("# Sunday\n\nMorning: groceries");
    }
    c.quickNoteAt(evening);  // (the editor needs a window: the UI test looks at the cursor)
    QFile f(QString::fromStdString((root / "Inbox" / "2026-10-04.md").string()));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll().toStdString(), "# Sunday\n\nMorning: groceries\n- 21:30 ");
    ASSERT_GE(c.tabCount(), 1);
    EXPECT_EQ(c.tabManager().indexOfFile(root / "Inbox" / "2026-10-04.md"), c.currentTab()) << "open in a tab";
}

// `xournal-qt --quick-note` while the app runs: the second instance hands the request over with its files; the
// running one opens the files, then makes the quick note (main.cpp connects the signal so).
TEST_F(QuickNoteTest, theCommandLineRequestReachesTheRunningInstance) {
    AppController c;
    Mode mode(c, DocumentMode::Mode::Xopp);
    c.setLibraryRoot(root);
    const QString key = QString("xqt-quicknote-test-%1").arg(QCoreApplication::applicationPid());
    SingleInstance primary(key);
    ASSERT_TRUE(primary.listen());
    QSignalSpy files(&primary, &SingleInstance::filesRequested);
    QSignalSpy quick(&primary, &SingleInstance::quickNoteRequested);
    QObject::connect(&primary, &SingleInstance::filesRequested, &c, &AppController::openPaths);
    QObject::connect(&primary, &SingleInstance::quickNoteRequested, &c, &AppController::quickNote);

    bool handedOver = false;
    std::thread second([&] {
        SingleInstance other(key);
        handedOver = other.sendToRunningInstance({SingleInstance::QUICK_NOTE}, 3000);
    });
    QElapsedTimer t;
    t.start();
    while (quick.isEmpty() && t.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    processEventsFor(50);
    second.join();
    EXPECT_TRUE(handedOver);
    ASSERT_EQ(files.count(), 1);
    EXPECT_TRUE(files.first().first().toStringList().isEmpty()) << "the request is no file (it shows the window)";
    ASSERT_EQ(quick.count(), 1);
    ASSERT_EQ(c.tabCount(), 1);
    EXPECT_EQ(currentFile(c).parent_path(), root / "Inbox");
    EXPECT_EQ(currentFile(c).extension(), ".xopp");
}

// With files: the files go on as files (without the request), then the request.
TEST(SingleInstanceQuickNote, filesAndTheRequestAreTold) {
    const QString key = QString("xqt-quicknote-files-%1").arg(QCoreApplication::applicationPid());
    SingleInstance primary(key);
    ASSERT_TRUE(primary.listen());
    QStringList order;
    QObject::connect(&primary, &SingleInstance::filesRequested, [&](const QStringList& paths) {
        order << "files:" + paths.join(',');
    });
    QObject::connect(&primary, &SingleInstance::quickNoteRequested, [&] { order << "quick note"; });
    std::thread second([&] {
        SingleInstance other(key);
        other.sendToRunningInstance({"/tmp/a.pdf", SingleInstance::QUICK_NOTE}, 3000);
    });
    QElapsedTimer t;
    t.start();
    while (order.size() < 2 && t.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    second.join();
    EXPECT_EQ(order, (QStringList{"files:/tmp/a.pdf", "quick note"}));
}
