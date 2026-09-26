/*
 * xournal-qt: space for notes beside slides (qt/docs/note-space.md): the model and its .xopp round trip, applying it
 * (one undo step), drawing (the PDF at its offset, the ink with it), PDF text (selection, search, boxes), the plain
 * PDF export, the hybrid PDF (larger boxes, text found where it is) and its incremental saves.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "control/ExportHelper.h"
#include "control/PdfCache.h"
#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "session/HybridPdf.h"
#include "session/PageNoteSpace.h"
#include "session/TextMatch.h"
#include "undo/UndoRedoHandler.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

#include "../SearchHits.h"

using namespace xqt;

namespace {
constexpr double W = 842, H = 474;  // a 16:9 slide (points)

/// Slides with a word each ("slideone", ...) at (72, 100), 24 pt.
void makeSlides(const fs::path& p, int count) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), W, H);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    for (int i = 0; i < count; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, i == 0 ? "slideone" : ("slide" + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

Stroke* addStroke(DocumentSession& s, size_t page, std::vector<Point> points, Color color = Color(0xffcc0000U)) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setToolType(StrokeTool::PEN);
    stroke->setColor(color);
    stroke->setWidth(3);
    for (const auto& p: points) {
        stroke->addPoint(p);
    }
    stroke->getBoundingBox();
    Stroke* raw = stroke.get();
    std::unique_lock lock(*s.getDocument());
    s.getDocument()->getPage(page)->getSelectedLayer()->addElement(std::move(stroke));
    return raw;
}

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A page as the app draws it (DocumentView with a PdfCache, as the canvas does), 1 px per point.
cairo_surface_t* drawPage(Document& doc, size_t index) {
    PageRef page = doc.getPage(index);
    const int w = static_cast<int>(std::ceil(page->getWidth())), h = static_cast<int>(std::ceil(page->getHeight()));
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    PdfCache cache(doc.getPdfDocument(), nullptr);
    DocumentView view;
    view.setPdfCache(&cache);
    view.drawPage(page, cr, false, xoj::view::BACKGROUND_SHOW_ALL);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

/// A page of a PDF as poppler draws it (with annotations), white behind, 1 px per point.
cairo_surface_t* renderPdf(const fs::path& pdf, size_t page) {
    XojPdfDocument doc;
    EXPECT_TRUE(doc.load(pdf, "", nullptr)) << pdf;
    XojPdfPageSPtr p = doc.getPage(page);
    const int w = static_cast<int>(std::ceil(p->getWidth())), h = static_cast<int>(std::ceil(p->getHeight()));
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    p->render(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

/// Pixels in the rectangle that are dark (text) / red (the pen stroke).
int count(cairo_surface_t* s, int x0, int y0, int x1, int y1, bool red) {
    const unsigned char* p = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(h, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
            const unsigned char b = p[y * stride + 4 * x], g = p[y * stride + 4 * x + 1], r = p[y * stride + 4 * x + 2];
            const unsigned char a = p[y * stride + 4 * x + 3];
            if (red ? (a > 128 && r > 150 && g < 100 && b < 100) : (a > 128 && r < 100 && g < 100 && b < 100)) {
                ++n;
            }
        }
    }
    return n;
}

/// Where poppler finds a word on a page of a PDF file (its first hit; page coordinates, y down).
QRectF foundIn(const fs::path& pdf, size_t page, const std::string& word) {
    XojPdfDocument doc;
    EXPECT_TRUE(doc.load(pdf, "", nullptr));
    const auto hits = doc.getPage(page)->findText(word);
    if (hits.empty()) {
        return {};
    }
    return QRectF(QPointF(hits[0].x1, hits[0].y1), QPointF(hits[0].x2, hits[0].y2)).normalized();
}

class NoteSpaceTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    std::unique_ptr<DocumentSession> slides(int count, const char* name = "slides.pdf") {
        makeSlides(path(name), count);
        auto r = DocumentSession::loadFile(path(name));
        EXPECT_TRUE(r.document) << r.error;
        return std::make_unique<DocumentSession>(*app, std::move(r.document));
    }
    static notespace::Amounts points(double l, double t, double r, double b) {
        notespace::Amounts a;
        a.left = l;
        a.top = t;
        a.right = r;
        a.bottom = b;
        return a;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

// --- the model ------------------------------------------------------------------------------------------------------

TEST_F(NoteSpaceTest, theSpaceAndTheLargerPageRoundTripInXopp) {
    auto s = slides(2);
    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 200, 0)), 1u);
    PageRef p = s->getDocument()->getPage(0);
    EXPECT_EQ(p->getWidth(), 100 + W + 200);
    EXPECT_EQ(p->getHeight(), 50 + H);
    EXPECT_EQ(p->getNoteSpace(), (NoteSpace{100, 50, 200, 0}));

    const fs::path xopp = path("notes.xopp");
    ASSERT_TRUE(s->saveAs(xopp).ok);
    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document) << loaded.error;
    PageRef back = loaded.document->getPage(0);
    EXPECT_EQ(back->getWidth(), 100 + W + 200);
    EXPECT_EQ(back->getHeight(), 50 + H);
    EXPECT_EQ(back->getNoteSpace(), (NoteSpace{100, 50, 200, 0}));
    EXPECT_TRUE(back->getBackgroundType().isPdfPage());
    EXPECT_EQ(back->getPdfPageNr(), 0u);
    EXPECT_TRUE(loaded.document->getPage(1)->getNoteSpace().empty());
    EXPECT_EQ(loaded.document->getPage(1)->getWidth(), W);

    // In the file: one attribute on the page that has space, none on the other
    SaveHandler h;
    {
        std::shared_lock lock(*s->getDocument());
        h.prepareSave(s->getDocument(), path("plain.xopp"));
    }
    h.saveTo(path("plain.xopp"));
    auto again = DocumentSession::loadFile(path("plain.xopp"));
    ASSERT_TRUE(again.document);
    EXPECT_EQ(again.document->getPage(0)->getNoteSpace(), (NoteSpace{100, 50, 200, 0}));
}

TEST_F(NoteSpaceTest, inkStaysOnTheSlideAndNoSpaceRestoresThePageExactly) {
    auto s = slides(1);
    Stroke* stroke = addStroke(*s, 0, {Point(100, 200), Point(180, 210)});
    ASSERT_EQ(notespace::apply(*s, {0}, points(72, 36, 0, 100)), 1u);
    EXPECT_EQ(stroke->getPointVector()[0].x, 172);
    EXPECT_EQ(stroke->getPointVector()[0].y, 236);
    // Another amount: from the slide, not added to what was there
    ASSERT_EQ(notespace::apply(*s, {0}, points(10, 0, 0, 0)), 1u);
    EXPECT_EQ(stroke->getPointVector()[0].x, 110);
    EXPECT_EQ(stroke->getPointVector()[0].y, 200);
    EXPECT_EQ(s->getDocument()->getPage(0)->getWidth(), W + 10);
    EXPECT_EQ(s->getDocument()->getPage(0)->getHeight(), H);
    ASSERT_EQ(notespace::apply(*s, {0}, points(0, 0, 0, 0)), 1u);
    EXPECT_EQ(stroke->getPointVector()[0].x, 100);
    EXPECT_EQ(stroke->getPointVector()[1].y, 210);
    EXPECT_EQ(s->getDocument()->getPage(0)->getWidth(), W);
    EXPECT_EQ(s->getDocument()->getPage(0)->getHeight(), H);
    EXPECT_TRUE(s->getDocument()->getPage(0)->getNoteSpace().empty());
    EXPECT_EQ(notespace::apply(*s, {0}, points(0, 0, 0, 0)), 0u) << "nothing to change";
}

TEST_F(NoteSpaceTest, relativeAmountsArePerPageAndImagePagesAreLeftOut) {
    auto s = slides(1);
    auto portrait = std::make_shared<XojPage>(595, 842);
    portrait->setBackgroundType(PageType(PageTypeFormat::Ruled));
    auto image = std::make_shared<XojPage>(300, 200);
    image->setBackgroundType(PageType(PageTypeFormat::Image));
    s->insertPages({portrait, image}, 1);
    notespace::Amounts half;
    half.relative = true;
    half.right = 0.5;
    half.bottom = 1;
    EXPECT_EQ(notespace::apply(*s, {0, 1, 2}, half), 2u);
    EXPECT_EQ(s->getDocument()->getPage(0)->getNoteSpace(), (NoteSpace{0, 0, 421, H}));
    EXPECT_EQ(s->getDocument()->getPage(1)->getNoteSpace(), (NoteSpace{0, 0, 297.5 + 0.5, 842}));  // (whole points)
    EXPECT_TRUE(s->getDocument()->getPage(2)->getNoteSpace().empty());
    EXPECT_EQ(s->getDocument()->getPage(2)->getWidth(), 300);
}

// A 300-page PDF: all pages at once, quickly, one step to undo
TEST_F(NoteSpaceTest, allPagesOfALongPdfAreOneQuickUndoStep) {
    auto s = slides(300);
    Stroke* on7 = addStroke(*s, 7, {Point(10, 20), Point(30, 40)});
    std::vector<size_t> all(300);
    for (size_t i = 0; i < all.size(); ++i) {
        all[i] = i;
    }
    notespace::Amounts a;
    a.relative = true;
    a.left = 0.25;
    a.bottom = 0.5;
    QElapsedTimer t;
    t.start();
    ASSERT_EQ(notespace::apply(*s, all, a), 300u);
    const qint64 ms = t.elapsed();
    std::cout << "[          ] space for notes on 300 pages: " << ms << " ms" << std::endl;
    EXPECT_LT(ms, 2000);
    for (size_t i = 0; i < 300; ++i) {
        ASSERT_EQ(s->getDocument()->getPage(i)->getWidth(), W + 211) << i;  // (a quarter: 210.5, whole points)
        ASSERT_EQ(s->getDocument()->getPage(i)->getHeight(), H + 237) << i;
    }
    EXPECT_EQ(on7->getPointVector()[0].x, 221);
    UndoRedoHandler* undo = s->getUndoRedoHandler();
    ASSERT_TRUE(undo->canUndo());
    undo->undo();
    EXPECT_FALSE(undo->canUndo()) << "one step";
    for (size_t i = 0; i < 300; ++i) {
        ASSERT_EQ(s->getDocument()->getPage(i)->getWidth(), W) << i;
        ASSERT_TRUE(s->getDocument()->getPage(i)->getNoteSpace().empty()) << i;
    }
    EXPECT_EQ(on7->getPointVector()[0].x, 10);
    EXPECT_EQ(on7->getPointVector()[0].y, 20);
    undo->redo();
    EXPECT_EQ(s->getDocument()->getPage(299)->getNoteSpace(), (NoteSpace{211, 0, 0, 237}));
    EXPECT_EQ(on7->getPointVector()[0].x, 221);
}

TEST_F(NoteSpaceTest, aBlankPageAfterEachSlideIsOneStep) {
    auto s = slides(3);
    EXPECT_EQ(notespace::insertBlankAfter(*s, {0, 2}), 2u);
    Document& doc = *s->getDocument();
    ASSERT_EQ(doc.getPageCount(), 5u);
    EXPECT_TRUE(doc.getPage(0)->getBackgroundType().isPdfPage());
    EXPECT_EQ(doc.getPage(1)->getBackgroundType().format, PageTypeFormat::Plain);
    EXPECT_EQ(doc.getPage(1)->getWidth(), W);
    EXPECT_TRUE(doc.getPage(2)->getBackgroundType().isPdfPage());
    EXPECT_EQ(doc.getPage(4)->getBackgroundType().format, PageTypeFormat::Plain);
    s->getUndoRedoHandler()->undo();
    EXPECT_EQ(doc.getPageCount(), 3u);
}

// --- drawing --------------------------------------------------------------------------------------------------------

TEST_F(NoteSpaceTest, thePdfIsDrawnAtTheOffsetWithTheInkOnIt) {
    auto s = slides(1);
    // A red line under the word, on the slide
    addStroke(*s, 0, {Point(72, 108), Point(180, 108)});
    cairo_surface_t* before = drawPage(*s->getDocument(), 0);
    ASSERT_GT(count(before, 72, 75, 180, 102, false), 50) << "the word";
    ASSERT_GT(count(before, 72, 104, 180, 112, true), 100) << "the line";

    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 200, 80)), 1u);
    cairo_surface_t* after = drawPage(*s->getDocument(), 0);
    EXPECT_EQ(cairo_image_surface_get_width(after), static_cast<int>(100 + W + 200));
    EXPECT_EQ(cairo_image_surface_get_height(after), static_cast<int>(50 + H + 80));
    EXPECT_EQ(count(after, 72, 75, 172, 102, false), 0) << "nothing where the word was";
    // The same picture of the slide, moved by (100, 50): the word and the line, each where it was relative to it
    EXPECT_EQ(count(after, 172, 125, 280, 152, false), count(before, 72, 75, 180, 102, false));
    EXPECT_EQ(count(after, 172, 154, 280, 162, true), count(before, 72, 104, 180, 112, true));
    // The space is white paper
    EXPECT_EQ(count(after, 0, 0, 100, 50 + H, false), 0);
    const unsigned char* px = cairo_image_surface_get_data(after);
    EXPECT_EQ(px[4 * 10 + 3], 255) << "opaque";
    EXPECT_EQ(px[4 * 10 + 2], 255) << "white";
    cairo_surface_destroy(before);
    cairo_surface_destroy(after);

    // Without a PdfCache (thumbnails, previews): the same place
    cairo_surface_t* thumb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1142, 604);
    cairo_t* cr = cairo_create(thumb);
    PageRef page = s->getDocument()->getPage(0);
    notespace::renderPdf(cr, *page, *s->getDocument()->getPdfPage(0));
    cairo_destroy(cr);
    cairo_surface_flush(thumb);
    EXPECT_EQ(count(thumb, 72, 75, 172, 102, false), 0);
    EXPECT_GT(count(thumb, 172, 125, 280, 152, false), 50);
    cairo_surface_destroy(thumb);
}

// --- PDF text -------------------------------------------------------------------------------------------------------

TEST_F(NoteSpaceTest, pdfTextSearchAndItsBoxesAreAtTheOffset) {
    auto s = slides(2);
    const auto was = DocumentSearch::findOnPage(*s->getDocument(), 0, "slideone");
    ASSERT_EQ(was.size(), 1u);
    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 0, 0)), 1u);

    // poppler's search of the page
    const auto hits = DocumentSearch::findOnPage(*s->getDocument(), 0, "slideone");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_NEAR(hits[0].left(), was[0].left() + 100, 0.01);
    EXPECT_NEAR(hits[0].top(), was[0].top() + 50, 0.01);

    // The search of the document (its text index) marks it there too
    s->search().setQuery("slideone", true);
    const auto placed = test::placedHits(s->search());
    ASSERT_EQ(placed.size(), 1u);
    EXPECT_NEAR(placed[0].rect.left(), hits[0].left(), 1.0);
    EXPECT_NEAR(placed[0].rect.top(), hits[0].top(), 2.0);

    // termRects (the library's pictures of pages with hits)
    {
        PdfLayoutReader reader(s->getDocument()->getPdfFilepath());
        std::shared_lock lock(*s->getDocument());
        const auto rects = termRects(*s->getDocument()->getPage(0), &reader, std::vector<textmatch::Term>{{textmatch::prepare(QStringLiteral("slideone"))}});
        ASSERT_FALSE(rects.empty());
        EXPECT_NEAR(rects[0].left(), hits[0].left(), 1.0);
    }

    // (selecting and marking it on the canvas: CanvasReplayTest.withSpaceForNotesPdfTextIsSelectedAndMarkedOnTheSlide)
}

// --- exports --------------------------------------------------------------------------------------------------------

TEST_F(NoteSpaceTest, thePlainPdfExportHasTheLargerPagesWithTheTextWhereItIs) {
    auto s = slides(2);
    addStroke(*s, 0, {Point(72, 108), Point(180, 108)});
    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 200, 0)), 1u);
    const fs::path out = path("export.pdf");
    ExportHelper::exportPdf(s->getDocument(), out, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_NEAR(pdf.getPage(0)->getWidth(), 100 + W + 200, 0.01);
    EXPECT_NEAR(pdf.getPage(0)->getHeight(), 50 + H, 0.01);
    EXPECT_NEAR(pdf.getPage(1)->getWidth(), W, 0.01);
    const QRectF word = foundIn(out, 0, "slideone");
    ASSERT_FALSE(word.isNull()) << "the text stays text";
    EXPECT_NEAR(word.left(), 172, 2);
    cairo_surface_t* page = renderPdf(out, 0);
    EXPECT_GT(count(page, 172, 154, 280, 162, true), 100) << "the line under the word";
    EXPECT_GT(count(page, 172, 125, 280, 152, false), 50) << "the word";
    cairo_surface_destroy(page);
}

TEST_F(NoteSpaceTest, theHybridPdfHasLargerBoxesAndOtherViewersFindTheText) {
    auto s = slides(2);
    addStroke(*s, 0, {Point(72, 108), Point(180, 108)});
    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 200, 0)), 1u);
    const fs::path out = path("slides.notes.pdf");
    const auto r = s->saveAsHybrid(out);
    ASSERT_TRUE(r.ok) << r.error;

    QPDF q;
    q.processFile(out.string().c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 2u);
    const auto media = pages[0].getMediaBox().getArrayAsRectangle();
    const auto crop = pages[0].getCropBox().getArrayAsRectangle();
    EXPECT_NEAR(media.urx - media.llx, 100 + W + 200, 0.01);
    EXPECT_NEAR(media.ury - media.lly, 50 + H, 0.01);
    EXPECT_NEAR(crop.llx, -100, 0.01) << "the content stays where it was: the box grows around it";
    EXPECT_NEAR(crop.ury, H + 50, 0.01);
    EXPECT_NEAR(pages[1].getMediaBox().getArrayAsRectangle().urx, W, 0.01) << "the other page as it was";

    // Other viewers (poppler): the slide at the offset, the ink on it, the word found where it is drawn
    const QRectF word = foundIn(out, 0, "slideone");
    ASSERT_FALSE(word.isNull());
    EXPECT_NEAR(word.left(), 172, 2);
    EXPECT_NEAR(word.top(), foundIn(path("slides.pdf"), 0, "slideone").top() + 50, 0.5);
    cairo_surface_t* page = renderPdf(out, 0);
    EXPECT_EQ(cairo_image_surface_get_width(page), static_cast<int>(100 + W + 200));
    EXPECT_GT(count(page, 172, 154, 280, 162, true), 100) << "the line under the word";
    EXPECT_GT(count(page, 172, 125, 280, 152, false), 50) << "the word";
    EXPECT_EQ(count(page, 72, 75, 172, 102, false), 0);
    cairo_surface_destroy(page);

    // Opened again: the same space; its background (the clean copy) has the slide's own size
    auto opened = DocumentSession::loadFile(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(opened.document->getPage(0)->getNoteSpace(), (NoteSpace{100, 50, 200, 0}));
    XojPdfDocument clean;
    ASSERT_TRUE(clean.load(opened.document->getPdfFilepath(), "", nullptr));
    EXPECT_NEAR(clean.getPage(0)->getWidth(), W, 0.01);
    EXPECT_NEAR(clean.getPage(0)->getHeight(), H, 0.01);
    cairo_surface_t* shown = drawPage(*opened.document, 0);
    EXPECT_GT(count(shown, 172, 125, 280, 152, false), 50) << "drawn in the app at the offset, once";
    EXPECT_EQ(count(shown, 72, 75, 172, 102, false), 0);
    cairo_surface_destroy(shown);
}

TEST_F(NoteSpaceTest, theArchivePdfHasTheLargerPagesWithTheInkMergedInPlace) {
    auto s = slides(1);
    addStroke(*s, 0, {Point(72, 108), Point(180, 108)});
    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 200, 0)), 1u);
    const fs::path out = path("slides.archive.pdf");
    const auto r = HybridPdf::writeArchive(*s->getDocument(), out);
    ASSERT_TRUE(r.ok) << r.error;
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_NEAR(pdf.getPage(0)->getWidth(), 100 + W + 200, 0.01);
    EXPECT_NEAR(foundIn(out, 0, "slideone").left(), 172, 2);
    cairo_surface_t* page = renderPdf(out, 0);
    EXPECT_GT(count(page, 172, 154, 280, 162, true), 100) << "the ink under the word";
    EXPECT_GT(count(page, 172, 125, 280, 152, false), 50) << "the word";
    cairo_surface_destroy(page);
    auto opened = DocumentSession::loadFile(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(opened.document->getPage(0)->getNoteSpace(), (NoteSpace{100, 50, 200, 0}));
    cairo_surface_t* shown = drawPage(*opened.document, 0);
    EXPECT_EQ(count(shown, 72, 75, 172, 102, false), 0) << "its background is the page without our boxes";
    EXPECT_GT(count(shown, 172, 125, 280, 152, false), 50);
    cairo_surface_destroy(shown);
}

TEST_F(NoteSpaceTest, rotatedPagesGrowOnTheSidesAsShown) {
    // A landscape slide stored as a portrait page turned by 90 degrees
    makeSlides(path("plain.pdf"), 1);
    {
        QPDF q;
        q.processFile(path("plain.pdf").string().c_str());
        QPDFPageObjectHelper p = QPDFPageDocumentHelper(q).getAllPages()[0];
        p.getObjectHandle().replaceKey("/Rotate", QPDFObjectHandle::newInteger(90));
        QPDFWriter w(q, path("rotated.pdf").string().c_str());
        w.write();
    }
    auto r = DocumentSession::loadFile(path("rotated.pdf"));
    ASSERT_TRUE(r.document);
    DocumentSession s(*app, std::move(r.document));
    const double w = s.getDocument()->getPage(0)->getWidth(), h = s.getDocument()->getPage(0)->getHeight();
    ASSERT_NEAR(w, H, 0.01) << "shown turned";
    addStroke(s, 0, {Point(20, 20), Point(60, 20)});
    ASSERT_EQ(notespace::apply(s, {0}, points(0, 0, 300, 0)), 1u);
    const fs::path out = path("rotated.notes.pdf");
    ASSERT_TRUE(s.saveAsHybrid(out).ok);
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_NEAR(pdf.getPage(0)->getWidth(), w + 300, 0.01) << "wider as shown";
    EXPECT_NEAR(pdf.getPage(0)->getHeight(), h, 0.01);
    // The slide is where it was (at the top left), the space on the right is white
    cairo_surface_t* shown = renderPdf(out, 0);
    cairo_surface_t* orig = renderPdf(path("rotated.pdf"), 0);
    EXPECT_EQ(count(shown, 0, 0, static_cast<int>(w), static_cast<int>(h), false),
              count(orig, 0, 0, static_cast<int>(w), static_cast<int>(h), false));
    EXPECT_EQ(count(shown, static_cast<int>(w) + 1, 0, static_cast<int>(w) + 300, static_cast<int>(h), false), 0);
    EXPECT_GT(count(shown, 20, 16, 60, 24, true), 40) << "the ink where it was";
    cairo_surface_destroy(shown);
    cairo_surface_destroy(orig);
}

TEST_F(NoteSpaceTest, anIncrementalSaveFollowsAChangeOfTheSpace) {
    HybridPdf::compactAbove = 1000;  // (the small test file: never compacted)
    struct Restore {
        ~Restore() { HybridPdf::compactAbove = 0.25; }
    } restore;
    auto s = slides(3);
    addStroke(*s, 1, {Point(72, 108), Point(180, 108)});
    const fs::path out = path("slides.notes.pdf");
    ASSERT_TRUE(s->saveAsHybrid(out).ok);
    const std::string first = bytesOf(out);

    // Space on page 2 (with ink) and page 3 (without): appended
    ASSERT_EQ(notespace::apply(*s, {1, 2}, points(100, 0, 0, 60)), 2u);
    auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_TRUE(bytesOf(out).substr(0, first.size()) == first) << "appended";
    {
        XojPdfDocument pdf;
        ASSERT_TRUE(pdf.load(out, "", nullptr));
        EXPECT_NEAR(pdf.getPage(0)->getWidth(), W, 0.01);
        EXPECT_NEAR(pdf.getPage(1)->getWidth(), W + 100, 0.01);
        EXPECT_NEAR(pdf.getPage(2)->getHeight(), H + 60, 0.01);
    }
    EXPECT_NEAR(foundIn(out, 1, "slide2").left(), 172, 2);
    cairo_surface_t* page = renderPdf(out, 1);
    EXPECT_GT(count(page, 172, 104, 280, 112, true), 100) << "the ink moved with the slide";
    cairo_surface_destroy(page);
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(loaded.document->getPage(2)->getNoteSpace(), (NoteSpace{100, 0, 0, 60}));

    // Other amounts, then none: the boxes follow; without space the page has its own size again
    ASSERT_EQ(notespace::apply(*s, {1}, points(0, 0, 150, 0)), 1u);
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_NEAR(foundIn(out, 1, "slide2").left(), 72, 2);
    {
        XojPdfDocument pdf;
        ASSERT_TRUE(pdf.load(out, "", nullptr));
        EXPECT_NEAR(pdf.getPage(1)->getWidth(), W + 150, 0.01);
    }
    ASSERT_EQ(notespace::apply(*s, {1, 2}, points(0, 0, 0, 0)), 2u);
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    QPDF q;
    q.processFile(out.string().c_str());
    for (auto& p: QPDFPageDocumentHelper(q).getAllPages()) {
        const auto m = p.getMediaBox().getArrayAsRectangle();
        EXPECT_NEAR(m.urx - m.llx, W, 0.01);
        EXPECT_NEAR(m.ury - m.lly, H, 0.01);
        EXPECT_FALSE(p.getObjectHandle().hasKey("/XournalQtBoxes"));
    }
    page = renderPdf(out, 1);
    EXPECT_GT(count(page, 72, 104, 180, 112, true), 100) << "the ink back on the slide";
    cairo_surface_destroy(page);
}

// --- upstream Xournal++ ---------------------------------------------------------------------------------------------

// Upstream reads the page at its larger size and draws the PDF at the top left (it knows no offset): space on the right
// and below shows exactly as here; with space on the left or at the top the ink is shifted against the slide by it.
TEST_F(NoteSpaceTest, upstreamXournalppShowsTheLargerPageWithThePdfAtTheTopLeft) {
    const QString upstream = qEnvironmentVariableIsSet("XOJ_UPSTREAM_BIN") ? qEnvironmentVariable("XOJ_UPSTREAM_BIN")
                                                                          : QString(XQT_UPSTREAM_BIN);
    if (!QFileInfo(upstream).isExecutable()) {
        GTEST_SKIP() << "no upstream xournalpp at " << upstream.toStdString() << " (set XOJ_UPSTREAM_BIN)";
    }
    auto s = slides(2);
    addStroke(*s, 0, {Point(72, 108), Point(180, 108)});
    addStroke(*s, 1, {Point(72, 108), Point(180, 108)});
    ASSERT_EQ(notespace::apply(*s, {0}, points(100, 50, 200, 0)), 1u);
    ASSERT_EQ(notespace::apply(*s, {1}, points(0, 0, 421, 474)), 1u);
    ASSERT_TRUE(s->saveAs(path("notes.xopp")).ok);
    auto run = [&](const QString& page, const QString& png) {
        QProcess p;
        // Its own configuration and caches: never the user's (AGENTS.md)
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const char* var: {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME"}) {
            env.insert(var, tmp.filePath(QString("upstream-") + var));
        }
        p.setProcessEnvironment(env);
        p.start(upstream, {tmp.filePath("notes.xopp"), "--create-img=" + tmp.filePath(png), "--export-png-dpi=72",
                           "--export-range=" + page});
        EXPECT_TRUE(p.waitForFinished(60000)) << "upstream did not finish";
        const QString output = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
        EXPECT_EQ(p.exitCode(), 0) << output.toStdString();
        EXPECT_FALSE(output.contains("error", Qt::CaseInsensitive)) << output.toStdString();
        return cairo_image_surface_create_from_png(tmp.filePath(png).toStdString().c_str());
    };
    cairo_surface_t* left = run("1", "left.png");
    ASSERT_EQ(cairo_surface_status(left), CAIRO_STATUS_SUCCESS);
    EXPECT_EQ(cairo_image_surface_get_width(left), static_cast<int>(100 + W + 200)) << "the larger page";
    EXPECT_EQ(cairo_image_surface_get_height(left), static_cast<int>(50 + H));
    EXPECT_GT(count(left, 72, 75, 180, 102, false), 50) << "the slide at the top left";
    EXPECT_GT(count(left, 172, 154, 280, 162, true), 100) << "the ink where it is on the page (shifted on the slide)";
    cairo_surface_destroy(left);

    cairo_surface_t* right = run("2", "right.png");
    ASSERT_EQ(cairo_surface_status(right), CAIRO_STATUS_SUCCESS);
    EXPECT_EQ(cairo_image_surface_get_width(right), static_cast<int>(W + 421));
    EXPECT_EQ(cairo_image_surface_get_height(right), static_cast<int>(2 * H));
    EXPECT_GT(count(right, 72, 75, 180, 102, false), 50) << "the slide where it is here too";
    EXPECT_GT(count(right, 72, 104, 180, 112, true), 100) << "the ink on it, as here";
    EXPECT_EQ(count(right, static_cast<int>(W) + 5, 0, static_cast<int>(W) + 421, static_cast<int>(2 * H), false), 0);
    cairo_surface_destroy(right);
}
