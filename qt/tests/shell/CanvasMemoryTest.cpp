/*
 * xournal-qt: the memory for rendered canvas pages (CanvasMemory): what the documents keep and what the current one
 * renders in advance.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <gtest/gtest.h>

#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"

#include "AppController.h"
#include "CanvasMemory.h"
#include "CanvasPage.h"
#include "CanvasView.h"

using namespace xqt;

namespace {
void processEvents(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

/// A new document of 41 plain pages in a view of 800 x 1000 px, at `page`
CanvasView* openPages(AppController& c, size_t page) {
    c.newDocument();
    c.insertPages(1, 0, -1, false, 40);
    CanvasView* view = c.tabManager().currentView();
    view->getViewController().setViewSize(QSizeF(800, 1000));
    view->getViewController().scrollToPage(page);
    processEvents(20);
    return view;
}

qint64 pageBytes(CanvasView* view) {
    const QSizeF s = view->pageViewRect(0).size();
    return static_cast<qint64>(std::ceil(s.width())) * static_cast<qint64>(std::ceil(s.height())) * 4;
}

void settle(AppController& c) {
    c.context().getRenderService()->waitForIdle();
    processEvents(20);
}

bool rendered(CanvasView* view, size_t page) { return view->getPage(page)->bufferInfo().valid; }

size_t renderedCount(CanvasView* view) {
    size_t n = 0;
    for (size_t i = 0; i < view->pageCount(); ++i) {
        n += rendered(view, i);
    }
    return n;
}

class CanvasMemoryTest: public ::testing::Test {
protected:
    void TearDown() override { CanvasMemory::instance().setLimit(CanvasMemory::defaultLimit()); }
};
}  // namespace

TEST_F(CanvasMemoryTest, theCurrentDocumentRendersAheadMoreThanBehind) {
    AppController c;
    CanvasView* view = openPages(c, 20);
    view->setShown(true);
    CanvasMemory::instance().setLimit(pageBytes(view) * 12);  // the only document: all of it (but for previews)
    CanvasMemory::instance().planNow();
    settle(c);

    const auto [first, last] = view->cacheWindow();
    const auto [visibleFirst, visibleLast] = view->visiblePages();
    const size_t before = visibleFirst - first, after = last - visibleLast;
    EXPECT_GT(after, before) << "reading goes forward";
    EXPECT_NEAR(static_cast<double>(before) / static_cast<double>(before + after), 0.35, 0.12);
    for (size_t i = 0; i < view->pageCount(); ++i) {
        EXPECT_EQ(rendered(view, i), i >= first && i <= last) << "page " << i + 1;
    }
    EXPECT_LE(CanvasMemory::instance().bytes(), pageBytes(view) * 12);
    EXPECT_FALSE(c.context().getRenderService()->hasWork(RenderService::Priority::Preload));
}

TEST_F(CanvasMemoryTest, atTheStartTheRestGoesToThePagesAfter) {
    AppController c;
    CanvasView* view = openPages(c, 0);
    view->setShown(true);
    CanvasMemory::instance().setLimit(pageBytes(view) * 12);
    CanvasMemory::instance().planNow();
    settle(c);
    EXPECT_EQ(view->cacheWindow().first, 0u);
    const auto fit = static_cast<size_t>(CanvasMemory::instance().pagesLimit() / pageBytes(view));  // (10)
    EXPECT_GE(renderedCount(view), fit) << "no pages before: all goes to those after";
}

TEST_F(CanvasMemoryTest, theCurrentDocumentTakes70PercentWhenOthersAreOpen) {
    AppController c;
    CanvasView* other = openPages(c, 0);
    CanvasView* view = openPages(c, 20);
    other->setShown(false);
    view->setShown(true);
    const qint64 limit = pageBytes(view) * 20;
    CanvasMemory::instance().setLimit(limit);
    CanvasMemory::instance().planNow();
    settle(c);
    const qint64 pages = CanvasMemory::instance().pagesLimit();  // (a tenth is for previews)
    EXPECT_LE(view->bufferBytes(), pages * 7 / 10);
    EXPECT_GE(view->bufferBytes(), pages * 7 / 10 - 2 * pageBytes(view));
}

TEST_F(CanvasMemoryTest, theDocumentUsedLongestAgoGivesUpItsPagesFirst) {
    AppController c;
    CanvasView* a = openPages(c, 20);
    CanvasView* b = openPages(c, 20);
    CanvasView* cView = openPages(c, 20);
    const qint64 page = pageBytes(a);
    CanvasMemory::instance().setLimit(page * 30);  // pages: 27; the current one: 18
    for (CanvasView* v: {a, b, cView}) {
        v->setShown(true);  // (as if switched to in turn)
        CanvasMemory::instance().planNow();
        settle(c);
        v->setShown(false);
    }
    EXPECT_EQ(a->bufferBytes(), 0) << "used longest ago";
    EXPECT_GT(b->bufferBytes(), 0) << "keeps what the rest of the limit holds";
    EXPECT_LE(b->bufferBytes(), page * 30 - cView->bufferBytes());
    EXPECT_LE(CanvasMemory::instance().bytes(), page * 30);
}

TEST_F(CanvasMemoryTest, afterZoomingOnlyNearPagesAreRenderedAgainInAdvance) {
    AppController c;
    CanvasView* view = openPages(c, 20);
    view->setShown(true);
    CanvasMemory::instance().setLimit(pageBytes(view) * 100);  // all pages
    CanvasMemory::instance().planNow();
    settle(c);
    ASSERT_EQ(renderedCount(view), view->pageCount());
    std::vector<double> before;
    for (size_t p = 0; p < view->pageCount(); ++p) {
        before.push_back(view->getPage(p)->bufferInfo().zoom);
    }

    view->getViewController().setZoom(view->getViewController().zoom() * 0.9, QPointF(400, 500));
    processEvents(350);  // (renders wait until the zoom is stable)
    CanvasMemory::instance().planNow();
    settle(c);
    const double now = view->getViewController().zoom();
    const auto current = static_cast<std::ptrdiff_t>(c.tabManager().currentSession()->getCurrentPageNo());
    int kept = 0;
    for (size_t p = 0; p < view->pageCount(); ++p) {
        const auto info = view->getPage(p)->bufferInfo();
        ASSERT_TRUE(info.valid) << "page " << p + 1 << " is kept (shown scaled)";
        const auto distance = std::abs(static_cast<std::ptrdiff_t>(p) - current);
        if (distance <= CanvasMemory::NEAR_PAGES) {
            EXPECT_DOUBLE_EQ(info.zoom, now) << "page " << p + 1 << " is near";
        } else if (distance > CanvasMemory::NEAR_PAGES + 3) {
            EXPECT_DOUBLE_EQ(info.zoom, before[p]) << "page " << p + 1 << ": again when it comes near";
            ++kept;
        }
    }
    EXPECT_GT(kept, 10) << "the far pages keep what they have";
}

TEST_F(CanvasMemoryTest, scrollChangesAreCollected) {
    AppController c;
    CanvasView* view = openPages(c, 0);
    const quint64 before = view->visibilityUpdates();
    for (int i = 1; i <= 50; ++i) {  // (a dragged scroll bar sends more moves than there are frames)
        view->getViewController().setScrollPosition(QPointF(0, i * 30));
    }
    const quint64 atOnce = view->visibilityUpdates() - before;
    EXPECT_LE(atOnce, 3u) << "the visible pages are not looked at for every scroll change";
    processEvents(40);
    EXPECT_GT(view->visibilityUpdates() - before, atOnce) << "but where it stopped, they are";
}

TEST_F(CanvasMemoryTest, theLimitIsASetting) {
    AppController c;
    SettingsModel settings(c.context());
    const int mb = settings.get("canvasMemory").toInt();
    EXPECT_EQ(mb, static_cast<int>(CanvasMemory::defaultLimit() / (1024 * 1024))) << "a quarter of the memory";
    settings.set("canvasMemory", 512);
    EXPECT_EQ(CanvasMemory::instance().limit(), qint64(512) * 1024 * 1024);
    settings.set("canvasMemory", 1 << 30);
    EXPECT_LE(CanvasMemory::instance().limit(), CanvasMemory::maxLimit()) << "at most a third";
    settings.set("canvasMemory", mb);
}

// XQT_BENCH_PDF=<pdf>: does rendering in advance delay the page the reader jumps to? (screen of 1280 x 1600 at 2x)
TEST_F(CanvasMemoryTest, benchJumps) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    QTemporaryDir dir;
    const QString copy = dir.filePath("bench.pdf");
    ASSERT_TRUE(QFile::copy(pdf, copy));
    for (bool inAdvance: {false, true}) {
        AppController c;
        ASSERT_TRUE(c.openPath(copy));
        CanvasView* view = c.tabManager().currentView();
        view->setDevicePixelRatio(2);
        view->getViewController().setViewSize(QSizeF(1100, 1600));
        view->getViewController().fitWidth();
        processEvents(400);
        CanvasMemory::instance().setLimit(inAdvance ? CanvasMemory::defaultLimit() : 1);
        view->setShown(true);
        const size_t n = view->pageCount();
        qint64 total = 0, worst = 0;
        int jumps = 0;
        QElapsedTimer all;
        all.start();
        for (size_t target: {size_t(5), n / 4, n / 2, size_t(12), 3 * n / 4, size_t(30), n - 3}) {
            processEvents(inAdvance ? 1500 : 300);  // (reading a moment: in advance, pages are rendered meanwhile)
            const bool had = view->getPage(target)->bufferInfo().valid;
            view->getViewController().scrollToPage(target);
            QElapsedTimer t;
            t.start();
            while (!view->getPage(target)->bufferInfo().valid && t.elapsed() < 10000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            }
            std::cout << "  page " << target + 1 << (had ? " (had a buffer)" : "") << ": " << t.elapsed() << " ms\n";
            total += t.elapsed();
            worst = std::max(worst, t.elapsed());
            ++jumps;
        }
        std::cout << (inAdvance ? "rendering in advance" : "only visible pages") << ": page after a jump in "
                  << total / jumps << " ms on average, worst " << worst << " ms; " << CanvasMemory::instance().bytes() / (1024 * 1024)
                  << " MB kept\n";
        view->setShown(false);
    }
}
