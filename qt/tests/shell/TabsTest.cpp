/*
 * xournal-qt: tabs (TabManager + AppController) and the single instance hand-over.
 *
 * @license GNU GPLv2 or later
 */
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cairo-pdf.h>

#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/SingleInstance.h"
#include "shell/ShortcutsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include <QQuickImageResponse>
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "hwr/HandwritingSearch.h"

#include "AppController.h"
#include "AppServices.h"
#include "CurrentDocument.h"
#include "CanvasMemory.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::processEventsFor;

using xqt::test::fixturePath;

using namespace xqt;

namespace {

void scribble(DocumentSession& s) {
    auto page = s.getDocument()->getPage(0);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->addPoint(Point(10, 10, 1));
    stroke->addPoint(Point(50, 40, 1));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    layer->addElement(std::move(stroke));
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

}  // namespace

TEST(Tabs, controllerStartsWithTheHomeScreen) {
    AppController c;
    EXPECT_EQ(c.tabCount(), 0);
    EXPECT_TRUE(c.homeVisible());
    EXPECT_EQ(c.view(), nullptr);
    EXPECT_FALSE(c.modified());
    c.newDocument();
    EXPECT_EQ(c.tabCount(), 1);
    EXPECT_FALSE(c.homeVisible());
    EXPECT_EQ(c.title(), "Untitled");
    c.setHomeVisible(true);  // the home tab; the document stays open behind it
    EXPECT_TRUE(c.homeVisible());
    c.nextTab();  // Ctrl+Tab: back to the document
    EXPECT_FALSE(c.homeVisible());
    c.closeTab(0);  // no document left: the home screen
    EXPECT_EQ(c.tabCount(), 0);
    EXPECT_TRUE(c.homeVisible());
}

TEST(Tabs, openingReplacesTheUntouchedNewDocument) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixturePath(u8"test1.xoj")));
    EXPECT_EQ(c.tabCount(), 1) << "the empty start document should have been replaced";
    EXPECT_EQ(c.title(), "test1.xoj");
    ASSERT_TRUE(c.openPath(fixturePath(u8"load/strokes.xopp")));
    EXPECT_EQ(c.tabCount(), 2);
    EXPECT_EQ(c.currentTab(), 1);
    EXPECT_EQ(c.title(), "strokes.xopp");
}

TEST(Tabs, openingAnOpenFileSwitchesToItsTab) {
    AppController c;
    ASSERT_TRUE(c.openPath(fixturePath(u8"test1.xoj")));
    ASSERT_TRUE(c.openPath(fixturePath(u8"load/strokes.xopp")));
    ASSERT_TRUE(c.openPath(fixturePath(u8"test1.xoj")));
    EXPECT_EQ(c.tabCount(), 2);
    EXPECT_EQ(c.currentTab(), 0);
    EXPECT_EQ(c.title(), "test1.xoj");
}

TEST(Tabs, tabsAreIndependentDocuments) {
    AppController c;
    c.newDocument();
    c.newDocument();
    ASSERT_EQ(c.tabCount(), 2);
    scribble(*c.tabManager().session(1));
    EXPECT_TRUE(c.tabModified(1));
    EXPECT_FALSE(c.tabModified(0));
    EXPECT_EQ(c.modifiedTabs(), QVariantList{1});
    c.setCurrentTab(0);
    EXPECT_FALSE(c.canUndo()) << "undo history belongs to the other tab";
    c.setCurrentTab(1);
    EXPECT_TRUE(c.canUndo());
    c.undo();
    EXPECT_FALSE(c.tabModified(1));
}

TEST(Tabs, closingTabs) {
    AppController c;
    c.newDocument();
    c.newDocument();
    c.newDocument();
    ASSERT_EQ(c.tabCount(), 3);
    c.setCurrentTab(1);
    QSignalSpy docChanged(&c, &AppController::documentChanged);
    c.closeTab(1);  // the current one: its right neighbour becomes current
    EXPECT_EQ(c.tabCount(), 2);
    EXPECT_EQ(c.currentTab(), 1);
    EXPECT_GE(docChanged.count(), 1);
    c.closeTab(0);
    EXPECT_EQ(c.currentTab(), 0);
    c.closeTab(0);  // the last tab: the home screen
    EXPECT_EQ(c.tabCount(), 0);
    EXPECT_TRUE(c.homeVisible());
    EXPECT_EQ(c.title(), "");
}

