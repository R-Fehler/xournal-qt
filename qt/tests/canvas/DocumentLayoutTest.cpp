/*
 * xournal-qt: page layout with columns and paired pages (DocumentLayout), and the view following the settings.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QSignalSpy>
#include <QTemporaryDir>

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "DocumentLayout.h"

using namespace xqt;

namespace {
constexpr double P = DocumentLayout::PADDING;
constexpr double B = DocumentLayout::PADDING_BETWEEN;

/// A document with pages of the given sizes (points).
struct Pages {
    explicit Pages(std::vector<QSizeF> sizes): doc(nullptr) {
        for (const auto& s: sizes) {
            auto p = std::make_shared<XojPage>(s.width(), s.height());
            doc.addPage(p);
            refs.push_back(p);
        }
    }
    DocumentLayout layout(DocumentLayout::Config cfg) {
        DocumentLayout l;
        l.update(doc, refs, cfg);
        return l;
    }
    Document doc;
    std::vector<PageRef> refs;
};
}  // namespace

TEST(DocumentLayout, singleColumnLikeUpstream) {
    Pages pages({{100, 200}, {50, 100}});
    const auto l = pages.layout({});
    EXPECT_EQ(l.columns(), 1u);
    EXPECT_EQ(l.pageRect(0, 2), QRectF(P, P, 200, 400));
    EXPECT_EQ(l.pageRect(1, 2), QRectF(P + 50, P + 400 + B, 100, 200)) << "centered in the column";
    EXPECT_EQ(l.contentSize(2), QSizeF(2 * P + 200, 2 * P + 600 + B));
    EXPECT_DOUBLE_EQ(l.fitWidthZoom(600, 0), 600.0 / (100 + 20)) << "upstream: width / (page width + 20)";
}

TEST(DocumentLayout, columns) {
    Pages pages(std::vector<QSizeF>(7, QSizeF(100, 150)));
    const auto l = pages.layout({3, false, 0});
    EXPECT_EQ(l.columns(), 3u);
    EXPECT_EQ(l.rows(), 3u);
    EXPECT_EQ(l.pageRect(4, 1), QRectF(P + 100 + B, P + 150 + B, 100, 150)) << "row 1, column 1";
    EXPECT_EQ(l.contentSize(1), QSizeF(2 * P + 300 + 2 * B, 2 * P + 450 + 2 * B));
    // The rows visible in a rectangle give a range of pages.
    const auto [first, last] = l.pagesIn(QRectF(0, P + 150 + B + 10, 400, 20), 1);
    EXPECT_EQ(first, 3u);
    EXPECT_EQ(last, 5u);
    EXPECT_EQ(l.pageAt(l.pageRect(5, 1).center(), 1), 5u);
    EXPECT_EQ(l.nearestPage(QPointF(P + 250, P + 2 * (150 + B) + 50), 1), 6u) << "empty cell: the last page";
    EXPECT_DOUBLE_EQ(l.fitWidthZoom(1000, 0), (1000 - 2 * B) / (300 + 20.0));
}

TEST(DocumentLayout, pairedPagesMeetInTheMiddle) {
    // A narrower page on the left of a pair is pushed to the middle.
    Pages pages({{100, 150}, {80, 150}, {100, 150}});
    const auto l = pages.layout({1, true, 0});  // an odd column count becomes 2
    EXPECT_EQ(l.columns(), 2u);
    const QRectF left = l.pageRect(0, 1), right = l.pageRect(1, 1);
    EXPECT_EQ(left.top(), right.top());
    EXPECT_DOUBLE_EQ(right.left() - left.right(), DocumentLayout::PAIR_GAP);
    EXPECT_GT(l.pageRect(2, 1).top(), left.bottom()) << "the next pair in the next row";
}

TEST(DocumentLayout, bookWithASingleCoverPage) {
    Pages pages(std::vector<QSizeF>(4, QSizeF(100, 150)));
    const auto l = pages.layout({2, true, 1});
    EXPECT_EQ(l.rows(), 3u) << "cover, pair 2-3, page 4";
    EXPECT_DOUBLE_EQ(l.pageRect(0, 1).left(), P + 100 + DocumentLayout::PAIR_GAP) << "cover on the right";
    EXPECT_EQ(l.pageRect(1, 1).top(), l.pageRect(2, 1).top());
    EXPECT_EQ(l.nearestPage(QPointF(P + 10, P + 10), 1), 0u) << "empty cell before the cover";
}

// Scrolling sideways: the pages side by side in one row, the view steps through them one by one.
TEST(DocumentLayout, sidewaysInOneRow) {
    Pages pages(std::vector<QSizeF>(5, QSizeF(100, 150)));
    DocumentLayout::Config cfg;
    cfg.horizontal = true;
    const auto l = pages.layout(cfg);
    EXPECT_EQ(l.rows(), 1u);
    EXPECT_EQ(l.columns(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(l.pageRect(i, 2), QRectF(P + i * (200 + B), P, 200, 300)) << "page " << i;
    }
    EXPECT_EQ(l.contentSize(2), QSizeF(2 * P + 1000 + 4 * B, 2 * P + 300));
    EXPECT_EQ(l.groupCount(), 5u);
    EXPECT_EQ(l.groupOf(3), 3u);
    EXPECT_EQ(l.groupRect(3, 2), l.pageRect(3, 2));
    // Only the pages in the rectangle are "in view" (not the whole row)
    const auto [first, last] = l.pagesIn(QRectF(P + 200 + B + 50, 0, 250, 400), 2);
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(last, 2u);
    EXPECT_EQ(l.nearestPage(QPointF(P + 4 * (200 + B) + 10, 50), 2), 4u);
    EXPECT_EQ(l.pageAt(l.pageRect(2, 2).center(), 2), 2u);
    EXPECT_FALSE(l.pageAt(QPointF(P + 200 + B / 2, 50), 2)) << "between two pages";
    EXPECT_DOUBLE_EQ(l.fitHeightZoom(600), (600 - 2 * P) / 150) << "the page fills the height";
    EXPECT_DOUBLE_EQ(l.fitWidthZoom(600, 2), 600 / (100 + 20.0)) << "one page fills the width";
}

// With more rows the pages go down a column first, then on to the next column (upstream's vertical layout with
// fixed rows): the reading order stays along the strip.
TEST(DocumentLayout, sidewaysInRows) {
    Pages pages(std::vector<QSizeF>(7, QSizeF(100, 150)));
    DocumentLayout::Config cfg;
    cfg.horizontal = true;
    cfg.rows = 3;
    const auto l = pages.layout(cfg);
    EXPECT_EQ(l.rows(), 3u);
    EXPECT_EQ(l.columns(), 3u);
    EXPECT_EQ(l.pageRect(4, 1), QRectF(P + 100 + B, P + 150 + B, 100, 150)) << "column 1, row 1";
    EXPECT_EQ(l.pageRect(6, 1).topLeft(), QPointF(P + 2 * (100 + B), P)) << "the last column is begun at the top";
    EXPECT_EQ(l.groupCount(), 3u);
    EXPECT_EQ(l.groupPages(1), (std::pair<size_t, size_t>{3, 5}));
    EXPECT_EQ(l.groupPages(2), (std::pair<size_t, size_t>{6, 6}));
    const auto [first, last] = l.pagesIn(QRectF(P + 100 + B + 10, 0, 50, 50), 1);
    EXPECT_EQ(first, 3u) << "the whole column";
    EXPECT_EQ(last, 5u);
    EXPECT_EQ(l.nearestPage(QPointF(P + 100 + B + 50, P + 2 * (150 + B) + 20), 1), 5u);
    EXPECT_EQ(l.nearestPage(QPointF(P + 2 * (100 + B) + 50, P + 2 * (150 + B) + 20), 1), 6u) << "empty: the last";
    EXPECT_DOUBLE_EQ(l.fitHeightZoom(1000), (1000 - 2 * P - 2 * B) / 450) << "all three rows in the height";

    cfg.rows = 20;
    EXPECT_EQ(pages.layout(cfg).rows(), 7u) << "no more rows than pages";
}

// Paired pages scrolling sideways: the pairs stay side by side (a book opened page by page), the cover alone.
TEST(DocumentLayout, sidewaysBook) {
    Pages pages(std::vector<QSizeF>(5, QSizeF(100, 150)));
    DocumentLayout::Config cfg;
    cfg.horizontal = true;
    cfg.paired = true;
    cfg.pairsOffset = 1;
    const auto l = pages.layout(cfg);
    EXPECT_EQ(l.rows(), 1u);
    EXPECT_EQ(l.columns(), 6u) << "cover | 1 2 | 3 4";
    EXPECT_EQ(l.groupRect(0, 1), l.pageRect(0, 1)) << "the cover alone";
    EXPECT_DOUBLE_EQ(l.pageRect(1, 1).left() - l.pageRect(0, 1).right(), B);
    EXPECT_DOUBLE_EQ(l.pageRect(2, 1).left() - l.pageRect(1, 1).right(), DocumentLayout::PAIR_GAP);
    EXPECT_EQ(l.groupCount(), 3u);
    EXPECT_EQ(l.groupOf(0), 0u);
    EXPECT_EQ(l.groupOf(2), 1u);
    EXPECT_EQ(l.groupPages(1), (std::pair<size_t, size_t>{1, 2}));
    EXPECT_EQ(l.groupRect(1, 1), l.pageRect(1, 1).united(l.pageRect(2, 1)));
    EXPECT_DOUBLE_EQ(l.fitWidthZoom(1000, 3), std::min((1000 - DocumentLayout::PAIR_GAP) / 220,
                                                       (1000 - 2 * P - DocumentLayout::PAIR_GAP - 4) / 200))
            << "the pair fills the width";
}

// Presenting: no margin around the pages, a page can fill the screen
TEST(DocumentLayout, noMargins) {
    Pages pages(std::vector<QSizeF>(3, QSizeF(960, 540)));
    DocumentLayout::Config cfg;
    cfg.horizontal = true;
    cfg.noMargins = true;
    const auto l = pages.layout(cfg);
    EXPECT_EQ(l.padding(), 0.0);
    EXPECT_EQ(l.pageRect(0, 2), QRectF(0, 0, 1920, 1080));
    EXPECT_EQ(l.pageRect(1, 2).left(), 1920 + B);
    EXPECT_DOUBLE_EQ(l.fitHeightZoom(1080), 2.0);
}

TEST(DocumentLayout, viewFollowsTheColumnSettings) {
    QTemporaryDir tmp;
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    DocumentSession session(app);
    session.insertNewPage(1);
    session.insertNewPage(2);
    CanvasView view(session);
    view.getViewController().setViewSize(QSizeF(1200, 800));
    EXPECT_EQ(view.documentLayout().columns(), 1u);

    app.getSettings()->setViewColumns(3);
    Q_EMIT app.settingsChanged();
    EXPECT_EQ(view.documentLayout().columns(), 3u);
    const double z = view.getViewController().zoom();
    EXPECT_EQ(view.documentLayout().pageRect(0, z).top(), view.documentLayout().pageRect(2, z).top()) << "one row";
    EXPECT_LE(view.documentLayout().contentSize(z).width(), 1200.0 + 1) << "fits the width";
}

// "Fit the width" fits the page in view, not the widest page of the document: after a 16:9 slide was pasted into
// an A4 document, the A4 pages still fill the width (the columns stay as wide as upstream makes them).
TEST(DocumentLayout, fitWidthFitsThePageInView) {
    const QSizeF a4(595.27559, 841.88976), slide(960, 540);
    Pages pages({a4, a4, slide, a4});
    const auto l = pages.layout({});
    DocumentLayout layout = l;
    ViewController vc(&layout);
    vc.setViewSize(QSizeF(800, 600));
    const double onA4 = 800 / (a4.width() + 20);  // upstream: width / (page width + 20)
    EXPECT_NEAR(vc.zoom(), onA4, 1e-6) << "opened on an A4 page";
    EXPECT_NEAR(vc.fitWidthZoom(2), 800 / (slide.width() + 20), 1e-6);

    vc.scrollToPage(2);
    vc.fitWidth();
    EXPECT_NEAR(vc.zoom(), 800 / (slide.width() + 20), 1e-6) << "on the slide: the slide's width";
    const QRectF slideRect = layout.pageRect(2, vc.zoom()).translated(vc.contentOrigin());
    EXPECT_GE(slideRect.left(), 0);
    EXPECT_LE(slideRect.right(), 800) << "all of it in view";

    vc.scrollToPage(3);
    vc.fitWidth();
    EXPECT_NEAR(vc.zoom(), onA4, 1e-6) << "back on an A4 page: its width";
    const QRectF a4Rect = layout.pageRect(3, vc.zoom()).translated(vc.contentOrigin());
    EXPECT_NEAR(a4Rect.center().x(), 400, 1) << "in the middle, though the column is as wide as the slide";

    // Two pages side by side: the row in view, with its gap
    DocumentLayout two = pages.layout({2, false, 0});
    ViewController vc2(&two);
    vc2.setViewSize(QSizeF(1200, 600));
    const double rowPts = two.pageRect(1, 1).right() - two.pageRect(0, 1).left() - B;  // (at zoom 1, minus the gap)
    EXPECT_NEAR(vc2.fitWidthZoom(0), std::min((1200 - B) / (rowPts + 20), (1200 - 2 * P - B - 4) / rowPts), 1e-6)
            << "the first row: two A4 pages";
    EXPECT_LT(vc2.fitWidthZoom(2), vc2.fitWidthZoom(0)) << "the second row holds the slide: wider";
}

TEST(PdfLinks, linksAreFoundAtTheirPlace) {
    QTemporaryDir tmp;
    // A small PDF with an external link and a link to page 2 (written by hand: cairo 1.16 cannot write page links).
    const std::string pdfPath = tmp.filePath("links.pdf").toStdString();
    {
        const std::vector<std::string> objects{
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] /Annots [5 0 R 6 0 R] >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 400] >>",
                // (PDF y goes up: page y 50..80 from the top is 320..350)
                "<< /Type /Annot /Subtype /Link /Rect [50 320 150 350] /Border [0 0 0] "
                "/A << /S /URI /URI (https://example.org/doc) >> >>",
                "<< /Type /Annot /Subtype /Link /Rect [50 170 150 200] /Border [0 0 0] /Dest [4 0 R /XYZ 0 400 0] >>"};
        std::string pdf = "%PDF-1.4\n";
        std::vector<size_t> offsets;
        for (size_t i = 0; i < objects.size(); ++i) {
            offsets.push_back(pdf.size());
            pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
        }
        const size_t xref = pdf.size();
        pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
        for (size_t o: offsets) {
            char line[24];
            std::snprintf(line, sizeof(line), "%010zu 00000 n \n", o);
            pdf += line;
        }
        pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
               std::to_string(xref) + "\n%%EOF\n";
        std::ofstream(pdfPath, std::ios::binary) << pdf;
    }
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    auto loaded = DocumentSession::loadFile(pdfPath);
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession session(app, std::move(loaded.document));
    CanvasView view(session);
    view.getViewController().setViewSize(QSizeF(800, 1000));
    auto at = [&](double x, double y) {
        const QRectF r = view.pageViewRect(0);
        return r.topLeft() + QPointF(x, y) * view.getViewController().zoom();
    };
    auto external = view.linkAt(at(100, 65));
    ASSERT_TRUE(external);
    EXPECT_EQ(external->uri, "https://example.org/doc");
    auto internal = view.linkAt(at(100, 215));
    ASSERT_TRUE(internal);
    EXPECT_TRUE(internal->uri.isEmpty());
    EXPECT_EQ(internal->pdfPage, 1);
    EXPECT_EQ(internal->page, 1);
    EXPECT_FALSE(view.linkAt(at(300, 300))) << "no link there";

    QSignalSpy tapped(&view, &CanvasView::linkTapped);
    EXPECT_TRUE(view.tapAt(at(100, 65)));
    EXPECT_EQ(tapped.count(), 1);
}

TEST(Navigation, backAndForwardAfterJumps) {
    QTemporaryDir tmp;
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    DocumentSession session(app);
    for (size_t i = 1; i < 10; ++i) {
        session.insertNewPage(i);
    }
    CanvasView view(session);
    view.getViewController().setViewSize(QSizeF(800, 600));
    view.getViewController().setScrollPosition(QPointF(0, 0));
    const double startY = view.getViewController().scrollPosition().y();
    EXPECT_FALSE(view.canGoBack());

    view.jumpToPage(7);
    EXPECT_EQ(session.getCurrentPageNo(), 7u);
    EXPECT_TRUE(view.canGoBack());
    view.jumpToPage(3);
    ASSERT_TRUE(view.navigateBack());
    EXPECT_EQ(session.getCurrentPageNo(), 7u);
    ASSERT_TRUE(view.navigateBack());
    EXPECT_EQ(session.getCurrentPageNo(), 0u);
    EXPECT_NEAR(view.getViewController().scrollPosition().y(), startY, 1) << "the exact place";
    EXPECT_FALSE(view.canGoBack());
    ASSERT_TRUE(view.navigateForward());
    EXPECT_EQ(session.getCurrentPageNo(), 7u);
    EXPECT_TRUE(view.canGoForward());

    // A new jump drops the forward places; a deleted page is skipped.
    view.jumpToPage(9);
    EXPECT_FALSE(view.canGoForward());
    session.setCurrentPageNo(7);
    session.deletePage();  // page 7 (a place in the history) is gone
    ASSERT_TRUE(view.navigateBack());
    EXPECT_EQ(session.getCurrentPageNo(), 0u) << "skipped the deleted page";
}
