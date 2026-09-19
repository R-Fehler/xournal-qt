/*
 * xournal-qt: page layout with columns and paired pages (DocumentLayout), and the view following the settings.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
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
    EXPECT_DOUBLE_EQ(l.fitWidthZoom(600), 600.0 / (100 + 20)) << "upstream: width / (page width + 20)";
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
    EXPECT_DOUBLE_EQ(l.fitWidthZoom(1000), (1000 - 2 * B) / (300 + 20.0));
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

TEST(DocumentLayout, viewFollowsTheColumnSettings) {
    QTemporaryDir tmp;
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    DocumentSession session(app);
    session.insertNewPage(1);
    session.insertNewPage(2);
    CanvasView view(session);
    view.getViewController().setViewSize(QSizeF(1200, 800));
    EXPECT_EQ(view.getLayout().columns(), 1u);

    app.getSettings()->setViewColumns(3);
    Q_EMIT app.settingsChanged();
    EXPECT_EQ(view.getLayout().columns(), 3u);
    const double z = view.getViewController().zoom();
    EXPECT_EQ(view.getLayout().pageRect(0, z).top(), view.getLayout().pageRect(2, z).top()) << "one row";
    EXPECT_LE(view.getLayout().contentSize(z).width(), 1200.0 + 1) << "fits the width";
}