TEST(Tabs, modelDataAndMoving) {
    AppController c;
    ASSERT_TRUE(c.openPath(fixturePath(u8"test1.xoj")));
    c.newDocument();
    TabManager& tabs = c.tabManager();
    EXPECT_EQ(tabs.data(tabs.index(0), TabManager::TitleRole).toString(), "test1.xoj");
    EXPECT_TRUE(tabs.data(tabs.index(1), TabManager::CurrentRole).toBool());
    QSignalSpy dataChanged(&tabs, &QAbstractItemModel::dataChanged);
    scribble(*tabs.session(1));
    EXPECT_TRUE(tabs.data(tabs.index(1), TabManager::ModifiedRole).toBool());
    EXPECT_GE(dataChanged.count(), 1);
    c.moveTab(1, 0);
    EXPECT_EQ(c.currentTab(), 0);
    EXPECT_EQ(tabs.data(tabs.index(1), TabManager::TitleRole).toString(), "test1.xoj");
}

// The order of use (a phone's recent tabs, the double tap on its tab count): the current one first, then the one used
// before it, like Alt+Tab; a closed tab leaves it, moving a tab keeps it
TEST(Tabs, theOrderOfUse) {
    AppController c;
    c.newDocument();
    c.newDocument();
    c.newDocument();
    ASSERT_EQ(c.tabCount(), 3);  // A B C, C current
    EXPECT_EQ(c.tabsByUse(), (QList<int>{2, 1, 0}));
    c.setCurrentTab(0);  // A
    c.setCurrentTab(2);  // C
    EXPECT_EQ(c.tabsByUse(), (QList<int>{2, 0, 1}));
    c.previousUsedTab();
    EXPECT_EQ(c.currentTab(), 0) << "back to the one used before";
    c.previousUsedTab();
    EXPECT_EQ(c.currentTab(), 2) << "and again: to and fro";
    c.moveTab(2, 0);  // C A B
    EXPECT_EQ(c.tabsByUse(), (QList<int>{0, 1, 2})) << "moving keeps the order of use";
    c.closeTab(1);  // A goes: C B
    EXPECT_EQ(c.tabsByUse(), (QList<int>{0, 1}));
    c.previousUsedTab();
    EXPECT_EQ(c.currentTab(), 1);
    c.setHomeVisible(true);
    c.previousUsedTab();
    EXPECT_FALSE(c.homeVisible()) << "from the home screen: the document behind it";
    EXPECT_EQ(c.currentTab(), 1);
}

