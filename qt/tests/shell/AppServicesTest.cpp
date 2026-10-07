/*
 * xournal-qt: what the windows of the process share (AppServices): one owner for the process-wide objects, the open
 * documents of every window (OpenDocuments) and the work off the UI thread that quitting waits for (BackgroundJobs).
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <memory>

#include <QCoreApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include "session/DocumentSession.h"
#include "shell/TabManager.h"

#include "AppController.h"
#include "AppServices.h"
#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::fixturePath;

using namespace xqt;

// A window of undocked documents is another controller on the same services: the same settings, tools, library and
// clipboard of pages, no copies and no pointers into the main window
TEST(AppServices, aSecondWindowSharesTheServicesOfTheFirst) {
    AppController c;
    c.newDocument();
    c.newDocument();
    c.undockTab(0);
    ASSERT_EQ(c.documentWindows().size(), 1u);
    AppController* second = c.documentWindows().front();
    EXPECT_EQ(&second->services(), &c.services());
    EXPECT_EQ(&second->context(), &c.context());
    EXPECT_EQ(second->libraryModel(), c.libraryModel());
    EXPECT_EQ(second->mainWindow(), &c);
    EXPECT_EQ(c.services().openDocuments().mainWindow(), &c);
    EXPECT_EQ(c.services().openDocuments().windows().size(), 2u);
    EXPECT_EQ(c.services().openDocuments().all().size(), 2u) << "the documents of both windows";
}

// main() holds the services; a window made on them is the main window, the next one a window of undocked documents.
// The main window going first takes its windows with it, before its own parts go.
TEST(AppServices, theMainWindowGoesFirstWithItsWindowsAndTheServicesStay) {
    AppServices services;
    auto main = std::make_unique<AppController>(services);
    EXPECT_FALSE(main->isSecondary());
    main->newDocument();
    main->newDocument();
    main->undockTab(1);
    ASSERT_EQ(main->documentWindows().size(), 1u);
    QPointer<AppController> second = main->documentWindows().front();
    EXPECT_TRUE(second->isSecondary());
    main.reset();
    EXPECT_TRUE(second.isNull()) << "deleted with the main window";
    EXPECT_TRUE(services.openDocuments().windows().empty());
    // (a new main window on the same services)
    AppController again(services);
    EXPECT_FALSE(again.isSecondary());
}

// Finding a file wherever it is open: its tab in any window, also a plain PDF (its document's background) or a text
// file when asked, never the document asking
TEST(AppServices, theOpenDocumentsOfAllWindowsAreFoundByTheirFile) {
    QTemporaryDir tmp;
    const fs::path pdf = fs::path(tmp.path().toStdString()) / "paper.pdf";
    xqt::test::makeTextPdf(pdf, {"words"});
    AppController c;
    ASSERT_TRUE(c.openPath(fixturePath(u8"load/pages.xopp")));
    const fs::path xopp = c.tabManager().currentSession()->getFilePath();
    ASSERT_TRUE(c.openPath(xqt::test::qstr(pdf)));
    DocumentSession* plain = c.tabManager().currentSession();
    ASSERT_FALSE(plain->hasFilePath()) << "a PDF without notes: no file of its own";
    c.undockTab(c.tabManager().indexOf(c.tabManager().session(0)));  // (the .xopp to a window of its own)
    ASSERT_EQ(c.documentWindows().size(), 1u);
    AppController* second = c.documentWindows().front();

    OpenDocuments& open = c.services().openDocuments();
    auto found = open.find(xopp);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].first, second) << "in the other window";
    EXPECT_EQ(found[0].second, second->tabManager().session(0));
    EXPECT_TRUE(open.find(xopp, {.except = found[0].second}).empty());
    EXPECT_TRUE(open.find(pdf).empty()) << "a plain PDF only when asked";
    found = open.find(pdf, {.plainPdf = true});
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].first, &c);
    EXPECT_EQ(found[0].second, plain);
}

// Quitting waits for the work started off the UI thread (it may be writing a document): it is not left to the global
// pool's destructor after main() returned
TEST(AppServices, quittingWaitsForTheBackgroundJobs) {
    AppController c;
    std::atomic<bool> done{false};
    c.services().jobs().start(
            [&done] {
                QThread::msleep(300);
                done = true;
            },
            BackgroundJobs::Priority::Normal);
    c.shutdown();
    EXPECT_TRUE(done) << "shutdown returned before the job was done";
}
