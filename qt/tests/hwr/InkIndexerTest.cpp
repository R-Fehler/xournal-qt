/*
 * xournal-qt: the recognition worker and the indexer of an open document (InkRecognitionService, InkTextIndexer),
 * with the scripted recogniser.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <memory>

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "hwr/FakeRecognizer.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/InkRecognitionService.h"
#include "hwr/InkTextIndexer.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "support/SearchHits.h"
#include "support/TestSupport.h"

using xqt::test::waitFor;

using namespace xqt;
using namespace xqt::hwr;

namespace {
/// A written word: a zigzag stroke of `letters` letters, 10 pt high, at x, y; `shape` makes it differ from others.
std::unique_ptr<Stroke> written(double x, double y, int letters = 4, int shape = 0) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.0);
    for (int i = 0; i <= 2 * letters; ++i) {
        s->addPoint(Point(x + i * 3.0, y + (i % 2 ? 10 : 0) + (i == 1 ? 0.3 * shape : 0)));
    }
    s->getBoundingBox();
    return s;
}

/// `pages` pages with `lines` lines of three words each (each line written a little differently).
std::unique_ptr<Document> notes(size_t pages, int lines) {
    auto doc = std::make_unique<Document>(nullptr);
    for (size_t p = 0; p < pages; ++p) {
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        for (int l = 0; l < lines; ++l) {
            for (int w = 0; w < 3; ++w) {
                page->getSelectedLayer()->addElement(
                        written(50 + 60 * w, 100 + 40 * l, 4, static_cast<int>(p) * lines + l + 1));
            }
        }
        doc->addPage(std::move(page));
    }
    return doc;
}

class InkIndexerTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        InkTextIndexer::setDelay(0);
        fake = std::make_shared<FakeRecognizer>();
        // Word i of every line reads "kalman" (best) or "kalmar"; the others "w<i>"
        fake->setScript([](const LineInput&, size_t word) -> FakeRecognizer::Readings {
            if (word == 1) {
                return {{QStringLiteral("Kalmar"), 0.6f}, {QStringLiteral("Kalman"), 0.4f}};
            }
            return {{QStringLiteral("w%1").arg(word), 1.0f}};
        });
        service = std::make_unique<InkRecognitionService>();
        service->setRecognizer(fake);
        service->setActivityPause(50);
    }
    void TearDown() override { InkTextIndexer::setDelay(InkTextIndexer::DELAY_MS); }
    std::unique_ptr<DocumentSession> open(size_t pages, int lines) {
        return std::make_unique<DocumentSession>(*app, notes(pages, lines));
    }
    int hits(DocumentSession& s, const QString& query) {
        s.search().setQuery(query, false);
        test::waitForCounts(s.search());
        return s.search().hitCount();
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::shared_ptr<FakeRecognizer> fake;
    std::unique_ptr<InkRecognitionService> service;
};
}  // namespace

TEST_F(InkIndexerTest, handwritingOfAnOpenDocumentBecomesSearchable) {
    auto s = open(3, 2);
    InkTextIndexer indexer(*s, *service);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && indexer.pagesRead() == 3; }, 10000));
    EXPECT_EQ(fake->calls(), 6);  // 3 pages x 2 lines
    EXPECT_EQ(s->search().textIndex().inkPages(), 3u);
    EXPECT_EQ(hits(*s, QStringLiteral("kalman")), 6);
    EXPECT_EQ(hits(*s, QStringLiteral("w2")), 6);
    // The words of a page, and its lines for the library
    const auto pages = indexer.pages();
    ASSERT_EQ(pages.size(), 3u);
    EXPECT_TRUE(pages[0].known && pages[0].complete);
    EXPECT_EQ(pages[0].lines.size(), 2u);
    const ink::PageText* ink = s->search().textIndex().inkOf(0);
    ASSERT_NE(ink, nullptr);
    ASSERT_EQ(ink->words.size(), 6u);
    EXPECT_NEAR(ink->words[1].box.left(), 110, 1e-9);
    EXPECT_NEAR(ink->words[4].box.top(), 140, 1e-9);
}

TEST_F(InkIndexerTest, anEditReadsOnlyTheLineItTouched) {
    auto s = open(2, 3);
    InkTextIndexer indexer(*s, *service);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && indexer.pagesRead() == 2; }, 10000));
    ASSERT_EQ(fake->calls(), 6);
    // A word added to the second line of page 1, as the pen does
    auto page = s->getDocument()->getPage(1);
    auto stroke = written(230, 140);
    const Stroke* raw = stroke.get();
    s->getDocument()->lock();
    page->getSelectedLayer()->addElement(std::move(stroke));
    s->getDocument()->unlock();
    s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, page->getSelectedLayer(), raw));
    ASSERT_TRUE(waitFor([&] { return fake->calls() == 7 && indexer.done(); }, 10000));
    EXPECT_EQ(s->search().textIndex().inkOf(1)->words.size(), 10u);
    // Undo: the line as it was is known, nothing is read
    s->getUndoRedoHandler()->undo();
    ASSERT_TRUE(waitFor([&] {
        const ink::PageText* ink = s->search().textIndex().inkOf(1);
        return indexer.done() && ink && ink->words.size() == 9u;
    }, 10000));
    EXPECT_EQ(fake->calls(), 7);
}

TEST_F(InkIndexerTest, thePageInViewComesFirst) {
    auto s = open(5, 1);
    s->setCurrentPageNo(3);
    std::vector<quint64> order;
    fake->setScript([&](const LineInput& line, size_t word) -> FakeRecognizer::Readings {
        if (word == 0) {
            order.push_back(line.hash);  // (the worker's thread; read after it is done)
        }
        return {{QStringLiteral("x"), 1.0f}};
    });
    // Each page's line at another height: another hash
    for (size_t p = 0; p < 5; ++p) {
        auto page = s->getDocument()->getPage(p);
        page->getSelectedLayer()->addElement(written(300, 100, static_cast<int>(p) + 2));
    }
    InkTextIndexer indexer(*s, *service);
    indexer.setFocused(true);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && indexer.pagesRead() == 5; }, 10000));
    ASSERT_EQ(order.size(), 5u);
    const auto lines = indexer.pages();
    EXPECT_EQ(order[0], lines[3].lines[0].hash);
}

TEST_F(InkIndexerTest, withoutAModelPagesWaitAndAreReadWhenItComes) {
    fake->setReady(false, QStringLiteral("no model"));
    auto s = open(2, 1);
    InkTextIndexer indexer(*s, *service);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && indexer.pagesRead() == 2; }, 10000));
    EXPECT_EQ(fake->calls(), 0);
    EXPECT_FALSE(indexer.pages()[0].complete);
    EXPECT_EQ(hits(*s, QStringLiteral("kalman")), 0);
    fake->setReady(true);
    service->setRecognizer(fake);  // (ready now: the clients are told)
    ASSERT_TRUE(waitFor([&] { return fake->calls() == 2 && indexer.done(); }, 10000));
    EXPECT_EQ(hits(*s, QStringLiteral("kalman")), 2);
}

TEST_F(InkIndexerTest, closingADocumentStopsItsWork) {
    fake->setDelay(5000);
    auto s = open(4, 2);
    auto indexer = std::make_unique<InkTextIndexer>(*s, *service);
    ASSERT_TRUE(waitFor([&] { return service->pending() > 0; }, 10000));
    QElapsedTimer t;
    t.start();
    indexer.reset();
    s.reset();
    EXPECT_LT(t.elapsed(), 1000);
    EXPECT_EQ(service->pending(), 0u);
    EXPECT_EQ(fake->calls(), 0);
}

TEST_F(InkIndexerTest, theLineCacheHasALimit) {
    auto s = open(3, 4);
    InkTextIndexer indexer(*s, *service);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && indexer.pagesRead() == 3; }, 10000));
    EXPECT_EQ(service->cachedLines(), 12u);
    const size_t all = service->cacheBytes();
    service->setCacheLimit(all / 2);
    EXPECT_LE(service->cacheBytes(), all / 2);
    EXPECT_LT(service->cachedLines(), 12u);
}

TEST_F(InkIndexerTest, theModelIsUnloadedWhenIdle) {
    service->setUnloadAfter(50);
    auto s = open(1, 1);
    InkTextIndexer indexer(*s, *service);
    ASSERT_TRUE(waitFor([&] { return indexer.done() && fake->calls() == 1; }, 10000));
    ASSERT_TRUE(waitFor([&] { return fake->unloads() == 1; }, 3000));
}

TEST_F(InkIndexerTest, theSettingSwitchesItOnAndOff) {
    QString madeFor;
    hwr::HandwritingSearch::setFactory([&](const QString& dir) {
        madeFor = dir;
        return fake;
    });
    Settings& settings = *app->getSettings();
    EXPECT_FALSE(hwr::HandwritingSearch::enabledIn(settings));  // (off until switched on)
    EXPECT_EQ(hwr::HandwritingSearch::modelDir(settings), hwr::HandwritingSearch::defaultModelDir());
    EXPECT_TRUE(hwr::HandwritingSearch::defaultModelDir().endsWith(QStringLiteral("/xournal-qt/models/trocr-small-hw-int8")));
    qputenv("XQT_HWR_MODEL", "/elsewhere/model");
    EXPECT_EQ(hwr::HandwritingSearch::modelDir(settings), QStringLiteral("/elsewhere/model"));
    qunsetenv("XQT_HWR_MODEL");

    hwr::HandwritingSearch search(*app);
    auto a = open(1, 1);
    auto b = open(1, 2);
    const QObject window;
    search.setSessions(&window, {a.get(), b.get()}, b.get());
    EXPECT_EQ(search.indexerOf(a.get()), nullptr);  // off
    hwr::HandwritingSearch::setEnabledIn(settings, true);
    Q_EMIT app->settingsChanged();
    EXPECT_TRUE(search.enabled());
    EXPECT_EQ(madeFor, hwr::HandwritingSearch::defaultModelDir());
    ASSERT_NE(search.indexerOf(a.get()), nullptr);
    ASSERT_NE(search.indexerOf(b.get()), nullptr);
    ASSERT_TRUE(waitFor([&] { return search.indexerOf(a.get())->done() && search.indexerOf(b.get())->done() &&
                                     a->search().textIndex().inkPages() == 1 && b->search().textIndex().inkPages() == 1; }, 10000));
    EXPECT_EQ(hits(*b, QStringLiteral("kalman")), 2);
    // A document closed: its indexer goes with it
    b.reset();
    search.setSessions(&window, {a.get()}, a.get());
    // Off: the open documents forget their handwriting
    hwr::HandwritingSearch::setEnabledIn(settings, false);
    Q_EMIT app->settingsChanged();
    EXPECT_EQ(search.indexerOf(a.get()), nullptr);
    EXPECT_EQ(a->search().textIndex().inkPages(), 0u);
    EXPECT_EQ(hits(*a, QStringLiteral("kalman")), 0);
    hwr::HandwritingSearch::setFactory({});
}