TEST(SingleInstanceTest, filesAreHandedToTheRunningInstance) {
    const QString key = QString("xqt-test-%1").arg(QCoreApplication::applicationPid());
    SingleInstance primary(key);
    ASSERT_TRUE(primary.listen());
    QSignalSpy requested(&primary, &SingleInstance::filesRequested);

    // The second "instance" runs in a thread: sendToRunningInstance blocks until the primary acknowledges.
    bool handedOver = false;
    std::thread second([&] {
        SingleInstance other(key);
        handedOver = other.sendToRunningInstance({"/tmp/a.pdf", "/tmp/b.xopp"}, 3000);
    });
    QElapsedTimer t;
    t.start();
    while (requested.isEmpty() && t.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    processEventsFor(50);
    second.join();
    ASSERT_EQ(requested.count(), 1);
    EXPECT_EQ(requested.first().first().toStringList(), (QStringList{"/tmp/a.pdf", "/tmp/b.xopp"}));
    EXPECT_TRUE(handedOver);

    SingleInstance nobody(key + "-unused");
    EXPECT_FALSE(nobody.sendToRunningInstance({"/tmp/c.pdf"}, 200));
}

TEST(Tabs, pageLayoutAppliesToAllTabs) {
    AppController c;
    c.newDocument();
    c.newDocument();
    ASSERT_EQ(c.tabCount(), 2);
    QSignalSpy changed(&c, &AppController::viewLayoutChanged);
    c.setViewColumns(3);
    EXPECT_EQ(c.viewColumns(), 3);
    EXPECT_EQ(c.tabManager().view(0)->documentLayout().columns(), 3u);
    EXPECT_EQ(c.tabManager().view(1)->documentLayout().columns(), 3u);
    c.setPairedPages(true);  // pairs need an even column count
    EXPECT_EQ(c.tabManager().view(0)->documentLayout().columns(), 4u);
    c.setPairedPages(false);
    c.setViewColumns(1);
    EXPECT_EQ(c.tabManager().view(1)->documentLayout().columns(), 1u);
    EXPECT_EQ(changed.count(), 4);
}

TEST(Export, pdfExportAndSuggestedName) {
    QTemporaryDir tmp;
    AppController c;
    ASSERT_TRUE(c.openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    EXPECT_TRUE(c.suggestedExportFile().toLocalFile().endsWith("old.pdf"));
    const QString out = tmp.filePath("export.pdf");
    ASSERT_TRUE(c.exportPdf(QUrl::fromLocalFile(out)));
    QFile f(out);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_TRUE(f.read(5) == "%PDF-");
    // The exported PDF has the document's pages.
    auto r = DocumentSession::loadFile(fs::path(out.toStdString()));
    ASSERT_TRUE(r.document);
    EXPECT_EQ(r.document->getPageCount(), 2u);

    // An unsaved annotated PDF: never suggest overwriting the PDF itself.
    AppController d;
    ASSERT_TRUE(d.openPath(QString::fromStdString(fs::path(r.document->getPdfFilepath()).string())));
    EXPECT_TRUE(d.suggestedExportFile().toLocalFile().endsWith("export_annotated.pdf"));
}

TEST(Shortcuts, noKeysAreUsedTwice) {
    // Two actions with the same keys make Qt report an ambiguous shortcut: then neither of them happens.
    AppController c;
    auto* shortcuts = qobject_cast<ShortcutsModel*>(c.shortcutsModel());
    ASSERT_NE(shortcuts, nullptr);
    std::map<QString, QString> owner;
    for (int row = 0; row < shortcuts->rowCount(); ++row) {
        const QString id = shortcuts->data(shortcuts->index(row), ShortcutsModel::IdRole).toString();
        QStringList seen;
        for (const QString& keys: shortcuts->keys(id)) {
            EXPECT_FALSE(seen.contains(keys)) << id.toStdString() << " lists " << keys.toStdString() << " twice";
            seen << keys;
            const auto it = owner.find(keys);
            EXPECT_EQ(it, owner.end()) << keys.toStdString() << " is used by " << id.toStdString() << " and "
                                       << (it == owner.end() ? std::string() : it->second.toStdString());
            owner[keys] = id;
        }
    }
}

TEST(SaveAs, suggestsTheDocumentsOwnFolderAndTheLibraryForNewOnes) {
    QTemporaryDir tmp;
    const fs::path lib = fs::path(tmp.path().toStdString()) / "Library";
    fs::create_directories(lib);
    AppController c;
    c.setLibraryRoot(lib);
    c.newDocument();
    // Never saved: in this window's library (not wherever something was saved last)
    EXPECT_EQ(fs::path(c.suggestedSaveFile().toLocalFile().toStdString()).parent_path(), lib);
    EXPECT_EQ(fs::path(c.suggestedSaveFile().toLocalFile().toStdString()).extension(), ".xopp");

    // A document that has a file: its own folder and name
    ASSERT_TRUE(c.openPath(fixturePath(u8"load/pages.xopp")));
    const fs::path opened = c.tabManager().currentSession()->getFilePath();
    EXPECT_EQ(fs::path(c.suggestedSaveFile().toLocalFile().toStdString()), opened);
}

TEST(Windows, aTabMovesToAWindowOfItsOwnAndBack) {
    AppController c;
    ASSERT_TRUE(c.openPath(fixturePath(u8"load/pages.xopp")));
    c.newDocument();
    ASSERT_EQ(c.tabManager().count(), 2);
    const QString first = c.tabManager().session(0)->getFilePath().string().c_str();

    c.undockTab(0);
    ASSERT_EQ(c.documentWindows().size(), 1u);
    AppController* window = c.documentWindows().front();
    EXPECT_TRUE(window->isSecondary());
    EXPECT_EQ(window->mainWindow(), &c);
    EXPECT_FALSE(window->homeVisible()) << "a window of its own shows documents only";
    EXPECT_EQ(c.tabManager().count(), 1) << "the document left the main window";
    ASSERT_EQ(window->tabManager().count(), 1);
    EXPECT_EQ(QString(window->tabManager().session(0)->getFilePath().string().c_str()), first)
            << "with its own session (undo history, zoom)";
    EXPECT_EQ(&window->context(), &c.context()) << "the same settings, tools and rendering";

    // The last document of such a window stays there
    window->undockTab(0);
    EXPECT_EQ(c.documentWindows().size(), 1u);
    EXPECT_EQ(window->tabManager().count(), 1);

    // Back to the main window: the window is left without documents
    QSignalSpy closing(window, &AppController::closeWindowRequested);
    window->dockTab(0);
    EXPECT_EQ(c.tabManager().count(), 2);
    EXPECT_EQ(window->tabManager().count(), 0);
    EXPECT_EQ(closing.count(), 1);
    window->windowClosed();
    EXPECT_TRUE(c.documentWindows().empty());
    QCoreApplication::processEvents();  // (the controller is deleted later)
}

// A window closed but not deleted yet (its deleteLater waits for the event loop, which a quit may never run again) goes
// before the services of the main window: its destructor still uses them (ThreadSanitizer found the use after free).
TEST(Windows, aClosedWindowNotDeletedYetGoesBeforeTheServicesOfTheMainWindow) {
    auto c = std::make_unique<AppController>();
    ASSERT_TRUE(c->openPath(fixturePath(u8"load/pages.xopp")));
    c->newDocument();
    c->undockTab(0);
    ASSERT_EQ(c->documentWindows().size(), 1u);
    AppController* window = c->documentWindows().front();
    window->windowClosed();  // (no event loop runs after it: deleted with the main window)
    bool servicesGone = false;
    bool windowWentFirst = false;
    QObject::connect(&c->services().handwriting(), &QObject::destroyed, [&] { servicesGone = true; });
    QObject::connect(window, &QObject::destroyed, [&] { windowWentFirst = !servicesGone; });
    c.reset();
    EXPECT_TRUE(servicesGone);
    EXPECT_TRUE(windowWentFirst) << "the closed window outlived the services it uses";
}

TEST(Windows, closingAWindowKeepsDocumentsWithUnsavedChanges) {
    AppController c;
    c.newDocument();
    c.newDocument();
    ASSERT_EQ(c.tabManager().count(), 2);
    c.undockTab(0);
    AppController* window = c.documentWindows().front();
    ASSERT_EQ(window->tabManager().count(), 1);
    // A change nobody saved: it must not go away with the window
    window->insertPages(0, 0, -1, false, 1);
    ASSERT_TRUE(window->tabManager().session(0)->isModified());
    window->windowClosed();
    EXPECT_EQ(c.tabManager().count(), 2) << "the changed document went back to the main window";
    QCoreApplication::processEvents();
}

// Highlight colors of PDF text: three presets, yellow first
TEST(PdfHighlightColors, threePresetsYellowFirst) {
    AppController c;
    ASSERT_EQ(c.pdfHighlightColors().size(), 3);
    EXPECT_EQ(c.pdfHighlightColor(), c.pdfHighlightColors().first().value<QColor>());
    c.setPdfHighlightColor(c.pdfHighlightColors().at(2).value<QColor>());
    EXPECT_EQ(c.pdfHighlightColor(), c.pdfHighlightColors().at(2).value<QColor>());
    c.setPdfHighlightColor(c.pdfHighlightColors().first().value<QColor>());
}

TEST(ToolSizes, fiveWidthsTheFifthAdjustableAndRemembered) {
    {
        AppController c;
        c.selectTool("pen");
        c.setSize(3);
        ToolHandler* th = c.context().getToolHandler();
        EXPECT_EQ(c.size(), 3);
        const double thick = th->getThickness();
        c.setSize(4);  // the fourth: very thick
        EXPECT_EQ(c.size(), 4);
        EXPECT_GT(th->getThickness(), thick);
        EXPECT_DOUBLE_EQ(c.sizeWidth(4), th->getThickness());

        // The fifth: a width of its own
        c.setSize(5);
        EXPECT_EQ(c.size(), 5);
        EXPECT_DOUBLE_EQ(th->getThickness(), c.customWidth());
        c.setCustomWidth(6.25);
        EXPECT_EQ(c.size(), 5) << "setting the width chooses it";
        EXPECT_DOUBLE_EQ(th->getThickness(), 6.25);

        // Per tool, and a size switches back
        c.selectTool("highlighter");
        EXPECT_NE(c.customWidth(), 6.25);
        EXPECT_NE(c.size(), 5) << "the highlighter keeps its own size";
        c.selectTool("pen");
        EXPECT_EQ(c.size(), 5);
        c.setSize(2);
        EXPECT_EQ(c.size(), 2);
        EXPECT_NE(th->getThickness(), 6.25);
        // (the eraser in hand at the end: the next start takes the toolbox's eraser, the pen keeps its state)
        c.takeToolOfType("eraser");
    }
    AppController again;  // the width is in the settings
    again.selectTool("pen");
    EXPECT_DOUBLE_EQ(again.customWidth(), 6.25);
    EXPECT_EQ(again.size(), 2) << "the size that was chosen last";
}

namespace {
/// A PDF whose pages take a while to draw (many curves)
void makeHeavyPdf(const std::string& path, int pages, int curves) {
    cairo_surface_t* s = cairo_pdf_surface_create(path.c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    unsigned seed = 7;
    auto rnd = [&seed](double max) {
        seed = seed * 1103515245u + 12345u;
        return static_cast<double>((seed >> 8) % 10000) / 10000.0 * max;
    };
    for (int p = 0; p < pages; ++p) {
        for (int i = 0; i < curves; ++i) {
            cairo_move_to(cr, rnd(595), rnd(842));
            cairo_curve_to(cr, rnd(595), rnd(842), rnd(595), rnd(842), rnd(595), rnd(842));
            cairo_set_source_rgba(cr, rnd(1), rnd(1), rnd(1), 0.5);
            cairo_set_line_width(cr, 0.5 + rnd(2));
            cairo_stroke(cr);
        }
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}
}  // namespace

// Regression test: closing a tab of a big PDF froze the window until work queued for it was done: the thumbnails
// asked for (the sidebar, the page grid) were all drawn first, and the pages queued for rendering in advance were
// rendered one after the other while the pages were taken down. Closing waits only for what is running.
TEST(Tabs, closingATabDoesNotWaitForQueuedWork) {
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("heavy.pdf").toStdString();
    QElapsedTimer made;
    made.start();
    makeHeavyPdf(pdf, 40, 150);
    AppController c;
    ASSERT_TRUE(c.openPath(QString::fromStdString(pdf)));
    CanvasView* view = c.tabManager().currentView();
    view->setDevicePixelRatio(2);
    view->getViewController().setViewSize(QSizeF(1100, 1600));
    view->setShown(true);
    processEventsFor(50);
    QElapsedTimer one;
    one.start();
    DocumentSession* s = c.tabManager().currentSession();
    ThumbnailProvider::render(*s, 5, 1024);
    const qint64 oneMs = std::max<qint64>(one.elapsed(), 1);
    std::cout << "PDF made in " << made.elapsed() << " ms, a thumbnail drawn in " << oneMs << " ms\n";

    // The sidebar asks for the thumbnails of all pages (bigger than the previews: they are drawn)
    ThumbnailProvider provider;
    const quint64 id = ThumbnailProvider::idOf(s);
    std::vector<QQuickImageResponse*> responses;
    int finished = 0;
    for (size_t p = 0; p < s->getDocument()->getPageCount(); ++p) {
        responses.push_back(provider.requestImageResponse(
                QString("%1/%2/%3").arg(id).arg(p).arg(s->pageRevision(p)), QSize(1024, 1448)));
        QObject::connect(responses.back(), &QQuickImageResponse::finished, [&finished] { ++finished; });
    }
    // and the canvas plans the pages to render in advance
    CanvasMemory::instance().planNow();
    ASSERT_TRUE(c.context().getRenderService()->hasWork(RenderService::Priority::Preload));
    const int drawnBefore = ThumbnailProvider::renderCount();

    QElapsedTimer t;
    t.start();
    c.closeTab(c.currentTab());
    const qint64 closeMs = t.elapsed();
    const int drawnWhileClosing = ThumbnailProvider::renderCount() - drawnBefore;
    std::cout << "closed in " << closeMs << " ms, " << drawnWhileClosing << " thumbnails drawn meanwhile\n";
    EXPECT_LE(drawnWhileClosing, 4) << "only those being drawn (one per worker) may finish";
    EXPECT_LT(closeMs, 6 * oneMs + 200) << "closing waited for queued work";
    QElapsedTimer rest;  // (a response goes when it has finished, as in QML)
    rest.start();
    while (finished < static_cast<int>(responses.size()) && rest.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    for (auto* r: responses) {
        delete r;
    }
}

// XQT_BENCH_PDF=<big pdf>: how long closing its tab takes while previews, thumbnails and pages in advance are busy
TEST(Tabs, benchClosingABigPdf) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    QTemporaryDir dir;
    const QString copy = dir.filePath("bench.pdf");
    ASSERT_TRUE(QFile::copy(pdf, copy));
    AppController c;
    for (int round = 0; round < 3; ++round) {
        ASSERT_TRUE(c.openPath(copy));
        CanvasView* view = c.tabManager().currentView();
        view->setDevicePixelRatio(2);
        view->getViewController().setViewSize(QSizeF(1100, 1600));
        view->setShown(true);
        view->getViewController().scrollToPage(view->pageCount() / 2);
        DocumentSession* s = c.tabManager().currentSession();
        ThumbnailProvider provider;
        const quint64 id = ThumbnailProvider::idOf(s);
        std::vector<QQuickImageResponse*> responses;
        int finished = 0;
        for (size_t p = view->pageCount() / 2; p < view->pageCount() / 2 + 60; ++p) {
            responses.push_back(provider.requestImageResponse(
                    QString("%1/%2/%3").arg(id).arg(p).arg(s->pageRevision(p)), QSize(1024, 1448)));
            QObject::connect(responses.back(), &QQuickImageResponse::finished, [&finished] { ++finished; });
        }
        processEventsFor(round * 700 + 400);  // (rendering, drawing previews and thumbnails meanwhile)
        QElapsedTimer t;
        t.start();
        c.closeTab(c.currentTab());
        std::cout << "closed after " << round * 700 + 400 << " ms of work in " << t.elapsed() << " ms\n";
        while (finished < static_cast<int>(responses.size())) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        qDeleteAll(responses);
    }
}

// The current document (CurrentDocument): it follows the current tab, relays the signals of that tab's document and
// view only, and says once that another document is current; the window's properties follow it
TEST(Tabs, theCurrentDocumentFollowsTheCurrentTabAndRelaysItsSignalsOnly) {
    AppController c;
    c.newDocument();
    c.newDocument();
    ASSERT_EQ(c.tabManager().count(), 2);
    ASSERT_EQ(c.currentTab(), 1);
    CurrentDocument& d = c.currentDocument();
    EXPECT_EQ(d.session(), c.tabManager().session(1));
    EXPECT_EQ(d.view(), c.tabManager().view(1));

    QSignalSpy changed(&d, &CurrentDocument::changed);
    QSignalSpy modified(&d, &CurrentDocument::modifiedChanged);
    QSignalSpy windowModified(&c, &AppController::modifiedChanged);
    QSignalSpy windowTitle(&c, &AppController::titleChanged);
    c.setCurrentTab(0);
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(d.session(), c.tabManager().session(0));
    EXPECT_EQ(d.view(), c.tabManager().view(0));
    EXPECT_GE(windowModified.count(), 1) << "another document: the window's properties are read again";
    EXPECT_GE(windowTitle.count(), 1);

    modified.clear();
    windowModified.clear();
    c.tabManager().session(1)->insertNewPage(1);  // (the other tab)
    EXPECT_EQ(modified.count(), 0) << "not the current document";
    EXPECT_EQ(windowModified.count(), 0);
    c.tabManager().session(0)->insertNewPage(1);
    EXPECT_GE(modified.count(), 1);
    EXPECT_GE(windowModified.count(), 1);
}
