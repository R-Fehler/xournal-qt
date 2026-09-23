/*
 * xournal-qt: text search in a document (PDF text and text elements).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <iostream>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

#include "MdBox.h"

#include "config-test.h"

using namespace xqt;

namespace {
class DocumentSearchTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    }
    std::unique_ptr<DocumentSession> open(const char8_t* fixture) {
        auto r = DocumentSession::loadFile(GET_TESTFILE(fixture));
        EXPECT_TRUE(r.document) << r.error;
        return std::make_unique<DocumentSession>(*app, std::move(r.document));
    }
    static void search(DocumentSession& s, const QString& text, bool jump = true) {
        QSignalSpy finished(&s.search(), &DocumentSearch::finished);
        s.search().setQuery(text, jump);
        if (s.search().isRunning()) {
            ASSERT_TRUE(finished.wait(5000));
        }
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

TEST_F(DocumentSearchTest, findsTextElementsOnAllPages) {
    auto s = open(u8"load/pages.xopp");
    search(*s, "p1");  // p1, p10, p11
    const auto& hits = s->search().hits();
    ASSERT_EQ(hits.size(), 3u);
    EXPECT_EQ(hits[0].page, 0u);
    EXPECT_EQ(hits[1].page, 9u);
    EXPECT_EQ(hits[2].page, 10u);
    EXPECT_GT(hits[0].rect.width(), 0);
    EXPECT_EQ(s->search().currentHit(), 0);

    search(*s, "P7");  // case-insensitive
    ASSERT_EQ(s->search().hits().size(), 1u);
    EXPECT_EQ(s->search().hits()[0].page, 6u);
    search(*s, "nothing like this");
    EXPECT_TRUE(s->search().hits().empty());
    EXPECT_EQ(s->search().currentHit(), -1);
    search(*s, "");
    EXPECT_TRUE(s->search().hits().empty());
}

TEST_F(DocumentSearchTest, findsPdfText) {
    auto s = open(u8"packaged_xopp/pdfBackground/old.xopp");
    search(*s, "xournal");
    ASSERT_GE(s->search().hits().size(), 1u);
    EXPECT_EQ(s->search().hits()[0].page, 0u);
    search(*s, "Page 2");
    ASSERT_GE(s->search().hits().size(), 1u);
    EXPECT_EQ(s->search().hits()[0].page, 1u);
}

TEST_F(DocumentSearchTest, currentHitStartsAtTheCurrentPageAndCycles) {
    auto s = open(u8"load/pages.xopp");
    s->setCurrentPageNo(5);
    QSignalSpy scrolls(s.get(), &DocumentSession::scrollToRectRequested);
    search(*s, "p1");
    EXPECT_EQ(s->search().currentHit(), 1) << "first hit from page 6 on: p10";
    EXPECT_EQ(s->getCurrentPageNo(), 9u);
    ASSERT_GE(scrolls.count(), 1);
    EXPECT_EQ(scrolls.last().at(0).toULongLong(), 9u);

    s->search().next();
    EXPECT_EQ(s->search().currentHit(), 2);
    s->search().next();
    EXPECT_EQ(s->search().currentHit(), 0) << "wraps around";
    s->search().previous();
    EXPECT_EQ(s->search().currentHit(), 2);

    // Search without jumping (tab overview), then jump from the current page later.
    search(*s, "p5", false);
    EXPECT_EQ(s->search().currentHit(), -1);
    s->setCurrentPageNo(0);
    s->search().jumpToFirstFromCurrentPage();
    EXPECT_EQ(s->search().currentHit(), 0);
    EXPECT_EQ(s->getCurrentPageNo(), 4u);
}

TEST_F(DocumentSearchTest, editsAreSearchedAgain) {
    auto s = open(u8"load/pages.xopp");
    search(*s, "p1");
    ASSERT_EQ(s->search().hits().size(), 3u);

    // A new text element through the undo machinery (like the text tool).
    auto page = s->getDocument()->getPage(3);
    auto text = std::make_unique<Text>();
    text->setText("p1 again");
    text->move(50, 300);
    const Text* raw = text.get();
    Layer* layer = page->getSelectedLayer();
    s->getDocument()->lock();
    layer->addElement(std::move(text));
    s->getDocument()->unlock();
    QSignalSpy finished(&s->search(), &DocumentSearch::finished);
    s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    ASSERT_TRUE(finished.wait(3000));
    EXPECT_EQ(s->search().hits().size(), 4u);
    EXPECT_EQ(s->search().hits()[1].page, 3u);
}

// XQT_BENCH_PDF=<pdf>: how long searching all its pages takes (poppler reads the text of every page)
TEST_F(DocumentSearchTest, benchSearch) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    auto r = DocumentSession::loadFile(fs::path(pdf.toStdString()));
    ASSERT_TRUE(r.document) << r.error;
    DocumentSession s(*app, std::move(r.document));
    for (const QString& query: {QStringLiteral("the"), QStringLiteral("xyzzy")}) {
        QElapsedTimer t;
        t.start();
        QSignalSpy finished(&s.search(), &DocumentSearch::finished);
        QSignalSpy changed(&s.search(), &DocumentSearch::changed);
        s.search().setQuery(query);
        if (s.search().isRunning()) {
            ASSERT_TRUE(finished.wait(120000));
        }
        std::cout << "\"" << query.toStdString() << "\" in " << s.getDocument()->getPageCount() << " pages: "
                  << t.elapsed() << " ms, " << s.search().hits().size() << " hits, " << changed.count()
                  << " updates of the views\n";
    }
}

// A Markdown box: the hit is where the word is drawn, not where it is in the source
TEST_F(DocumentSearchTest, hitsInMarkdownBoxesAreWhereTheTextIsDrawn) {
    DocumentSession s(*app);
    const PageRef page = s.getDocument()->getPage(0);
    auto* layer = new Layer();
    layer->setName("Markdown");
    auto box = std::make_unique<Text>();
    box->setText("# A heading\n\nthe **needle** in the text");
    box->setWrap(400);
    box->setTransformation(xoj::util::Matrix::TRANSLATION(60, 80));
    const Text* raw = box.get();
    layer->addElement(std::move(box));
    s.getDocument()->lock();
    page->getLayers().insert(page->getLayers().begin(), layer);  // (the page owns it)
    s.getDocument()->unlock();

    search(s, "needle");
    ASSERT_EQ(s.search().hits().size(), 1u);
    const QRectF hit = s.search().hits()[0].rect;
    const auto drawn = md::findText(*raw, "needle");
    ASSERT_EQ(drawn.size(), 1u);
    EXPECT_NEAR(hit.x(), drawn[0].x, 0.01);
    EXPECT_NEAR(hit.y(), drawn[0].y, 0.01);
    const auto inSource = raw->findText("needle");
    ASSERT_EQ(inSource.size(), 1u);
    EXPECT_GT(std::abs(hit.x() - inSource[0].x1), 5) << "\"the \" is drawn before it, not \"the **\"";
    EXPECT_GT(hit.y(), 80 + 20) << "below the heading";
}
