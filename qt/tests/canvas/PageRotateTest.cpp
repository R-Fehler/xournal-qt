/*
 * xournal-qt: turning pages by a quarter turn (PageRotate.h, qt/docs/features/page-rotation.md). The author: "rotate
 * all pages / selected pages / current page by 90 degree left or right". The size swaps, the content turns about the
 * page (a stroke at a known place ends up at the turned place), Markdown boxes and sticky notes stay upright, the
 * page's text flows anew, space for notes and image backgrounds turn, one undo step that puts everything back exactly,
 * the thumbnails are drawn again (the page revision). PDF pages: left alone in a .xopp; in a PDF with notes the PDF
 * page itself turns (its /Rotate), with our ink and the embedded document to match.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "control/xojfile/LoadHandler.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/MergedPdf.h"
#include "session/PageMargins.h"
#include "session/PageNoteSpace.h"
#include "session/StickyNote.h"
#include "session/TextDocument.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

#include "MarkdownSession.h"
#include "MdBox.h"
#include "PageRotate.h"

using namespace xqt;
using pagerotate::PdfPages;
using pagerotate::Turn;

namespace {
constexpr double MM = 72.0 / 25.4;
constexpr double A4_W = 210 * MM, A4_H = 297 * MM;

Stroke* addStroke(DocumentSession& s, size_t page, std::vector<Point> points, double width = 2,
                  Color color = Color(0xffcc0000U)) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setToolType(StrokeTool::PEN);
    stroke->setColor(color);
    stroke->setWidth(width);
    for (const auto& p: points) {
        stroke->addPoint(p);
    }
    Stroke* raw = stroke.get();
    std::unique_lock lock(*s.getDocument());
    s.getDocument()->getPage(page)->getSelectedLayer()->addElement(std::move(stroke));
    return raw;
}

/// A PDF of A4 pages with a word on each, at (72, 100)
void makePdf(const fs::path& p, const std::vector<std::string>& words) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    for (const auto& w: words) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, w.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// A page of a PDF as poppler draws it (with its annotations), one pixel per point, white behind
cairo_surface_t* renderPdf(const fs::path& pdf, size_t page) {
    XojPdfDocument doc;
    EXPECT_TRUE(doc.load(pdf, "", nullptr)) << pdf;
    XojPdfPageSPtr p = doc.getPage(page);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, static_cast<int>(std::ceil(p->getWidth())),
                                                    static_cast<int>(std::ceil(p->getHeight())));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    p->render(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}
bool redAt(cairo_surface_t* s, int x, int y) {
    const unsigned char* d = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + 4 * x;
    return d[2] > 150 && d[1] < 100 && d[0] < 100;
}

/// The /Rotate of a page of a PDF file
int rotateOf(const fs::path& pdf, size_t page) {
    QPDF q;
    q.processFile(pdf.string().c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    auto r = pages.at(page).getAttribute("/Rotate", false);
    return r.isInteger() ? static_cast<int>(r.getIntValue()) : 0;
}

class PageRotateTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
        session->getUndoRedoHandler()->clearContents();
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    PageRef page(size_t i) const { return session->getDocument()->getPage(i); }
    size_t pageCount() const { return session->getDocument()->getPageCount(); }
    pagerotate::Result turn(std::vector<size_t> pages, Turn t, PdfPages pdf = PdfPages::Kept) {
        return pagerotate::apply(*session, pages, t, pdf);
    }
    UndoRedoHandler* undo() const { return session->getUndoRedoHandler(); }
    /// A document opened from a PDF of these words (one page each)
    void openPdf(const std::vector<std::string>& words) {
        makePdf(path("slides.pdf"), words);
        auto r = DocumentSession::loadFile(path("slides.pdf"));
        ASSERT_TRUE(r.document) << r.error;
        session = std::make_unique<DocumentSession>(*app, std::move(r.document));
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
};
}  // namespace

// A stroke at a known place ends up where the turned page puts it; the sizes swap; undo puts back every point
// exactly; redo turns again; to the left is the other way
TEST_F(PageRotateTest, aStrokeEndsUpAtTheTurnedPlace) {
    const double w = page(0)->getWidth(), h = page(0)->getHeight();
    ASSERT_NE(w, h);
    Stroke* stroke = addStroke(*session, 0, {Point(100, 200, 0.5), Point(150, 260, 0.7), Point(160.25, 270.125)});
    const std::vector<Point> before = stroke->getPointVector();

    ASSERT_EQ(turn({0}, Turn::Right).pages, 1u);
    EXPECT_EQ(page(0)->getWidth(), h);
    EXPECT_EQ(page(0)->getHeight(), w);
    // (x, y) -> (h - y, x): the top left goes to the top right
    auto pts = stroke->getPointVector();
    ASSERT_EQ(pts.size(), 3u);
    EXPECT_DOUBLE_EQ(pts[0].x, h - 200);
    EXPECT_DOUBLE_EQ(pts[0].y, 100);
    EXPECT_DOUBLE_EQ(pts[1].x, h - 260);
    EXPECT_DOUBLE_EQ(pts[1].y, 150);
    EXPECT_EQ(pts[1].z, 0.7) << "the pressure stays";
    const auto box = stroke->getBoundingBox();
    EXPECT_NEAR(box.x, h - 270.125, 2) << "the bounding box follows";
    EXPECT_NEAR(box.y, 100, 2);

    undo()->undo();
    EXPECT_EQ(page(0)->getWidth(), w);
    EXPECT_EQ(page(0)->getHeight(), h);
    pts = stroke->getPointVector();
    for (size_t i = 0; i < pts.size(); ++i) {
        EXPECT_EQ(pts[i].x, before[i].x) << "exactly: " << i;
        EXPECT_EQ(pts[i].y, before[i].y) << i;
        EXPECT_EQ(pts[i].z, before[i].z) << i;
    }
    EXPECT_FALSE(undo()->canUndo()) << "one step";
    undo()->redo();
    EXPECT_DOUBLE_EQ(stroke->getPointVector()[0].x, h - 200);
    EXPECT_EQ(page(0)->getWidth(), h);
    undo()->undo();

    // (x, y) -> (y, w - x): the top left goes to the bottom left
    ASSERT_EQ(turn({0}, Turn::Left).pages, 1u);
    pts = stroke->getPointVector();
    EXPECT_DOUBLE_EQ(pts[0].x, 200);
    EXPECT_DOUBLE_EQ(pts[0].y, w - 100);
    EXPECT_EQ(page(0)->getWidth(), h);
    // Left, then right: where it was
    ASSERT_EQ(turn({0}, Turn::Right).pages, 1u);
    EXPECT_NEAR(stroke->getPointVector()[2].x, 160.25, 1e-9);
    EXPECT_NEAR(stroke->getPointVector()[2].y, 270.125, 1e-9);
    EXPECT_EQ(page(0)->getWidth(), w);
}

// Texts, images and TeX turn with their transformation; Markdown boxes and sticky notes stay upright, their middle
// where the page takes it, with what is on a note
TEST_F(PageRotateTest, textsTurnBoxesAndNotesStayUpright) {
    const double w = page(0)->getWidth(), h = page(0)->getHeight();
    Text* text = nullptr;
    Image* image = nullptr;
    Text* box = nullptr;
    Layer* note = nullptr;
    Stroke* onNote = nullptr;
    {
        std::unique_lock lock(*session->getDocument());
        auto t = std::make_unique<Text>();
        t->setText("plain text");
        t->setTransformation(xoj::util::Matrix::TRANSLATION(50, 60));
        text = t.get();
        page(0)->getSelectedLayer()->addElement(std::move(t));
        auto i = std::make_unique<Image>();
        i->setTransformation(xoj::util::Matrix{2, 0, 0, 2, {300, 400}});
        image = i.get();
        page(0)->getSelectedLayer()->addElement(std::move(i));
        // A Markdown box (not the page's text: away from the margins)
        auto md = new Layer();
        md->setName(std::string(xoj::markdown::LAYER_NAME));
        auto b = std::make_unique<Text>();
        b->setText("**box**");
        b->setWrap(120);
        b->setTransformation(xoj::util::Matrix::TRANSLATION(200, 500));
        box = b.get();
        md->addElement(std::move(b));
        page(0)->getLayers().push_back(md);
        note = sticky::makeNote({{100, 600, 200, 140}, Color(0xfff5e28aU), false});
        auto ink = std::make_unique<Stroke>();
        ink->setWidth(1);
        ink->addPoint(Point(110, 610));
        ink->addPoint(Point(130, 620));
        onNote = ink.get();
        note->addElement(std::move(ink));
        page(0)->getLayers().push_back(note);
    }
    ASSERT_TRUE(box->isMarkdown());
    const auto boxBefore = md::boxRect(*box);
    const xoj::util::Matrix textBefore = text->getTransformation();

    ASSERT_EQ(turn({0}, Turn::Right).pages, 1u);
    // A plain text turns: its origin goes to (h - 60, 50) and it reads downwards
    const auto& m = text->getTransformation();
    EXPECT_EQ(m.xx, 0);
    EXPECT_EQ(m.yx, 1);
    EXPECT_EQ(m.xy, -1);
    EXPECT_EQ(m.yy, 0);
    EXPECT_DOUBLE_EQ(m.shift.x, h - 60);
    EXPECT_DOUBLE_EQ(m.shift.y, 50);
    const auto& im = image->getTransformation();
    EXPECT_EQ(im.xx, 0);
    EXPECT_EQ(im.yx, 2) << "its scale stays";
    EXPECT_DOUBLE_EQ(im.shift.x, h - 400);
    EXPECT_DOUBLE_EQ(im.shift.y, 300);
    // The box: upright, the same size, its middle turned
    const auto boxAfter = md::boxRect(*box);
    EXPECT_EQ(box->getTransformation().xx, 1);
    EXPECT_EQ(box->getTransformation().xy, 0);
    EXPECT_NEAR(boxAfter.width, boxBefore.width, 1e-9);
    EXPECT_NEAR(boxAfter.height, boxBefore.height, 1e-9);
    EXPECT_NEAR(boxAfter.x + boxAfter.width / 2, h - (boxBefore.y + boxBefore.height / 2), 1e-9);
    EXPECT_NEAR(boxAfter.y + boxAfter.height / 2, boxBefore.x + boxBefore.width / 2, 1e-9);
    // The note: upright, its middle turned, its ink carried along (not turned)
    const auto look = sticky::lookOf(*note);
    ASSERT_TRUE(look);
    EXPECT_NEAR(look->rect.width, 200, 1e-9);
    EXPECT_NEAR(look->rect.height, 140, 1e-9);
    EXPECT_NEAR(look->rect.x + 100, h - 670, 1e-9);
    EXPECT_NEAR(look->rect.y + 70, 200, 1e-9);
    const double dx = look->rect.x - 100, dy = look->rect.y - 600;
    EXPECT_NEAR(onNote->getPointVector()[0].x, 110 + dx, 1e-9);
    EXPECT_NEAR(onNote->getPointVector()[1].y, 620 + dy, 1e-9);

    undo()->undo();
    EXPECT_EQ(text->getTransformation(), textBefore) << "exactly";
    EXPECT_EQ(image->getTransformation(), (xoj::util::Matrix{2, 0, 0, 2, {300, 400}}));
    EXPECT_EQ(box->getTransformation(), xoj::util::Matrix::TRANSLATION(200, 500));
    EXPECT_EQ(sticky::lookOf(*note)->rect.x, 100);
    EXPECT_EQ(sticky::lookOf(*note)->rect.y, 600);
    EXPECT_EQ(onNote->getPointVector()[0].x, 110);
    EXPECT_EQ(page(0)->getWidth(), w);
}

// The page's own Markdown text stays at the margins and flows anew on the turned page; undo brings it back
TEST_F(PageRotateTest, thePageTextFlowsAnewAtTheMargins) {
    std::string text;
    for (int i = 0; i < 6; ++i) {
        text += "Paragraph " + std::to_string(i) + " with a few more words to fill the line.\n\n";
    }
    {
        MarkdownSession edit(*session);
        edit.begin(0, md::Style{});
        edit.update(text);
        edit.finish();
    }
    session->getUndoRedoHandler()->clearContents();
    const auto pageText = [&] {
        MarkdownSession read(*session);
        std::string t = read.begin(0, md::Style{});
        read.cancel();
        return t;
    };
    const std::string written = pageText();
    const Text* before = TextDocument::pageBoxOf(page(0));
    ASSERT_NE(before, nullptr);
    const double wrapBefore = before->getWrap();
    ASSERT_EQ(turn({0}, Turn::Left).pages, 1u);
    const Text* box = TextDocument::pageBoxOf(page(0));
    ASSERT_NE(box, nullptr);
    const PageMargins::Margins m = PageMargins::of(page(0));
    EXPECT_NEAR(box->getTransformation().shift.x, m.left, 1e-9) << "upright, at the margin";
    EXPECT_NEAR(box->getTransformation().shift.y, m.top, 1e-9);
    EXPECT_EQ(box->getTransformation().xy, 0);
    EXPECT_GT(box->getWrap(), wrapBefore + 100) << "as wide as the landscape page";
    EXPECT_EQ(pageText(), written) << "the same text";
    undo()->undo();
    EXPECT_NEAR(TextDocument::pageBoxOf(page(0))->getWrap(), wrapBefore, 1e-9);
    EXPECT_FALSE(undo()->canUndo()) << "one step";
}

// Space for notes beside a slide turns with it; image backgrounds are turned pictures (the old one comes back on undo)
TEST_F(PageRotateTest, spaceForNotesAndImageBackgroundsTurn) {
    notespace::Amounts a;
    a.right = 200;
    ASSERT_EQ(notespace::apply(*session, {0}, a), 1u);
    const double w = page(0)->getWidth(), h = page(0)->getHeight();
    ASSERT_EQ(turn({0}, Turn::Right).pages, 1u);
    EXPECT_EQ(page(0)->getNoteSpace().bottom, 200) << "the space on the right is below after a turn to the right";
    EXPECT_EQ(page(0)->getNoteSpace().right, 0);
    EXPECT_EQ(page(0)->getWidth(), h);
    EXPECT_EQ(page(0)->getHeight(), w);
    ASSERT_EQ(turn({0}, Turn::Left).pages, 1u);
    ASSERT_EQ(turn({0}, Turn::Left).pages, 1u);
    EXPECT_EQ(page(0)->getNoteSpace().top, 200) << "and above after a turn to the left";

    // An image background: 2 x 1 pixels, red on the left, blue on the right
    cairo_surface_t* png = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 2, 1);
    cairo_t* cr = cairo_create(png);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_rectangle(cr, 0, 0, 1, 1);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 1);
    cairo_rectangle(cr, 1, 0, 1, 1);
    cairo_fill(cr);
    cairo_destroy(cr);
    ASSERT_EQ(cairo_surface_write_to_png(png, path("two.png").string().c_str()), CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(png);
    BackgroundImage img;
    GError* error = nullptr;
    img.loadFile(path("two.png"), &error);
    ASSERT_EQ(error, nullptr);
    auto photo = std::make_shared<XojPage>(200, 100);
    photo->setBackgroundImage(img);
    photo->setBackgroundType(PageType(PageTypeFormat::Image));
    session->insertPages({photo}, 1);
    ASSERT_EQ(turn({1}, Turn::Right).pages, 1u);
    EXPECT_EQ(photo->getWidth(), 100);
    EXPECT_EQ(photo->getHeight(), 200);
    const GdkPixbuf* turned = photo->getBackgroundImage().getPixbuf();
    ASSERT_NE(turned, nullptr);
    ASSERT_EQ(gdk_pixbuf_get_width(turned), 1);
    ASSERT_EQ(gdk_pixbuf_get_height(turned), 2);
    const guchar* px = gdk_pixbuf_read_pixels(turned);
    const int stride = gdk_pixbuf_get_rowstride(turned);
    EXPECT_GT(px[0], 200) << "red at the top: the left side went up";
    EXPECT_GT(px[stride + 2], 200) << "blue below";
    EXPECT_TRUE(photo->getBackgroundImage().isAttached()) << "kept in the document";
    undo()->undo();
    EXPECT_EQ(photo->getBackgroundImage(), img) << "the picture it had";
    EXPECT_EQ(photo->getWidth(), 200);
}

// PdfPages::Kept (not used by the app): a PDF page stays as it is (counted); the other pages turn. Every turned page
// gets a new revision (its thumbnail is drawn again), and again on undo
TEST_F(PageRotateTest, keptPdfPagesStayAndThumbnailsFollow) {
    openPdf({"one", "two"});
    session->insertPages({std::make_shared<XojPage>(A4_W, A4_H)}, 2);
    session->getUndoRedoHandler()->clearContents();
    const auto p = pagerotate::preview(*session, {0, 1, 2}, PdfPages::Kept);
    EXPECT_EQ(p.pages, 1u);
    EXPECT_EQ(p.pdfPages, 2u);
    EXPECT_EQ(pagerotate::preview(*session, {0, 1, 2}, PdfPages::InPdf).pages, 3u);
    std::vector<quint64> revisions;
    for (size_t i = 0; i < 3; ++i) {
        revisions.push_back(session->pageRevision(i));
    }
    ASSERT_EQ(turn({0, 1, 2}, Turn::Right).pages, 1u);
    EXPECT_NEAR(page(0)->getWidth(), 595, 0.5) << "the PDF page stays";
    EXPECT_NEAR(page(2)->getWidth(), A4_H, 1e-9);
    EXPECT_EQ(session->pageRevision(0), revisions[0]);
    EXPECT_NE(session->pageRevision(2), revisions[2]) << "drawn again";
    const quint64 turned = session->pageRevision(2);
    undo()->undo();
    EXPECT_NE(session->pageRevision(2), turned);
    EXPECT_EQ(turn({0, 1}, Turn::Left).pages, 0u) << "only PDF pages: nothing";
    EXPECT_FALSE(undo()->canUndo());
}

// Many pages at once: one quick step
TEST_F(PageRotateTest, allPagesAreOneUndoStep) {
    std::vector<PageRef> pages;
    for (int i = 0; i < 199; ++i) {
        pages.push_back(std::make_shared<XojPage>(A4_W, A4_H));
    }
    session->insertPages(pages, 1);
    Stroke* on7 = addStroke(*session, 7, {Point(10, 20), Point(30, 40)});
    session->getUndoRedoHandler()->clearContents();
    std::vector<size_t> all(pageCount());
    std::vector<double> heights;
    for (size_t i = 0; i < all.size(); ++i) {
        all[i] = i;
        heights.push_back(page(i)->getHeight());
    }
    ASSERT_EQ(turn(all, Turn::Left).pages, 200u);
    for (size_t i = 0; i < 200; ++i) {
        ASSERT_EQ(page(i)->getWidth(), heights[i]) << i;
    }
    EXPECT_DOUBLE_EQ(on7->getPointVector()[0].x, 20);
    undo()->undo();
    EXPECT_FALSE(undo()->canUndo());
    EXPECT_EQ(on7->getPointVector()[0].x, 10);
    EXPECT_NEAR(page(199)->getWidth(), A4_W, 1e-9);
}

// A PDF with notes: the PDF page itself turns (a copy with its /Rotate, in the merged PDF until it is saved). Saved,
// the file's page has /Rotate 90, our ink is drawn where it was on the page, the embedded document has the turned
// size, and the PDF's text is found at its turned place
TEST_F(PageRotateTest, aPdfWithNotesTurnsThePdfPageItself) {
    openPdf({"slideone", "slidetwo"});
    addStroke(*session, 0, {Point(100, 200), Point(300, 200)}, 8, Color(0xffff0000U));
    const fs::path out = path("notes.pdf");
    ASSERT_TRUE(session->saveAsHybrid(out).ok);
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
    ASSERT_TRUE(session->isHybrid());
    Stroke* stroke = nullptr;
    for (const Element* e: page(0)->getSelectedLayer()->getElementsView()) {
        stroke = const_cast<Stroke*>(dynamic_cast<const Stroke*>(e));
    }
    ASSERT_NE(stroke, nullptr);
    const std::vector<Point> before = stroke->getPointVector();
    const size_t number = page(0)->getPdfPageNr();

    const auto r = turn({0}, Turn::Right, PdfPages::InPdf);
    ASSERT_EQ(r.pages, 1u) << r.error;
    EXPECT_NE(page(0)->getPdfPageNr(), number) << "a turned copy of the PDF page";
    EXPECT_NEAR(page(0)->getWidth(), 842, 0.5);
    EXPECT_NEAR(page(0)->getHeight(), 595, 0.5);
    EXPECT_DOUBLE_EQ(stroke->getPointVector()[0].x, page(0)->getWidth() - 200);
    session->waitForMerges();
    XojPdfPageSPtr pdfPage = session->getDocument()->getPdfPage(page(0)->getPdfPageNr());
    ASSERT_NE(pdfPage, nullptr);
    EXPECT_NEAR(pdfPage->getWidth(), 842, 0.5) << "the PDF page is turned too";

    // Undo: the page as it was, its PDF page too
    undo()->undo();
    EXPECT_EQ(page(0)->getPdfPageNr(), number);
    EXPECT_NEAR(page(0)->getWidth(), 595, 0.5);
    EXPECT_EQ(stroke->getPointVector()[0].x, before[0].x);
    undo()->redo();
    EXPECT_NEAR(page(0)->getWidth(), 842, 0.5);

    auto saved = session->save();
    ASSERT_TRUE(saved.ok) << saved.error;
    EXPECT_EQ(rotateOf(out, 0), 90);
    EXPECT_EQ(rotateOf(out, 1), 0);
    cairo_surface_t* picture = renderPdf(out, 0);
    ASSERT_EQ(cairo_image_surface_get_width(picture), 842);
    // The stroke from (100, 200) to (300, 200) is now from (642, 100) down to (642, 300)
    EXPECT_TRUE(redAt(picture, 642, 200)) << "our ink where the turned page has it, in any PDF app";
    EXPECT_FALSE(redAt(picture, 200, 200));
    cairo_surface_destroy(picture);
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    const auto hits = pdf.getPage(0)->findText("slideone");
    ASSERT_FALSE(hits.empty());
    EXPECT_GT(hits[0].x1, 842 - 100 - 30) << "the word at (72, 100) went to the right edge";
    EXPECT_LT(hits[0].y1, 72 + 10);

    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document) << again.error;
    const PageRef p0 = again.document->getPage(0);
    EXPECT_NEAR(p0->getWidth(), 842, 0.5) << "the embedded document has the turned size";
    EXPECT_NEAR(p0->getHeight(), 595, 0.5);
    const Stroke* s = nullptr;
    for (const Element* e: p0->getSelectedLayer()->getElementsView()) {
        s = dynamic_cast<const Stroke*>(e);
    }
    ASSERT_NE(s, nullptr);
    EXPECT_NEAR(s->getPointVector()[0].x, 642, 0.5);
    EXPECT_NEAR(s->getPointVector()[0].y, 100, 0.5);
    EXPECT_NEAR(again.document->getPdfPage(p0->getPdfPageNr())->getWidth(), 842, 0.5);
}

// Turned twice in a row (the first copy still on its way into the merged PDF), then all pages back to the left
TEST_F(PageRotateTest, aPdfPageTurnedTwiceInARow) {
    openPdf({"wordone", "wordtwo"});
    ASSERT_TRUE(session->saveAsHybrid(path("notes.pdf")).ok);
    auto r = turn({0}, Turn::Right, PdfPages::InPdf);
    ASSERT_EQ(r.pages, 1u) << r.error;
    r = turn({0}, Turn::Right, PdfPages::InPdf);
    ASSERT_EQ(r.pages, 1u) << r.error;
    EXPECT_NEAR(page(0)->getWidth(), 595, 0.5);
    session->waitForMerges();
    XojPdfPageSPtr upsideDown = session->getDocument()->getPdfPage(page(0)->getPdfPageNr());
    const auto hits = upsideDown->findText("wordone");
    ASSERT_FALSE(hits.empty());
    EXPECT_GT(hits[0].y1, 842 - 100 - 30) << "upside down: the word at the bottom";
    ASSERT_EQ(turn({0, 1}, Turn::Left, PdfPages::InPdf).pages, 2u);
    EXPECT_NEAR(page(0)->getWidth(), 842, 0.5);
    EXPECT_NEAR(page(1)->getWidth(), 842, 0.5);
    ASSERT_TRUE(session->save().ok);
    EXPECT_EQ(rotateOf(path("notes.pdf"), 0), 90);
    EXPECT_EQ(rotateOf(path("notes.pdf"), 1), 270);
    undo()->undo();
    undo()->undo();
    undo()->undo();
    EXPECT_NEAR(page(0)->getWidth(), 595, 0.5);
    ASSERT_TRUE(session->save().ok);
    EXPECT_EQ(rotateOf(path("notes.pdf"), 0), 0);
    EXPECT_EQ(rotateOf(path("notes.pdf"), 1), 0);
}

// PDF files mode, a PDF annotated before its first save: the user's PDF is not touched by turning (the turned page is
// in the merged PDF in the cache), the document still annotates it, and the first save writes the turn into it
TEST_F(PageRotateTest, aPdfNotSavedYetIsTurnedWhenSavedIntoItself) {
    openPdf({"firstpage", "secondpage"});
    const fs::path pdf = path("slides.pdf");
    const auto bytes = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string original = bytes(pdf);
    const auto r = turn({1}, Turn::Left, PdfPages::InPdf);
    ASSERT_EQ(r.pages, 1u) << r.error;
    session->waitForMerges();
    EXPECT_EQ(bytes(pdf), original) << "the user's PDF stays as it is until it is saved";
    EXPECT_EQ(session->annotatedPdf(), pdf) << "and it is still the PDF the document annotates";
    EXPECT_NEAR(page(1)->getWidth(), 842, 0.5);
    ASSERT_TRUE(session->saveAsHybrid(pdf).ok);
    EXPECT_EQ(rotateOf(pdf, 0), 0);
    EXPECT_EQ(rotateOf(pdf, 1), 270);
    auto again = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_NEAR(again.document->getPage(1)->getWidth(), 842, 0.5);
    EXPECT_NEAR(again.document->getPage(0)->getWidth(), 595, 0.5);
}


// The author's decision for a .xopp: a turned PDF page is a turned copy in the hidden ".name.pages.pdf" next to it,
// which upstream Xournal++ reads too. The PDF the .xopp annotates stays as it is; undo and turning twice work across
// saves; our loader and upstream's LoadHandler open it with the page turned
TEST_F(PageRotateTest, aXoppTurnsItsPdfPageInItsPagesPdf) {
    openPdf({"lectureone", "lecturetwo"});
    const fs::path pdf = path("slides.pdf");
    const auto bytes = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string original = bytes(pdf);
    Stroke* stroke = addStroke(*session, 0, {Point(100, 200), Point(300, 200)}, 8, Color(0xffff0000U));
    const fs::path xopp = path("lecture.xopp");
    ASSERT_TRUE(session->saveAs(xopp).ok);
    session->getUndoRedoHandler()->clearContents();
    const fs::path pagesPdf = MergedPdf::sidecarOf(xopp);
    EXPECT_FALSE(fs::exists(pagesPdf)) << "no pages PDF before";

    // Our loader and upstream's: the page, its size, its PDF page; the user's PDF untouched
    const auto check = [&](double width, int rotate) {
        auto ours = DocumentSession::loadFile(xopp);
        ASSERT_TRUE(ours.document) << ours.error;
        const PageRef p0 = ours.document->getPage(0);
        EXPECT_NEAR(p0->getWidth(), width, 0.5);
        ASSERT_TRUE(p0->getBackgroundType().isPdfPage());
        EXPECT_NEAR(ours.document->getPdfPage(p0->getPdfPageNr())->getWidth(), width, 0.5);
        EXPECT_NEAR(ours.document->getPage(1)->getWidth(), 595, 0.5);
        LoadHandler handler;
        auto upstream = handler.loadDocument(xopp);
        ASSERT_TRUE(upstream) << "upstream's loader";
        EXPECT_TRUE(handler.getMissingPdfFilename().empty());
        const PageRef u0 = upstream->getPage(0);
        EXPECT_NEAR(u0->getWidth(), width, 0.5);
        auto pdfPage = upstream->getPdfPage(u0->getPdfPageNr());
        ASSERT_NE(pdfPage, nullptr);
        EXPECT_NEAR(pdfPage->getWidth(), width, 0.5) << "Xournal++ shows the PDF page turned";
        EXPECT_FALSE(pdfPage->findText("lectureone").empty());
        EXPECT_EQ(rotateOf(upstream->getPdfFilepath(), u0->getPdfPageNr()), rotate);
        const Stroke* s = nullptr;
        for (const Element* e: u0->getSelectedLayer()->getElementsView()) {
            s = dynamic_cast<const Stroke*>(e);
        }
        ASSERT_NE(s, nullptr);
        EXPECT_NEAR(s->getPointVector()[0].x, rotate == 90 ? 642 : rotate == 180 ? 495 : 100, 0.5)
                << "the ink turned with it";
        EXPECT_EQ(bytes(pdf), original) << "the PDF the notes annotate is never changed";
    };

    auto r = turn({0}, Turn::Right, PdfPages::InPdf);
    ASSERT_EQ(r.pages, 1u) << r.error;
    EXPECT_NEAR(page(0)->getWidth(), 842, 0.5);
    EXPECT_DOUBLE_EQ(stroke->getPointVector()[0].x, page(0)->getWidth() - 200);
    ASSERT_TRUE(session->save().ok);
    EXPECT_TRUE(fs::exists(pagesPdf)) << "the hidden pages PDF";
    {
        SCOPED_TRACE("turned right");
        check(842, 90);
    }

    // Undo (after the save may have renumbered the pages PDF), saved: as it was
    undo()->undo();
    EXPECT_NEAR(page(0)->getWidth(), 595, 0.5);
    EXPECT_EQ(stroke->getPointVector()[0].x, 100);
    ASSERT_TRUE(session->save().ok);
    {
        SCOPED_TRACE("undone");
        check(595, 0);
    }

    // Twice in a row: upside down
    ASSERT_EQ(turn({0}, Turn::Right, PdfPages::InPdf).pages, 1u);
    ASSERT_EQ(turn({0}, Turn::Right, PdfPages::InPdf).pages, 1u);
    EXPECT_NEAR(page(0)->getWidth(), 595, 0.5);
    ASSERT_TRUE(session->save().ok);
    {
        SCOPED_TRACE("turned twice");
        check(595, 180);
    }
    undo()->undo();
    undo()->undo();
    ASSERT_TRUE(session->save().ok);
    {
        SCOPED_TRACE("both undone");
        check(595, 0);
    }
}
