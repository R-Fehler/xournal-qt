/*
 * xournal-qt: tabs (TabManager + AppController) and the single instance hand-over.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/SingleInstance.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "config-test.h"

using namespace xqt;

namespace {
void processEvents(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

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

QString fixture(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
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
    ASSERT_TRUE(c.openPath(fixture(u8"test1.xoj")));
    EXPECT_EQ(c.tabCount(), 1) << "the empty start document should have been replaced";
    EXPECT_EQ(c.title(), "test1.xoj");
    ASSERT_TRUE(c.openPath(fixture(u8"load/strokes.xopp")));
    EXPECT_EQ(c.tabCount(), 2);
    EXPECT_EQ(c.currentTab(), 1);
    EXPECT_EQ(c.title(), "strokes.xopp");
}

TEST(Tabs, openingAnOpenFileSwitchesToItsTab) {
    AppController c;
    ASSERT_TRUE(c.openPath(fixture(u8"test1.xoj")));
    ASSERT_TRUE(c.openPath(fixture(u8"load/strokes.xopp")));
    ASSERT_TRUE(c.openPath(fixture(u8"test1.xoj")));
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
    ASSERT_TRUE(c.openPath(fixture(u8"test1.xoj")));
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

TEST(Tabs, backgroundTabsReleaseTheirPageBuffers) {
    AppController c;
    ASSERT_TRUE(c.openPath(fixture(u8"test1.xoj")));
    TabManager& tabs = c.tabManager();
    tabs.setBackgroundReleaseDelay(50);
    CanvasView* first = tabs.view(0);
    first->getViewController().setViewSize(QSizeF(800, 1000));
    processEvents(100);
    c.context().getRenderService()->waitForIdle();
    processEvents(50);
    ASSERT_TRUE(first->getPage(0)->bufferInfo().valid) << "visible page of the current tab should be rendered";
    c.newDocument();  // first tab goes to the background
    processEvents(200);
    EXPECT_FALSE(first->getPage(0)->bufferInfo().valid) << "background tab kept its buffers";
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
    processEvents(50);
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
    ASSERT_TRUE(c.openPath(fixture(u8"packaged_xopp/pdfBackground/old.xopp")));
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

TEST(ToolbarColors, orangeByDefaultAddRemoveReset) {
    AppController c;
    c.resetToolbarColors();
    const QVariantList defaults = c.toolbarColors();
    ASSERT_EQ(defaults.size(), 9);
    EXPECT_EQ(defaults.last().value<QColor>(), QColor(255, 128, 0)) << "orange";
    QSignalSpy changed(&c, &AppController::toolbarColorsChanged);
    c.addToolbarColor(QColor("#123456"));
    c.addToolbarColor(QColor("#123456"));  // not twice
    EXPECT_EQ(c.toolbarColors().size(), 10);
    EXPECT_EQ(c.toolbarColors().last().value<QColor>(), QColor("#123456"));
    c.removeToolbarColor(0);  // black
    EXPECT_EQ(c.toolbarColors().size(), 9);
    EXPECT_NE(c.toolbarColors().first().value<QColor>(), QColor(Qt::black));
    EXPECT_GE(changed.count(), 2);
    c.resetToolbarColors();
    EXPECT_EQ(c.toolbarColors(), defaults);

    // Highlight colors: three presets, yellow first
    ASSERT_EQ(c.pdfHighlightColors().size(), 3);
    EXPECT_EQ(c.pdfHighlightColor(), c.pdfHighlightColors().first().value<QColor>());
    c.setPdfHighlightColor(c.pdfHighlightColors().at(2).value<QColor>());
    EXPECT_EQ(c.pdfHighlightColor(), c.pdfHighlightColors().at(2).value<QColor>());
    c.setPdfHighlightColor(c.pdfHighlightColors().first().value<QColor>());
}
