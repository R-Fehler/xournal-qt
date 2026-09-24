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
#include <cmath>
#include <functional>
#include <iostream>
#include <gtest/gtest.h>

#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/PageSketches.h"
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

/// Until nothing is rendered and no plan waits (a render that landed after a plan asks for another one)
void settle(AppController& c) {
    for (int round = 0; round < 10; ++round) {
        c.context().getRenderService()->waitForIdle();
        processEvents(20);
        if (!CanvasMemory::instance().planPending()) {
            return;
        }
        CanvasMemory::instance().planNow();
    }
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

// A render asked for before a plan (the pages first in view when a document opens at another page) may land after
// that plan left its page out: the page must not stay rendered beyond the limit until the reader scrolls again.
TEST_F(CanvasMemoryTest, aPageRenderedAfterThePlanLeftItOutIsGivenUpAgain) {
    AppController c;
    CanvasView* view = openPages(c, 20);
    view->setShown(true);
    CanvasMemory::instance().setLimit(pageBytes(view) * 12);
    CanvasMemory::instance().planNow();
    settle(c);
    ASSERT_GT(view->cacheWindow().first, 1u);
    // (the render the first view of the document asked for, landing late)
    view->getPage(0)->getRaster().ensureRendered(false);
    c.context().getRenderService()->waitForIdle();
    processEvents(20);
    settle(c);
    EXPECT_FALSE(rendered(view, 0)) << "outside the window of the plan";
    for (size_t i = 0; i < view->pageCount(); ++i) {
        const auto [first, last] = view->cacheWindow();
        EXPECT_EQ(rendered(view, i), i >= first && i <= last) << "page " << i + 1;
    }
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

// XQT_BENCH_PDF=<pdf>: how soon the pages in view are sharp once a zoom stops: Ctrl+wheel in, a pinch in (the
// fingers are lifted at the end), Ctrl+wheel out to several pages; a screen of 1100 x 1600 at 2x. The time counts
// from the last zoom step, so after a Ctrl+wheel it includes the 300 ms the renders wait for the zoom to be stable.
// XQT_BENCH_QUIET=1: no page previews and nothing rendered in advance (what the other work costs the visible pages).
TEST_F(CanvasMemoryTest, benchZoomSettle) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    QTemporaryDir dir;
    const QString copy = dir.filePath("bench.pdf");
    ASSERT_TRUE(QFile::copy(pdf, copy));
    const bool quiet = qEnvironmentVariableIsSet("XQT_BENCH_QUIET");
    if (quiet) {
        PageSketches::instance().setDelays(1000000000, 1000000000);
    }
    AppController c;
    ASSERT_TRUE(c.openPath(copy));
    CanvasView* view = c.tabManager().currentView();
    if (quiet) {
        CanvasMemory::instance().setLimit(1);
    }
    view->setDevicePixelRatio(2);
    ViewController& vc = view->getViewController();
    vc.setViewSize(QSizeF(1100, 1600));
    vc.fitWidth();
    view->setShown(true);
    processEvents(400);
    const size_t n = view->pageCount();
    auto sharp = [&](size_t page) {
        const auto info = view->getPage(page)->bufferInfo();
        return info.valid && info.zoom == vc.zoom();
    };
    const QPointF center(550, 800);
    struct Motion {
        const char* name;
        std::function<void()> run;
        qint64 current = 0, all = 0, worst = 0;
        int count = 0;
    };
    std::vector<Motion> motions;
    motions.push_back({"Ctrl+wheel in", [&] {
                           for (int step = 0; step < 6; ++step) {
                               vc.setZoom(vc.zoom() * 1.1, center);
                               processEvents(16);
                           }
                       }});
    motions.push_back({"pinch in", [&] {
                           vc.pinchBegin(center, 200);
                           for (int step = 1; step <= 6; ++step) {
                               vc.pinchUpdate(center, 200 * std::pow(1.1, step));
                               processEvents(16);
                           }
                           vc.pinchEnd();
                       }});
    motions.push_back({"Ctrl+wheel out", [&] {
                           for (int step = 0; step < 6; ++step) {
                               vc.setZoom(vc.zoom() * 0.8, center);
                               processEvents(16);
                           }
                       }});
    for (size_t target: {size_t(10), n / 3, n / 2, 2 * n / 3, size_t(40)}) {
        for (Motion& motion: motions) {
            vc.fitWidth();
            vc.scrollToPage(target);
            processEvents(1500);  // (reading a moment: pages are rendered in advance, previews drawn meanwhile)
            motion.run();
            QElapsedTimer t;
            t.start();
            const size_t current = c.tabManager().currentSession()->getCurrentPageNo();
            qint64 currentMs = -1;
            auto allSharp = [&] {
                const auto [first, last] = view->visiblePages();
                for (size_t i = first; i <= last; ++i) {
                    if (!sharp(i)) {
                        return false;
                    }
                }
                return true;
            };
            while (!allSharp() && t.elapsed() < 20000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
                if (currentMs < 0 && sharp(current)) {
                    currentMs = t.elapsed();
                }
            }
            const qint64 allMs = t.elapsed();
            if (currentMs < 0) {
                currentMs = allMs;
            }
            const auto [first, last] = view->visiblePages();
            std::cout << "  page " << target + 1 << " " << motion.name << " (" << last - first + 1
                      << " in view): current page sharp after " << currentMs << " ms, all after " << allMs << " ms\n";
            motion.current += currentMs;
            motion.all += allMs;
            motion.worst = std::max(motion.worst, allMs);
            ++motion.count;
        }
    }
    for (const Motion& m: motions) {
        std::cout << m.name << ": current page sharp after " << m.current / m.count << " ms on average, all pages "
                  << "in view after " << m.all / m.count << " ms, worst " << m.worst << " ms\n";
    }
    view->setShown(false);
}

// Reference mode: two documents are in sight at once. The one beside the notes keeps its pages before a document in
// the background does, even when that one was used after it.
TEST_F(CanvasMemoryTest, aDocumentInSightKeepsItsPagesBeforeOneInTheBackground) {
    AppController c;
    CanvasView* reference = openPages(c, 20);
    CanvasView* background = openPages(c, 20);
    CanvasView* notes = openPages(c, 20);
    const qint64 page = pageBytes(notes);
    CanvasMemory::instance().setLimit(page * 30);  // pages: 27; the one in use: 18
    // Shown in a canvas item: it sets the view's size, which looks at the visible pages
    auto show = [](CanvasView* v) {
        v->setShown(true);
        v->getViewController().setViewSize(QSizeF(800, 1000));
        processEvents(20);
    };
    show(reference);  // (beside the notes from now on)
    CanvasMemory::instance().planNow();
    settle(c);
    show(background);  // (a tab looked at meanwhile)
    CanvasMemory::instance().planNow();
    settle(c);
    background->setShown(false);
    const qint64 referenceHeld = reference->bufferBytes();
    ASSERT_GT(referenceHeld, 4 * page);
    show(notes);
    CanvasMemory::instance().planNow();
    settle(c);
    EXPECT_EQ(reference->bufferBytes(), referenceHeld) << "the document in sight gave up its pages";
    EXPECT_LT(background->bufferBytes(), referenceHeld) << "the one in the background kept more";
    EXPECT_LE(CanvasMemory::instance().bytes(), page * 30);
    // Both documents in sight have their visible pages
    for (CanvasView* v: {notes, reference}) {
        const auto [first, last] = v->visiblePages();
        for (size_t i = first; i <= last; ++i) {
            EXPECT_TRUE(rendered(v, i)) << "page " << i + 1;
        }
    }
    reference->setShown(false);
    notes->setShown(false);
}
