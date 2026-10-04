/*
 * xournal-qt: handwriting in the search of a document (DocumentTextIndex::setInk, DocumentSearch, termRects).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "session/InkText.h"

#include "../SearchHits.h"

using namespace xqt;

namespace {
std::shared_ptr<const ink::PageText> handwriting(QPointF at, std::vector<std::pair<const char*, float>> words) {
    auto line = std::make_shared<ink::LineResult>();
    double x = 0;
    for (const auto& [text, p]: words) {
        ink::Word w;
        w.box = QRectF(x, 0, 40, 12);
        w.conf = 0.9f;
        w.text = QString::fromUtf8(text);
        w.candidates.push_back(ink::candidate(w.text, p));
        if (p < 1) {
            w.candidates.push_back(ink::candidate(u"other", 1 - p));
        }
        line->words.push_back(std::move(w));
        x += 50;
    }
    return ink::PageText::assemble({{at, line}});
}

class InkSearchTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
        session->insertNewPage(1);
        session->insertNewPage(2);
    }
    DocumentTextIndex& index() { return session->search().textIndex(); }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
};
}  // namespace

TEST_F(InkSearchTest, handwritingIsCountedAndMarked) {
    DocumentSearch& search = session->search();
    ASSERT_EQ(index().pageCount(), 3u);
    index().setInk(1, handwriting({100, 200}, {{"Kalman", 1.0f}, {"filter", 0.3f}}));
    search.setQuery(QStringLiteral("kalman"), false);
    ASSERT_TRUE(test::waitForCounts(search));
    EXPECT_EQ(search.hitCount(), 1);
    EXPECT_EQ(search.countOn(1), 1);
    auto places = test::placesOn(search, 1);
    ASSERT_EQ(places.size(), 1u);
    EXPECT_EQ(places[0].rect, QRectF(100, 200, 40, 12).adjusted(-2, -2, 2, 2));
    EXPECT_FALSE(places[0].faint);
    // A word the recogniser was unsure of: found, marked lighter
    search.setQuery(QStringLiteral("filter"), false);
    ASSERT_TRUE(test::waitForCounts(search));
    places = test::placesOn(search, 1);
    ASSERT_EQ(places.size(), 1u);
    EXPECT_TRUE(places[0].faint);
    EXPECT_EQ(search.countCorrections(), 0);  // (counted and placed alike)
}

TEST_F(InkSearchTest, newHandwritingUpdatesTheHits) {
    DocumentSearch& search = session->search();
    search.setQuery(QStringLiteral("turbine"), false);
    ASSERT_TRUE(test::waitForCounts(search));
    EXPECT_EQ(search.hitCount(), 0);
    index().setInk(2, handwriting({0, 0}, {{"turbine", 1.0f}, {"turbines", 1.0f}}));
    EXPECT_EQ(search.hitCount(), 2);
    EXPECT_EQ(search.countOn(2), 2);
    // A typo is found in handwriting with the fuzzy search off
    search.setQuery(QStringLiteral("turbnie"), false);
    ASSERT_TRUE(test::waitForCounts(search));
    EXPECT_EQ(search.hitCount(), 1);
    // The fuzzy search: its expression over the page
    search.setQuery(QStringLiteral("tbine !river"), false, true);
    ASSERT_TRUE(test::waitForCounts(search));
    EXPECT_EQ(search.hitCount(), 2);
    EXPECT_TRUE(search.matches(u"name"));
    index().setInk(2, nullptr);
    EXPECT_EQ(search.hitCount(), 0);
    EXPECT_EQ(index().inkPages(), 0u);
}

TEST_F(InkSearchTest, handwritingMovesWithItsPage) {
    index().setInk(2, handwriting({0, 0}, {{"kalman", 1.0f}}));
    EXPECT_GT(index().inkBytes(), 0u);
    session->insertNewPage(0);  // a page before it
    ASSERT_EQ(index().pageCount(), 4u);
    EXPECT_EQ(index().inkOf(2), nullptr);
    ASSERT_NE(index().inkOf(3), nullptr);
    DocumentSearch& search = session->search();
    search.setQuery(QStringLiteral("kalman"), false);
    ASSERT_TRUE(test::waitForCounts(search));
    EXPECT_EQ(search.countOn(3), 1);
}

TEST_F(InkSearchTest, termRectsMarkHandwriting) {
    auto ink = handwriting({10, 20}, {{"dumb", 1.0f}, {"test", 1.0f}});
    Document* doc = session->getDocument();
    std::shared_lock lock(*doc);
    const auto rects = termRects(*doc->getPage(0), nullptr, {{QStringLiteral("dumb test"), textmatch::Anywhere}}, ink.get());
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_EQ(rects[0], QRectF(10, 20, 90, 12).adjusted(-2, -2, 2, 2));
}
