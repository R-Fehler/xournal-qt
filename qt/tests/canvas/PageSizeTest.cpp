/*
 * xournal-qt: the ruling of pages smaller than A5 to scale (the margin line of a lined page, the space above and below
 * the lines: upstream's xoj::view::ruledScale, set by PageMargins), drawn so in the app, the PDF export and the hybrid
 * PDF, and the page's text after that line. Changing the size of existing pages (PageResize.h): one page, several,
 * all; the content stays, PDF pages keep their size, the page's text flows anew, one undo step, saved and loaded.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/ExportHelper.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/PageMargins.h"
#include "session/PageNoteSpace.h"
#include "session/StickyNote.h"
#include "session/TextDocument.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"
#include "view/background/RuledBackgroundView.h"

#include "MarkdownSession.h"
#include "MdBox.h"
#include "MdPaginate.h"
#include "PageResize.h"
#include "TextFlow.h"

using namespace xqt;

namespace {
constexpr double MM = 72.0 / 25.4;
constexpr double A7_W = 74 * MM, A7_H = 105 * MM;
constexpr double A4_W = 210 * MM, A4_H = 297 * MM;
/// Pixels per point of the pictures looked at
constexpr int SCALE = 4;

PageRef linedPage(double w, double h, const std::string& config = "") {
    auto page = std::make_shared<XojPage>(w, h);
    PageType lined(PageTypeFormat::Lined);
    lined.config = config;
    page->setBackgroundType(lined);
    page->setBackgroundColor(Colors::white);
    return page;
}

cairo_surface_t* surfaceFor(double w, double h) {
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(w * SCALE)),
                                      static_cast<int>(std::ceil(h * SCALE)));
}

/// The page as the app draws it (DocumentView), SCALE pixels per point
cairo_surface_t* draw(const PageRef& page) {
    cairo_surface_t* s = surfaceFor(page->getWidth(), page->getHeight());
    cairo_t* cr = cairo_create(s);
    cairo_scale(cr, SCALE, SCALE);
    DocumentView view;
    view.drawPage(page, cr, false, xoj::view::BACKGROUND_SHOW_ALL);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

/// A page of a PDF file as poppler draws it, white behind, SCALE pixels per point
cairo_surface_t* drawPdf(const fs::path& pdf, size_t page) {
    XojPdfDocument doc;
    EXPECT_TRUE(doc.load(pdf, "", nullptr)) << pdf;
    XojPdfPageSPtr p = doc.getPage(page);
    cairo_surface_t* s = surfaceFor(p->getWidth(), p->getHeight());
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, SCALE, SCALE);
    p->render(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

struct Pixel {
    int r, g, b;
};
Pixel at(cairo_surface_t* s, int x, int y) {
    const unsigned char* d = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + 4 * x;
    return {d[2], d[1], d[0]};
}
bool pink(Pixel p) { return p.r > 200 && p.g < 170 && p.b > 60 && p.b < 220 && p.r - p.g > 60; }  // (deep pink)
bool blue(Pixel p) { return p.b > 200 && p.r < 200 && p.b - p.r > 40; }                        // (dodger blue)

/// The margin line: the middle of the pink pixels of the row at `yPt` (points; -1: none)
double marginLineAt(cairo_surface_t* s, double yPt) {
    const int y = static_cast<int>(yPt * SCALE);
    int first = -1, last = -1;
    for (int x = 0; x < cairo_image_surface_get_width(s); ++x) {
        if (pink(at(s, x, y))) {
            first = first < 0 ? x : first;
            last = x;
        }
    }
    return first < 0 ? -1 : (first + last + 1) / 2.0 / SCALE;
}

/// The first horizontal line from the top, in the column at `xPt` (points; -1: none)
double firstLineAt(cairo_surface_t* s, double xPt) {
    const int x = static_cast<int>(xPt * SCALE);
    for (int y = 0; y < cairo_image_surface_get_height(s); ++y) {
        if (blue(at(s, x, y))) {
            int end = y;
            while (end + 1 < cairo_image_surface_get_height(s) && blue(at(s, x, end + 1))) {
                ++end;
            }
            return (y + end + 1) / 2.0 / SCALE;
        }
    }
    return -1;
}
/// The last horizontal line (from the bottom)
double lastLineAt(cairo_surface_t* s, double xPt) {
    const int x = static_cast<int>(xPt * SCALE);
    for (int y = cairo_image_surface_get_height(s) - 1; y >= 0; --y) {
        if (blue(at(s, x, y))) {
            return y / static_cast<double>(SCALE);
        }
    }
    return -1;
}

bool samePixels(cairo_surface_t* a, cairo_surface_t* b) {
    const size_t bytes = static_cast<size_t>(cairo_image_surface_get_stride(a) * cairo_image_surface_get_height(a));
    return cairo_image_surface_get_stride(a) == cairo_image_surface_get_stride(b) &&
           cairo_image_surface_get_height(a) == cairo_image_surface_get_height(b) &&
           std::memcmp(cairo_image_surface_get_data(a), cairo_image_surface_get_data(b), bytes) == 0;
}

/// Upstream's ruling (no scale) while it lives
struct UpstreamRuling {
    UpstreamRuling(): saved(xoj::view::ruledScale.exchange(nullptr)) {}
    ~UpstreamRuling() { xoj::view::ruledScale.store(saved); }
    xoj::view::RuledScale saved;
};

Stroke* addStroke(DocumentSession& s, size_t page, std::vector<Point> points) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setToolType(StrokeTool::PEN);
    stroke->setColor(Color(0xffcc0000U));
    stroke->setWidth(2);
    for (const auto& p: points) {
        stroke->addPoint(p);
    }
    Stroke* raw = stroke.get();
    std::unique_lock lock(*s.getDocument());
    s.getDocument()->getPage(page)->getSelectedLayer()->addElement(std::move(stroke));
    return raw;
}

/// A PDF of `count` A4 pages
void makePdf(const fs::path& p, int count) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), A4_W, A4_H);
    cairo_t* cr = cairo_create(s);
    for (int i = 0; i < count; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, "slide");
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

class PageSizeTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    PageRef page(size_t i) const { return session->getDocument()->getPage(i); }
    size_t pageCount() const { return session->getDocument()->getPageCount(); }
    /// A document of two PDF pages (A4) and then `lined` lined A4 pages
    void pdfThenLined(size_t lined) {
        makePdf(path("slides.pdf"), 2);
        auto r = DocumentSession::loadFile(path("slides.pdf"));
        ASSERT_TRUE(r.document) << r.error;
        session = std::make_unique<DocumentSession>(*app, std::move(r.document));
        std::vector<PageRef> pages;
        for (size_t i = 0; i < lined; ++i) {
            pages.push_back(linedPage(A4_W, A4_H));
        }
        session->insertPages(pages, 2);
    }
    bool sizeIs(size_t i, double w, double h) const {
        return std::abs(page(i)->getWidth() - w) < 0.01 && std::abs(page(i)->getHeight() - h) < 0.01;
    }
    /// The whole page's text that starts on page `i`
    std::string pageTextFrom(size_t i) {
        MarkdownSession read(*session);
        std::string text = read.begin(i, md::Style{});
        read.cancel();
        return text;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
};
}  // namespace

// --- the ruling of small pages --------------------------------------------------------------------------------------

// A4 (and A5, and bigger): exactly upstream's picture, with or without the scale set
TEST_F(PageSizeTest, aLinedA4IsDrawnExactlyAsUpstream) {
    ASSERT_NE(xoj::view::ruledScale.load(), nullptr) << "AppContext sets it";
    EXPECT_EQ(PageMargins::rulingScale(A4_W, A4_H), 1.0);
    EXPECT_EQ(PageMargins::rulingScale(148 * MM, 210 * MM), 1.0) << "A5";
    for (const auto& [w, h]: {std::pair{A4_W, A4_H}, std::pair{148 * MM, 210 * MM}, std::pair{A4_H, A4_W}}) {
        for (PageTypeFormat format: {PageTypeFormat::Lined, PageTypeFormat::Ruled}) {
            const PageRef p = linedPage(w, h);
            p->setBackgroundType(PageType(format));
            cairo_surface_t* ours = draw(p);
            cairo_surface_t* upstream = nullptr;
            {
                UpstreamRuling plain;
                upstream = draw(p);
            }
            EXPECT_TRUE(samePixels(ours, upstream)) << w << " x " << h;
            cairo_surface_destroy(ours);
            cairo_surface_destroy(upstream);
        }
    }
    const PageRef a4 = linedPage(A4_W, A4_H);
    cairo_surface_t* s = draw(a4);
    EXPECT_NEAR(marginLineAt(s, 40), 72, 0.3) << "1 inch";
    EXPECT_NEAR(firstLineAt(s, 200), 80, 0.3);
    cairo_surface_destroy(s);
}

// A7 (74 x 105 mm: half of A5's short side): the margin line at half an inch, the lines from higher up (on upstream's
// grid of lines: 80 - 24) to lower down
TEST_F(PageSizeTest, aLinedA7HasItsMarginLineToScale) {
    EXPECT_NEAR(PageMargins::rulingScale(A7_W, A7_H), 0.5, 1e-9);
    EXPECT_NEAR(PageMargins::rulingScale(A7_H, A7_W), 0.5, 1e-9) << "landscape: by the short side";
    const PageRef card = linedPage(A7_W, A7_H);
    cairo_surface_t* s = draw(card);
    EXPECT_NEAR(marginLineAt(s, 40), 36, 0.3);
    EXPECT_NEAR(firstLineAt(s, 100), 56, 0.3);
    EXPECT_GT(lastLineAt(s, 100), A7_H - 60) << "on to 30 pt above the bottom, not 60";
    EXPECT_LT(lastLineAt(s, 100), A7_H - 30 + 0.5);
    cairo_surface_destroy(s);
    {
        UpstreamRuling plain;  // (what it was: the test sees the difference)
        cairo_surface_t* before = draw(card);
        EXPECT_NEAR(marginLineAt(before, 40), 72, 0.3);
        EXPECT_NEAR(firstLineAt(before, 100), 80, 0.3);
        cairo_surface_destroy(before);
    }
    // A margin set in the page type stays where it is set (on the left or, negative, on the right)
    const PageRef set = linedPage(A7_W, A7_H, "m1=50");
    s = draw(set);
    EXPECT_NEAR(marginLineAt(s, 40), 50, 0.3);
    cairo_surface_destroy(s);
    const PageRef right = linedPage(A7_W, A7_H, "m1=-72");
    s = draw(right);
    EXPECT_NEAR(marginLineAt(s, 40), A7_W - 72, 0.3) << "a margin set in the page type: as set";
    cairo_surface_destroy(s);
}

// The PDF export and the hybrid PDF draw it the same way
TEST_F(PageSizeTest, thePdfExportAndTheHybridPdfDrawTheScaledLine) {
    page(0)->setSize(A7_W, A7_H);
    PageType lined(PageTypeFormat::Lined);
    page(0)->setBackgroundType(lined);
    session->insertPages({linedPage(A4_W, A4_H)}, 1);

    const fs::path out = path("cards.pdf");
    ExportHelper::exportPdf(session->getDocument(), out, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    const fs::path hybrid = path("cards.notes.pdf");
    const auto r = session->saveAsHybrid(hybrid);
    ASSERT_TRUE(r.ok) << r.error;
    for (const fs::path& pdf: {out, hybrid}) {
        cairo_surface_t* card = drawPdf(pdf, 0);
        EXPECT_NEAR(marginLineAt(card, 40), 36, 0.4) << pdf;
        EXPECT_NEAR(firstLineAt(card, 100), 56, 0.4) << pdf;
        cairo_surface_destroy(card);
        cairo_surface_t* a4 = drawPdf(pdf, 1);
        EXPECT_NEAR(marginLineAt(a4, 40), 72, 0.4) << pdf;
        EXPECT_NEAR(firstLineAt(a4, 200), 80, 0.4) << pdf;
        cairo_surface_destroy(a4);
    }
}

// The page's text on a lined A7 card starts after its line (at half an inch), not after upstream's at 1 inch; text
// written before, after the line at 1 inch, is still the page's text and moves when it is edited
TEST_F(PageSizeTest, thePageTextOfALinedA7StartsAfterTheScaledLine) {
    const PageRef card = page(0);
    card->setSize(A7_W, A7_H);
    card->setBackgroundType(PageType(PageTypeFormat::Lined));
    const PageMargins::Margins m = PageMargins::of(card);
    EXPECT_NEAR(m.left, 36 + 10, 1e-9);
    EXPECT_NEAR(m.top, PageMargins::forSize(A7_W, A7_H), 1e-9);
    EXPECT_NEAR(PageMargins::of(linedPage(A4_W, A4_H)).left, 72 + 10, 1e-9) << "A4 as before";

    MarkdownSession edit(*session);
    edit.begin(0, md::Style{});
    edit.update("**Mitochondrion**\n\nthe powerhouse of the cell");
    edit.finish();
    const Text* box = TextDocument::pageBoxOf(card);
    ASSERT_NE(box, nullptr);
    EXPECT_NEAR(box->getTransformation().shift.x, 46, 1e-9) << "after the line at 36";
    EXPECT_NEAR(box->getWrap(), A7_W - 46 - m.right, 1e-9);

    // Older text: after the line at 1 inch
    auto older = std::make_unique<DocumentSession>(*app);
    const PageRef old = older->getDocument()->getPage(0);
    old->setSize(A7_W, A7_H);
    old->setBackgroundType(PageType(PageTypeFormat::Lined));
    auto* layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    old->getLayers().insert(old->getLayers().begin(), layer);
    auto t = std::make_unique<Text>();
    t->setText("older text");
    t->setWrap(A7_W - 82 - m.right);
    t->setTransformation(xoj::util::Matrix::TRANSLATION(82, m.top));
    const Text* oldBox = t.get();
    layer->addElement(std::move(t));
    EXPECT_EQ(TextDocument::pageBoxOf(old), oldBox) << "still the page's text";
    MarkdownSession edit2(*older);
    EXPECT_EQ(edit2.begin(0, md::Style{}), "older text");
    edit2.update("older text, edited");
    edit2.finish();
    const Text* moved = TextDocument::pageBoxOf(old);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->getText(), "older text, edited");
    EXPECT_NEAR(moved->getTransformation().shift.x, 46, 1e-9);
}

// --- changing the size of pages --------------------------------------------------------------------------------------

// One page, the selection, all pages: the new size, the content where it was, PDF pages left out, one step each
TEST_F(PageSizeTest, onePageTheSelectionAndAllPagesInOneUndoStepEach) {
    pdfThenLined(3);
    ASSERT_EQ(pageCount(), 5u);
    Stroke* stroke = addStroke(*session, 2, {Point(20, 30), Point(60, 70)});
    UndoRedoHandler* undo = session->getUndoRedoHandler();
    undo->clearContents();  // (the pages inserted)

    // This page
    EXPECT_EQ(pagesize::apply(*session, {2}, A7_W, A7_H), 1u);
    EXPECT_TRUE(sizeIs(2, A7_W, A7_H));
    EXPECT_TRUE(sizeIs(3, A4_W, A4_H)) << "only this page";
    EXPECT_EQ(stroke->getPointVector()[0].x, 20) << "the content stays where it is";
    EXPECT_EQ(stroke->getPointVector()[1].y, 70);
    EXPECT_EQ(pagesize::apply(*session, {2}, A7_W, A7_H), 0u) << "the same size: nothing to do";
    undo->undo();
    EXPECT_TRUE(sizeIs(2, A4_W, A4_H));
    EXPECT_FALSE(undo->canUndo()) << "one step";
    undo->redo();
    EXPECT_TRUE(sizeIs(2, A7_W, A7_H));
    undo->undo();

    // Selected pages, a PDF page among them: it keeps its size
    const pagesize::Preview p = pagesize::preview(*session->getDocument(), {1, 3, 4}, 105 * MM, 148 * MM);
    EXPECT_EQ(p.pages, 2u);
    EXPECT_EQ(p.pdfPages, 1u);
    EXPECT_EQ(pagesize::apply(*session, {1, 3, 4}, 105 * MM, 148 * MM), 2u);
    EXPECT_TRUE(sizeIs(1, A4_W, A4_H)) << "a PDF page keeps the PDF's size";
    EXPECT_TRUE(sizeIs(3, 105 * MM, 148 * MM));
    EXPECT_TRUE(sizeIs(4, 105 * MM, 148 * MM));
    EXPECT_TRUE(sizeIs(2, A4_W, A4_H));
    undo->undo();
    EXPECT_TRUE(sizeIs(3, A4_W, A4_H));
    EXPECT_TRUE(sizeIs(4, A4_W, A4_H));

    // All pages, landscape A5
    const std::vector<size_t> all{0, 1, 2, 3, 4};
    EXPECT_EQ(pagesize::preview(*session->getDocument(), all, 210 * MM, 148 * MM).pdfPages, 2u);
    EXPECT_EQ(pagesize::apply(*session, all, 210 * MM, 148 * MM), 3u);
    for (size_t i: {2, 3, 4}) {
        EXPECT_TRUE(sizeIs(i, 210 * MM, 148 * MM)) << i;
    }
    EXPECT_TRUE(sizeIs(0, A4_W, A4_H));
    EXPECT_TRUE(page(0)->getBackgroundType().isPdfPage());
    EXPECT_EQ(page(1)->getPdfPageNr(), 1u);
    undo->undo();
    for (size_t i: {2, 3, 4}) {
        EXPECT_TRUE(sizeIs(i, A4_W, A4_H)) << "all in one step: " << i;
    }
    EXPECT_FALSE(undo->canUndo());
}

// A smaller page: what it no longer holds stays, beyond its edge, and the dialog is told how many elements that is
TEST_F(PageSizeTest, contentBeyondASmallerPageIsKeptAndCounted) {
    Stroke* inside = addStroke(*session, 0, {Point(20, 30), Point(60, 70)});
    Stroke* right = addStroke(*session, 0, {Point(150, 40), Point(400, 50)});  // (reaches past A7's 210 pt)
    Stroke* below = addStroke(*session, 0, {Point(40, 500), Point(60, 520)});
    auto text = std::make_unique<Text>();
    text->setText("far down");
    text->setTransformation(xoj::util::Matrix::TRANSLATION(30, 700));
    {
        std::unique_lock lock(*session->getDocument());
        page(0)->getSelectedLayer()->addElement(std::move(text));
        page(0)->getLayers().push_back(sticky::makeNote({{100, 100, 200, 150}, Color(0xfff5e28aU), false}));
        page(0)->getLayers().push_back(sticky::makeNote({{20, 120, 100, 80}, Color(0xfff5e28aU), false}));  // (fits)
    }
    // The page's own Markdown text is not counted: it flows anew
    MarkdownSession edit(*session);
    edit.begin(0, md::Style{});
    edit.update("A text that is written across the whole width of the page, from margin to margin.");
    edit.finish();

    EXPECT_EQ(pagesize::preview(*session->getDocument(), {0}, A7_W, A7_H).outside, 4u)
            << "two strokes, the text, one sticky note";
    EXPECT_EQ(pagesize::preview(*session->getDocument(), {0}, A4_H, A4_W).outside, 1u) << "landscape: the text below";
    EXPECT_EQ(pagesize::preview(*session->getDocument(), {0}, 841 * MM, 1189 * MM).outside, 0u) << "bigger";

    ASSERT_EQ(pagesize::apply(*session, {0}, A7_W, A7_H), 1u);
    EXPECT_EQ(right->getPointVector()[1].x, 400) << "kept, beyond the edge";
    EXPECT_EQ(below->getPointVector()[0].y, 500);
    EXPECT_EQ(inside->getPointVector()[0].x, 20);
    size_t strokes = 0;
    for (const Element* e: page(0)->getSelectedLayer()->getElementsView()) {
        strokes += e->getType() == ELEMENT_STROKE ? 1 : 0;
    }
    EXPECT_EQ(strokes, 3u);
    EXPECT_EQ(pagesize::preview(*session->getDocument(), {0}, A4_W, A4_H).outside, 0u) << "back to A4: all on it";
}

// The page's Markdown text flows anew: onto more cards when they are smaller, onto fewer pages when bigger; the same
// text, at the new margins and width; undo brings the pages and the text back
TEST_F(PageSizeTest, thePagesTextFlowsAnewOnTheNewSize) {
    std::string text;
    for (int i = 0; i < 30; ++i) {
        text += "Paragraph " + std::to_string(i) + " with a few more words to fill the line.\n\n";
    }
    text += "The end.";
    {
        MarkdownSession edit(*session);
        edit.begin(0, md::Style{});
        edit.update(text);
        edit.finish();
    }
    const size_t a4Pages = pageCount();
    ASSERT_GE(a4Pages, 1u);
    const std::string before = pageTextFrom(0);
    UndoRedoHandler* undo = session->getUndoRedoHandler();

    // Smaller: more cards, of the new size, each with its part at the card's margins
    std::vector<size_t> all(pageCount());
    for (size_t i = 0; i < all.size(); ++i) {
        all[i] = i;
    }
    ASSERT_EQ(pagesize::apply(*session, all, A7_W, A7_H), a4Pages);
    const size_t cards = pageCount();
    EXPECT_GT(cards, a4Pages + 3) << "the text needs more A7 cards";
    const PageMargins::Margins m = PageMargins::of(page(0));
    for (size_t i = 0; i < cards; ++i) {
        ASSERT_TRUE(sizeIs(i, A7_W, A7_H)) << i;
        const Text* box = TextDocument::pageBoxOf(page(i));
        ASSERT_NE(box, nullptr) << i;
        EXPECT_NEAR(box->getTransformation().shift.x, m.left, 1e-9) << i;
        EXPECT_NEAR(box->getTransformation().shift.y, m.top, 1e-9) << i;
        EXPECT_NEAR(box->getWrap(), A7_W - m.left - m.right, 1e-9) << i;
        EXPECT_LE(md::boxRect(*box).y + md::boxRect(*box).height, A7_H - m.bottom + 0.5) << i;
    }
    EXPECT_EQ(pageTextFrom(0), before) << "the same text";

    undo->undo();
    EXPECT_EQ(pageCount(), a4Pages) << "one step: the cards added go";
    EXPECT_TRUE(sizeIs(0, A4_W, A4_H));
    EXPECT_EQ(pageTextFrom(0), before);
    EXPECT_NEAR(TextDocument::pageBoxOf(page(0))->getWrap(), A4_W - PageMargins::of(page(0)).left - TextFlow::MARGIN,
                1e-5);
    undo->redo();
    EXPECT_EQ(pageCount(), cards);
    EXPECT_EQ(pageTextFrom(0), before);

    // Bigger again (A4, from the cards): fewer pages; the cards that only held the text go
    std::vector<size_t> allCards(pageCount());
    for (size_t i = 0; i < allCards.size(); ++i) {
        allCards[i] = i;
    }
    ASSERT_EQ(pagesize::apply(*session, allCards, A4_W, A4_H), cards);
    EXPECT_EQ(pageCount(), a4Pages);
    EXPECT_EQ(pageTextFrom(0), before);
    EXPECT_NEAR(TextDocument::pageBoxOf(page(0))->getTransformation().shift.y, TextFlow::MARGIN, 1e-9);
    undo->undo();
    EXPECT_EQ(pageCount(), cards) << "the cards come back";
    EXPECT_EQ(pageTextFrom(0), before);
    EXPECT_TRUE(sizeIs(cards - 1, A7_W, A7_H));
}

// Many pages at once: quick, and one step
TEST_F(PageSizeTest, threeHundredPagesAreOneQuickUndoStep) {
    std::vector<PageRef> pages;
    for (int i = 0; i < 299; ++i) {
        pages.push_back(linedPage(A4_W, A4_H));
    }
    session->insertPages(pages, 1);
    ASSERT_EQ(pageCount(), 300u);
    Stroke* on7 = addStroke(*session, 7, {Point(10, 20), Point(30, 40)});
    session->getUndoRedoHandler()->clearContents();  // (the pages inserted)
    std::vector<size_t> all(300);
    for (size_t i = 0; i < all.size(); ++i) {
        all[i] = i;
    }
    QElapsedTimer t;
    t.start();
    ASSERT_EQ(pagesize::apply(*session, all, 148 * MM, 210 * MM), 300u);
    const qint64 ms = t.elapsed();
    std::cout << "[          ] page size of 300 pages: " << ms << " ms" << std::endl;
    EXPECT_LT(ms, 2000);
    EXPECT_EQ(on7->getPointVector()[0].x, 10);
    UndoRedoHandler* undo = session->getUndoRedoHandler();
    undo->undo();
    EXPECT_FALSE(undo->canUndo()) << "one step";
    for (size_t i = 0; i < 300; ++i) {
        ASSERT_TRUE(sizeIs(i, A4_W, A4_H)) << i;
    }
}

// A page without a PDF that had space for notes: the new size is the page as a whole; undo brings the space back
TEST_F(PageSizeTest, aPageWithSpaceForNotesIsTheNewSizeAsAWhole) {
    notespace::Amounts a;
    a.right = 200;
    ASSERT_EQ(notespace::apply(*session, {0}, a), 1u);
    ASSERT_NEAR(page(0)->getWidth(), A4_W + 200, 0.5);
    ASSERT_EQ(pagesize::apply(*session, {0}, A7_W, A7_H), 1u);
    EXPECT_TRUE(sizeIs(0, A7_W, A7_H));
    EXPECT_TRUE(page(0)->getNoteSpace().empty());
    session->getUndoRedoHandler()->undo();
    EXPECT_NEAR(page(0)->getWidth(), A4_W + 200, 0.5);
    EXPECT_EQ(page(0)->getNoteSpace().right, 200);
}

TEST_F(PageSizeTest, theNewSizesAreSavedAndLoaded) {
    pdfThenLined(2);
    ASSERT_EQ(pagesize::apply(*session, {1, 2}, A7_H, A7_W), 1u);  // (the PDF page stays)
    ASSERT_EQ(pagesize::apply(*session, {3}, 841 * MM, 1189 * MM), 1u);
    const fs::path xopp = path("sizes.xopp");
    ASSERT_TRUE(session->saveAs(xopp).ok);
    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document) << loaded.error;
    Document& doc = *loaded.document;
    ASSERT_EQ(doc.getPageCount(), 4u);
    EXPECT_NEAR(doc.getPage(1)->getWidth(), A4_W, 0.01);
    EXPECT_TRUE(doc.getPage(1)->getBackgroundType().isPdfPage());
    EXPECT_NEAR(doc.getPage(2)->getWidth(), A7_H, 0.01);
    EXPECT_NEAR(doc.getPage(2)->getHeight(), A7_W, 0.01);
    EXPECT_EQ(doc.getPage(2)->getBackgroundType().format, PageTypeFormat::Lined);
    EXPECT_NEAR(doc.getPage(3)->getWidth(), 841 * MM, 0.01);
    EXPECT_NEAR(doc.getPage(3)->getHeight(), 1189 * MM, 0.01);
}
