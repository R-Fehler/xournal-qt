/*
 * xournal-qt: the page sidebar model (PagesModel), thumbnails and page operations through AppController.
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <thread>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "model/Font.h"
#include "model/Text.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "shell/PageFilterModel.h"
#include "shell/LayersModel.h"
#include "shell/OutlineModel.h"
#include "shell/PagesModel.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
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

QString fixture(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}

PagesModel& pagesOf(AppController& c) { return *qobject_cast<PagesModel*>(c.pagesModel()); }

std::string thumbnailUrl(PagesModel& m, int row) {
    return m.data(m.index(row), PagesModel::ThumbnailRole).toString().toStdString();
}

/// "image://thumbnail/<session>" of a thumbnail URL.
std::string sessionPart(const std::string& url) {
    return url.substr(0, url.find('/', std::string("image://thumbnail/").size()));
}

/// Layout and document agree: one layout slot per page, with the page's size.
void expectLayoutMatchesDocument(AppController& c) {
    DocumentSession* s = c.tabManager().currentSession();
    CanvasView* view = c.tabManager().view(c.currentTab());
    Document* doc = s->getDocument();
    ASSERT_EQ(view->pageCount(), doc->getPageCount());
    ASSERT_EQ(view->documentLayout().pageCount(), doc->getPageCount());
    for (size_t i = 0; i < doc->getPageCount(); ++i) {
        EXPECT_EQ(view->getPage(i)->getPage(), doc->getPage(i)) << "page " << i;
        const QRectF r = view->documentLayout().pageRect(i, 1.0);
        EXPECT_DOUBLE_EQ(r.width(), doc->getPage(i)->getWidth()) << "page " << i;
        EXPECT_DOUBLE_EQ(r.height(), doc->getPage(i)->getHeight()) << "page " << i;
    }
}
}  // namespace

TEST(Chapters, comeFromTheDocumentWhenNoPdfHasThem) {
    AppController c;
    c.newDocument();
    c.insertPageAfter(0);
    c.insertPageAfter(1);
    auto* outline = qobject_cast<OutlineModel*>(c.outlineModel());
    ASSERT_NE(outline, nullptr);
    outline->setRebuildDelay(10);  // (chapters written are read again once the writing paused)
    EXPECT_FALSE(outline->available()) << "an empty document has no contents";

    EXPECT_TRUE(c.addChapter(0, "Beginnings", 0));
    EXPECT_TRUE(c.addChapter(2, "The middle", 1));
    EXPECT_TRUE(outline->available());
    ASSERT_EQ(outline->count(), 2);
    EXPECT_EQ(outline->data(outline->index(0), OutlineModel::TitleRole).toString(), "Beginnings");
    EXPECT_EQ(outline->data(outline->index(0), OutlineModel::LevelRole).toInt(), 0);
    EXPECT_EQ(outline->data(outline->index(0), OutlineModel::PageRole).toInt(), 0);
    EXPECT_EQ(outline->data(outline->index(1), OutlineModel::TitleRole).toString(), "The middle");
    EXPECT_EQ(outline->data(outline->index(1), OutlineModel::LevelRole).toInt(), 1);
    EXPECT_EQ(outline->data(outline->index(1), OutlineModel::PageRole).toInt(), 2);

    // It is in the document: the text of the heading says so, and undo takes it back
    auto* s = c.tabManager().currentSession();
    bool found = false;
    for (const auto& element: s->getDocument()->getPage(0)->getSelectedLayer()->getElements()) {
        if (element->getType() == ELEMENT_TEXT) {
            found = static_cast<const Text*>(element.get())->getText() == "# Beginnings";
        }
    }
    EXPECT_TRUE(found);
    c.undo();
    processEvents(40);
    EXPECT_EQ(outline->count(), 1) << "undo takes the chapter back";
}

TEST(Chapters, comeFromTheHeadingsOfMarkdownBoxes) {
    AppController c;
    c.newDocument();
    auto* outline = qobject_cast<OutlineModel*>(c.outlineModel());
    ASSERT_NE(outline, nullptr);
    outline->setRebuildDelay(10);  // (the chapters are read again once the writing paused)
    c.beginMarkdown(0);
    c.updateMarkdown("# Intro\n\nSome text.\n\n## Details\n\n### Deeper\n\n#### Too deep for the contents\n");
    c.endMarkdown(true);
    processEvents(40);
    ASSERT_EQ(outline->count(), 3);
    EXPECT_EQ(outline->data(outline->index(0), OutlineModel::TitleRole).toString(), "Intro");
    EXPECT_EQ(outline->data(outline->index(0), OutlineModel::LevelRole).toInt(), 0);
    EXPECT_EQ(outline->data(outline->index(1), OutlineModel::TitleRole).toString(), "Details");
    EXPECT_EQ(outline->data(outline->index(1), OutlineModel::LevelRole).toInt(), 1);
    EXPECT_EQ(outline->data(outline->index(2), OutlineModel::TitleRole).toString(), "Deeper");
    EXPECT_EQ(outline->data(outline->index(2), OutlineModel::LevelRole).toInt(), 2);
}

TEST(PageLinks, followTheirPagesWhenPagesChange) {
    AppController c;
    c.newDocument();
    for (int i = 0; i < 4; ++i) {
        c.insertPageAfter(i);  // five pages
    }
    ASSERT_EQ(c.pageCount(), 5);

    // A note on page 1 that points to page 4
    auto* s = c.tabManager().currentSession();
    Document* doc = s->getDocument();
    auto text = std::make_unique<Text>();
    text->setText("see #Page:4 for the rest");
    text->setFont(XojFont("Sans", 12));
    text->move(40, 40);
    Text* raw = text.get();
    doc->lock();
    doc->getPage(0)->getSelectedLayer()->addElement(std::move(text));
    doc->unlock();

    // A page before it: the link counts on
    c.insertPageAfter(0);
    EXPECT_EQ(raw->getText(), "see #Page:5 for the rest");
    c.undoPages();
    EXPECT_EQ(raw->getText(), "see #Page:4 for the rest") << "and back";

    // A page behind it does not touch it
    c.insertPageAfter(4);
    EXPECT_EQ(raw->getText(), "see #Page:4 for the rest");

    // Deleting a page in front moves it up
    c.deletePage(1);
    EXPECT_EQ(raw->getText(), "see #Page:3 for the rest");
}

// Scrolling through a document changes the current page all the time. The layers of the new page are read again, but
// the model must only be reset when they really differ: a reset makes QML build all its rows again (7 ms), which made
// scrolling with the mouse on the scroll bar stutter.
TEST(Layers, goingToAnotherPageDoesNotResetTheModel) {
    AppController c;
    c.newDocument();
    c.insertPageAfter(0);
    auto* m = qobject_cast<LayersModel*>(c.layersModel());
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->rowCount(), 2);
    DocumentSession* s = c.tabManager().currentSession();
    s->setCurrentPageNo(0);  // (the inserted page is the current one)
    QSignalSpy reset(m, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy changed(m, &LayersModel::changed);

    s->setCurrentPageNo(1);
    EXPECT_EQ(reset.count(), 0) << "the same layers: nothing to build again";
    EXPECT_GE(changed.count(), 1) << "but the selected layer may be another one";
    s->setCurrentPageNo(0);
    EXPECT_EQ(reset.count(), 0);

    m->addLayer(false);  // another layer: the rows do change
    EXPECT_GE(reset.count(), 1);
    EXPECT_EQ(m->rowCount(), 3);
    s->setCurrentPageNo(1);
    EXPECT_EQ(m->rowCount(), 2) << "that page has one layer";
}

TEST(Layers, listAddRenameHideMoveAndRemove) {
    AppController c;
    c.newDocument();
    auto* m = qobject_cast<LayersModel*>(c.layersModel());
    ASSERT_NE(m, nullptr);
    // A new page: one layer and the background below it
    ASSERT_EQ(m->rowCount(), 2);
    EXPECT_EQ(m->data(m->index(1), LayersModel::IsBackgroundRole).toBool(), true);
    EXPECT_EQ(m->currentRow(), 0);

    m->addLayer(false);  // above the current one
    ASSERT_EQ(m->rowCount(), 3);
    EXPECT_EQ(m->currentRow(), 0) << "the new layer is on top and is drawn on";
    m->rename(0, "Notes");
    EXPECT_EQ(m->data(m->index(0), LayersModel::NameRole).toString(), "Notes");

    m->setVisible(0, false);
    EXPECT_FALSE(m->data(m->index(0), LayersModel::VisibleRole).toBool());
    m->setVisible(0, true);
    EXPECT_TRUE(m->data(m->index(0), LayersModel::VisibleRole).toBool());

    m->moveDown(0);
    EXPECT_EQ(m->data(m->index(1), LayersModel::NameRole).toString(), "Notes") << "one lower now";

    m->remove(1);
    EXPECT_EQ(m->rowCount(), 2);
    c.undo();  // layer operations are undoable
    EXPECT_EQ(m->rowCount(), 3);

    // The layers of the page that is shown
    c.insertPageAfter(0);
    EXPECT_EQ(m->rowCount(), 2) << "the new page has one layer again";
    c.goToPage(0);
    EXPECT_EQ(m->rowCount(), 3);
}

TEST(Pages, modelFollowsPageOperations) {
    AppController c;
    c.newDocument();
    PagesModel& m = pagesOf(c);
    EXPECT_EQ(m.rowCount(), 1);
    QSignalSpy count(&m, &PagesModel::countChanged);

    c.insertPageAfter(0);
    c.insertPageAfter(1);
    EXPECT_EQ(m.rowCount(), 3);
    EXPECT_EQ(m.currentPage(), 2);
    EXPECT_EQ(m.data(m.index(2), PagesModel::PageNumberRole).toInt(), 3);
    expectLayoutMatchesDocument(c);

    c.deletePage(0);
    EXPECT_EQ(m.rowCount(), 2);
    EXPECT_EQ(m.currentPage(), 0);
    expectLayoutMatchesDocument(c);

    c.duplicatePage(1);
    EXPECT_EQ(m.rowCount(), 3);
    expectLayoutMatchesDocument(c);
    c.movePageUp(2);
    expectLayoutMatchesDocument(c);

    c.undoPages();  // move
    c.undoPages();  // duplicate
    c.undoPages();  // delete
    EXPECT_EQ(m.rowCount(), 3);
    EXPECT_EQ(m.currentPage() >= 0, true);
    expectLayoutMatchesDocument(c);
    EXPECT_GE(count.count(), 5);
}

TEST(Pages, modelFollowsTheCurrentTab) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"packaged_xopp/pdfBackground/old.xopp")));
    PagesModel& m = pagesOf(c);
    EXPECT_EQ(m.rowCount(), 2);
    const std::string firstTab = sessionPart(thumbnailUrl(m, 0));
    c.newDocument();
    EXPECT_EQ(m.rowCount(), 1);
    EXPECT_NE(sessionPart(thumbnailUrl(m, 0)), firstTab) << "thumbnail URLs must name the tab's session";
    c.setCurrentTab(0);
    EXPECT_EQ(m.rowCount(), 2);
    EXPECT_EQ(sessionPart(thumbnailUrl(m, 0)), firstTab);
}

TEST(Pages, editsChangeTheThumbnailRevision) {
    AppController c;
    c.newDocument();
    PagesModel& m = pagesOf(c);
    m.setRefreshDelay(10);
    const std::string before = thumbnailUrl(m, 0);

    DocumentSession* s = c.tabManager().currentSession();
    auto page = s->getDocument()->getPage(0);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->addPoint(Point(10, 10, 1));
    stroke->addPoint(Point(50, 40, 1));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s->getDocument()->lock();
    layer->addElement(std::move(stroke));
    s->getDocument()->unlock();
    QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
    // Like the stroke tool: the undo action announces the changed page.
    s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    processEvents(80);
    ASSERT_GE(changed.count(), 1);
    const std::string afterEdit = thumbnailUrl(m, 0);
    EXPECT_NE(afterEdit, before);

    c.undo();
    processEvents(80);
    EXPECT_NE(thumbnailUrl(m, 0), afterEdit) << "undo must refresh the thumbnail too";
}

TEST(Pages, thumbnailShowsThePage) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"packaged_xopp/pdfBackground/old.xopp")));
    DocumentSession* s = c.tabManager().currentSession();
    const QImage img = ThumbnailProvider::render(*s, 0, 160);
    ASSERT_EQ(img.width(), 160);
    const auto* page = s->getDocument()->getPage(0).get();
    EXPECT_NEAR(img.height(), 160 * page->getHeight() / page->getWidth(), 1);
    // The PDF background (large dark shapes) is in the thumbnail.
    int dark = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            dark += qGray(img.pixel(x, y)) < 100;
        }
    }
    EXPECT_GT(dark, img.width() * img.height() / 20);
    EXPECT_TRUE(ThumbnailProvider::render(*s, 99, 160).isNull()) << "no page 99";
}

TEST(Pages, searchHitsPerPage) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"load/pages.xopp")));
    PagesModel& m = pagesOf(c);
    DocumentSession* s = c.tabManager().currentSession();
    QSignalSpy finished(&s->search(), &DocumentSearch::finished);
    c.setSearchQuery("p1");
    ASSERT_TRUE(finished.wait(3000));
    auto hitsOf = [&](int row) { return m.data(m.index(row), PagesModel::SearchHitsRole).toList(); };
    ASSERT_EQ(hitsOf(0).size(), 1);
    EXPECT_EQ(hitsOf(1).size(), 0);
    EXPECT_EQ(hitsOf(9).size(), 1);
    const QRectF r = hitsOf(0)[0].toRectF();
    EXPECT_GT(r.x(), 0);
    EXPECT_LT(r.right(), 1) << "relative to the page size";
    EXPECT_EQ(m.data(m.index(0), PagesModel::CurrentSearchHitRole).toInt(), 0);
    EXPECT_EQ(m.data(m.index(9), PagesModel::CurrentSearchHitRole).toInt(), -1);
    c.searchNext();
    EXPECT_EQ(m.data(m.index(9), PagesModel::CurrentSearchHitRole).toInt(), 0);
}

TEST(Pages, filterShowsOnlyPagesWithHits) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"load/pages.xopp")));
    auto* filter = qobject_cast<PageFilterModel*>(c.filteredPagesModel());
    ASSERT_NE(filter, nullptr);
    EXPECT_EQ(filter->count(), 11);
    DocumentSession* s = c.tabManager().currentSession();
    QSignalSpy finished(&s->search(), &DocumentSearch::finished);
    c.setSearchQuery("p1");
    ASSERT_TRUE(finished.wait(3000));
    EXPECT_EQ(c.searchHitPageCount(), 3);

    filter->setOnlySearchHits(true);
    ASSERT_EQ(filter->count(), 3);
    EXPECT_EQ(filter->index(1, 0).data(PagesModel::PageIndexRole).toInt(), 9);
    EXPECT_EQ(filter->rowOf(10), 2);
    EXPECT_EQ(filter->rowOf(4), -1);

    c.setSearchQuery("p5");  // new hits: filtered again
    QSignalSpy finished2(&s->search(), &DocumentSearch::finished);
    ASSERT_TRUE(finished2.wait(3000));
    EXPECT_EQ(filter->count(), 1);
    EXPECT_EQ(filter->index(0, 0).data(PagesModel::PageIndexRole).toInt(), 4);

    c.clearSearch();  // the filter ends with the search
    EXPECT_FALSE(filter->onlySearchHits());
    EXPECT_EQ(filter->count(), 11);
}

TEST(Pages, typicalAspectFollowsTheDocument) {
    AppController c;
    c.newDocument();
    PagesModel& m = pagesOf(c);
    EXPECT_NEAR(m.typicalAspect(), 1.414, 0.01) << "A4 portrait";
    // Slides (16:9) with one portrait page in between: the grid cells follow the slides.
    DocumentSession* s = c.tabManager().currentSession();
    auto slide = [] { return std::make_shared<XojPage>(1600.0, 900.0); };
    s->insertPage(slide(), 1);
    s->insertPage(slide(), 2);
    s->insertPage(slide(), 3);
    EXPECT_NEAR(m.typicalAspect(), 900.0 / 1600.0, 0.001);
}

// Text renders the same from several threads at once (thumbnails and the page renderer run in parallel).
TEST(Pages, concurrentTextRenderingIsComplete) {
    // Reference: rendered one after the other, from a separately loaded copy.
    std::vector<QImage> reference;
    {
        AppController r;
        ASSERT_TRUE(r.openPath(fixture(u8"load/pages.xopp")));
        for (size_t p = 0; p < 11; ++p) {
            reference.push_back(ThumbnailProvider::render(*r.tabManager().currentSession(), p, 200));
        }
    }
    // A freshly loaded document, rendered from several threads at once from the start.
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"load/pages.xopp")));
    DocumentSession* s = c.tabManager().currentSession();
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([&, t] {
            for (int round = 0; round < 5; ++round) {
                for (size_t p = 0; p < 11; ++p) {
                    const size_t page = (p + static_cast<size_t>(t)) % 11;
                    if (ThumbnailProvider::render(*s, page, 200) != reference[page]) {
                        ++mismatches;
                    }
                }
            }
        });
    }
    for (auto& th: threads) {
        th.join();
    }
    EXPECT_EQ(mismatches.load(), 0);
}

TEST(Pages, selectionLikeAFileManager) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"load/pages.xopp")));
    PagesModel& m = pagesOf(c);
    m.select(2);
    EXPECT_EQ(m.selectedPages(), QList<int>({2}));
    m.select(5, Qt::ShiftModifier);
    EXPECT_EQ(m.selectedPages(), QList<int>({2, 3, 4, 5}));
    m.select(8, Qt::ControlModifier);
    m.select(3, Qt::ControlModifier);
    EXPECT_EQ(m.selectedPages(), QList<int>({2, 4, 5, 8}));
    m.select(10, Qt::ShiftModifier | Qt::ControlModifier);  // adds the range from the last clicked page (3)
    EXPECT_EQ(m.selectedPages(), QList<int>({2, 3, 4, 5, 6, 7, 8, 9, 10}));
    m.select(1);
    EXPECT_EQ(m.selectionCount(), 1);
    EXPECT_TRUE(m.data(m.index(1), PagesModel::SelectedRole).toBool());
    m.clearSelection();
    EXPECT_EQ(m.selectionCount(), 0);
}

TEST(Pages, copyPasteDeleteMoveWithPageUndo) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"load/pages.xopp")));
    PagesModel& m = pagesOf(c);
    DocumentSession* s = c.tabManager().currentSession();
    const auto original = s->pageOrder();

    c.copyPages({0, 1});
    EXPECT_EQ(c.copiedPages(), 2);
    m.select(10);
    EXPECT_EQ(c.pastePages(), 2) << "after the selection";
    ASSERT_EQ(s->getDocument()->getPageCount(), 13u);
    EXPECT_EQ(m.selectedPages(), QList<int>({11, 12})) << "the pasted pages are selected";
    EXPECT_NE(s->pageOrder()[11], original[0]) << "a copy";
    EXPECT_EQ(s->pageOrder()[11]->getSelectedLayer()->getElements().size(),
              original[0]->getSelectedLayer()->getElements().size());

    QSignalSpy done(&c, &AppController::pageActionDone);
    ASSERT_TRUE(c.deletePages({11, 12}));
    EXPECT_EQ(s->pageOrder(), original);
    ASSERT_EQ(done.count(), 1);
    EXPECT_TRUE(done.first().at(1).toBool()) << "offered for undo";

    ASSERT_TRUE(c.movePages({0, 1}, 5));
    EXPECT_EQ(s->pageOrder()[3], original[0]);
    EXPECT_EQ(m.selectedPages(), QList<int>({3, 4})) << "the moved pages stay selected";
    EXPECT_FALSE(c.deletePages({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10})) << "not all pages";

    EXPECT_TRUE(c.canUndoPages());
    c.undoPages();  // move
    c.undoPages();  // delete
    c.undoPages();  // paste
    EXPECT_EQ(s->pageOrder(), original);
    EXPECT_FALSE(c.canUndoPages());
    EXPECT_FALSE(c.canUndo()) << "the annotation undo stack is separate";
    EXPECT_TRUE(c.canRedoPages());
}

TEST(Pages, pdfPagesPastedIntoAnotherDocumentBecomeImages) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"packaged_xopp/pdfBackground/old.xopp")));
    c.copyPages({1});
    c.pastePages(0);  // same document: still the PDF page
    DocumentSession* pdfDoc = c.tabManager().currentSession();
    EXPECT_TRUE(pdfDoc->getDocument()->getPage(0)->getBackgroundType().isPdfPage());
    EXPECT_EQ(pdfDoc->getDocument()->getPage(0)->getPdfPageNr(), 1u);

    c.newDocument();
    ASSERT_EQ(c.pastePages(1), 1);
    DocumentSession* other = c.tabManager().currentSession();
    auto page = other->getDocument()->getPage(1);
    EXPECT_TRUE(page->getBackgroundType().isImagePage());
    EXPECT_FALSE(page->getBackgroundImage().isEmpty());
    // The image shows the PDF page (the thumbnail has the dark shapes of the test PDF).
    const QImage thumb = ThumbnailProvider::render(*other, 1, 120);
    int dark = 0;
    for (int y = 0; y < thumb.height(); ++y) {
        for (int x = 0; x < thumb.width(); ++x) {
            dark += qGray(thumb.pixel(x, y)) < 100;
        }
    }
    EXPECT_GT(dark, thumb.width() * thumb.height() / 20);
}

TEST(Pages, insertPagesWithBackgroundSizeAndOrientation) {
    AppController c;
    c.newDocument();
    auto* settings = qobject_cast<SettingsModel*>(c.settingsModel());
    const QStringList formats = settings->pageBackgroundFormats();
    const int plain = static_cast<int>(formats.indexOf("plain"));
    const int graph = static_cast<int>(formats.indexOf("graph"));
    ASSERT_GE(plain, 0);
    ASSERT_GE(graph, 0);
    DocumentSession* s = c.tabManager().currentSession();
    Document* doc = s->getDocument();

    // Three plain landscape A4 pages after the first one
    ASSERT_TRUE(c.insertPages(1, plain, 1, true, 3));
    ASSERT_EQ(doc->getPageCount(), 4u);
    for (size_t i = 1; i <= 3; ++i) {
        EXPECT_NEAR(doc->getPage(i)->getWidth(), 841.89, 0.1);
        EXPECT_NEAR(doc->getPage(i)->getHeight(), 595.28, 0.1);
        EXPECT_EQ(doc->getPage(i)->getBackgroundType().format, PageTypeFormat::Plain);
    }
    EXPECT_EQ(c.pageNumber(), 2) << "shows the first new page";
    EXPECT_EQ(c.currentPageFormat().value("background").toInt(), plain);
    EXPECT_TRUE(c.currentPageFormat().value("landscape").toBool());

    // A graph page before the first one, with the size of the current page (landscape A4) turned to portrait
    ASSERT_TRUE(c.insertPages(0, graph, -1, false, 1));
    ASSERT_EQ(doc->getPageCount(), 5u);
    EXPECT_EQ(doc->getPage(0)->getBackgroundType().format, PageTypeFormat::Graph);
    EXPECT_NEAR(doc->getPage(0)->getWidth(), 595.28, 0.1);

    // One undo step each
    c.undoPages();
    EXPECT_EQ(doc->getPageCount(), 4u);
    c.undoPages();
    EXPECT_EQ(doc->getPageCount(), 1u);
    EXPECT_FALSE(c.insertPages(0, 999, -1, false, 1)) << "no such background";
}

namespace {
/// A 7-page PDF with an outline: cover (page 1), "Chapter 1" p.2 with "Section 1.1" p.3 and "Section 1.2" p.5,
/// "Chapter 2" p.6.
void makeOutlinePdf(const std::string& file) {
    cairo_surface_t* s = cairo_pdf_surface_create(file.c_str(), 400, 500);
    cairo_t* cr = cairo_create(s);
    for (int p = 1; p <= 7; ++p) {
        cairo_move_to(cr, 50, 50);
        cairo_show_text(cr, ("page " + std::to_string(p)).c_str());
        cairo_show_page(cr);
    }
    const int ch1 = cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 1", "page=2", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(s, ch1, "Section 1.1", "page=3", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(s, ch1, "Section 1.2", "page=5", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 2", "page=6", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}
}  // namespace

TEST(Pages, outlineWithPageRanges) {
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("book.pdf").toStdString();
    makeOutlinePdf(pdf);
    AppController c;
    ASSERT_TRUE(c.openPath(QString::fromStdString(pdf)));
    auto* m = qobject_cast<OutlineModel*>(c.outlineModel());
    ASSERT_NE(m, nullptr);
    ASSERT_TRUE(m->available());
    auto row = [&](int r) {
        return std::tuple(m->data(m->index(r), OutlineModel::TitleRole).toString().toStdString(),
                          m->data(m->index(r), OutlineModel::LevelRole).toInt(),
                          m->data(m->index(r), OutlineModel::PageRole).toInt(),
                          m->data(m->index(r), OutlineModel::PageEndRole).toInt());
    };
    ASSERT_EQ(m->count(), 5);
    EXPECT_EQ(row(0), std::tuple(std::string("Beginning"), 0, 0, 1)) << "the cover before the first entry";
    EXPECT_EQ(row(1), std::tuple(std::string("Chapter 1"), 0, 1, 2));
    EXPECT_EQ(row(2), std::tuple(std::string("Section 1.1"), 1, 2, 4));
    EXPECT_EQ(row(3), std::tuple(std::string("Section 1.2"), 1, 4, 5));
    EXPECT_EQ(row(4), std::tuple(std::string("Chapter 2"), 0, 5, 7));

    c.goToPage(3);
    EXPECT_EQ(m->currentRow(), 2) << "page 4 is in section 1.1";

    // A collapsed chapter has the pages of its sections
    m->toggle(1);
    ASSERT_EQ(m->count(), 3);
    EXPECT_EQ(row(1), std::tuple(std::string("Chapter 1"), 0, 1, 5));
    EXPECT_EQ(m->currentRow(), 1);
    m->toggle(1);
    EXPECT_EQ(m->count(), 5);

    // Pages inserted before: the entries move along
    c.insertPageBefore(0);
    EXPECT_EQ(row(1), std::tuple(std::string("Chapter 1"), 0, 2, 3));
    EXPECT_EQ(row(0), std::tuple(std::string("Beginning"), 0, 0, 2));
}
