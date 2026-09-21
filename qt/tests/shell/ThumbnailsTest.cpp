/*
 * xournal-qt: page thumbnails are kept (ThumbnailProvider) and named by the page's revision (DocumentSession), so a
 * page is drawn again only when it changed.
 *
 * @license GNU GPLv2 or later
 */
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "shell/PageSketches.h"
#include "shell/PagesModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "config-test.h"

using namespace xqt;

namespace {
void processEvents(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

QString fixture(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}

PagesModel& pagesOf(AppController& c) { return *qobject_cast<PagesModel*>(c.pagesModel()); }

QString urlOf(AppController& c, int page) { return pagesOf(c).thumbnailUrl(page); }

/// What QML gets for this URL, asked `width` pixels wide.
QImage request(const QString& url, int width) {
    ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> r(
            provider.requestImageResponse(url.mid(QString("image://thumbnail/").size()), QSize(width, 0)));
    QSignalSpy done(r.get(), &QQuickImageResponse::finished);
    if (!done.wait(10000)) {
        return {};
    }
    std::unique_ptr<QQuickTextureFactory> f(r->textureFactory());
    return f ? f->image() : QImage();
}

/// Until the sketches of all open documents are there
bool sketched() {
    QElapsedTimer t;
    t.start();
    while (!PageSketches::instance().idle() && t.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return PageSketches::instance().idle();
}

QString sketchOf(AppController& c, int page) {
    return pagesOf(c).data(pagesOf(c).index(page), PagesModel::SketchRole).toString();
}

void openPages(AppController& c) {
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"load/pages.xopp")));
}

void openLecture(AppController& c) {
    c.newDocument();
    ASSERT_TRUE(c.openPath(fixture(u8"packaged_xopp/pdfBackground/old.xopp")));
}
}  // namespace

TEST(Thumbnails, aDrawnPageIsKept) {
    AppController c;
    openLecture(c);
    const QString url = urlOf(c, 0);
    const int before = ThumbnailProvider::renderCount();
    const QImage first = request(url, 300);
    ASSERT_FALSE(first.isNull());
    EXPECT_EQ(first.width(), 320) << "widths are rounded up to steps of 64 px";
    EXPECT_EQ(ThumbnailProvider::renderCount(), before + 1);

    EXPECT_EQ(request(url, 300).width(), 320);
    EXPECT_EQ(request(url + "/7", 300).width(), 320) << "what follows the revision only makes QML ask again";
    EXPECT_EQ(ThumbnailProvider::renderCount(), before + 1) << "asked again: shown as it was kept, not drawn";

    EXPECT_EQ(request(url, 150).width(), 192);
    EXPECT_EQ(ThumbnailProvider::renderCount(), before + 1) << "a smaller one is scaled from the kept one";

    EXPECT_EQ(request(url, 500).width(), 512);
    EXPECT_EQ(ThumbnailProvider::renderCount(), before + 2) << "a bigger one has to be drawn";
}

TEST(Thumbnails, switchingTabsKeepsTheThumbnails) {
    AppController c;
    openLecture(c);
    const QString first = urlOf(c, 0), second = urlOf(c, 1);
    c.newDocument();
    ASSERT_NE(urlOf(c, 0), first);
    c.setCurrentTab(0);
    EXPECT_EQ(urlOf(c, 0), first) << "the kept images of the tab are shown again, not drawn again";
    EXPECT_EQ(urlOf(c, 1), second);
}

TEST(Thumbnails, aChangedPageIsDrawnAgain) {
    AppController c;
    openLecture(c);
    DocumentSession* s = c.tabManager().currentSession();
    pagesOf(c).setRefreshDelay(10);
    const QString url = urlOf(c, 0), other = urlOf(c, 1);
    request(url, 200);
    const int before = ThumbnailProvider::renderCount();

    s->firePageChanged(0);
    processEvents(40);
    const QString changed = urlOf(c, 0);
    EXPECT_NE(changed, url);
    EXPECT_EQ(urlOf(c, 1), other) << "only the changed page";
    ASSERT_FALSE(request(changed, 200).isNull());
    EXPECT_EQ(ThumbnailProvider::renderCount(), before + 1);
}

TEST(Thumbnails, theOverviewSharesThemWithTheSidebar) {
    AppController c;
    c.newDocument();  // (a saved one shows its stored preview on its title page)
    TabManager& tabs = c.tabManager();
    const QString overview = tabs.data(tabs.index(tabs.currentIndex()), TabManager::ThumbnailRole).toString();
    EXPECT_EQ(overview, urlOf(c, 0)) << "the overview names the current page like the sidebar";
}

TEST(Thumbnails, keptWithinTheMemoryLimit) {
    AppController c;
    openLecture(c);
    const qint64 page = request(urlOf(c, 0), 640).sizeInBytes();
    // Three quarters are for these (a quarter for the sketches): room for one page at 640 px, not for two
    const qint64 limit = page * 3 / 2 * 4 / 3;
    ThumbnailProvider::setCacheLimit(limit);
    EXPECT_GT(ThumbnailProvider::cacheBytes(), 0);
    request(urlOf(c, 1), 640);
    EXPECT_LE(ThumbnailProvider::cacheBytes(), limit * 3 / 4);
    EXPECT_GT(ThumbnailProvider::cacheBytes(), 0);

    const int drawn = ThumbnailProvider::renderCount();
    request(urlOf(c, 1), 640);
    EXPECT_EQ(ThumbnailProvider::renderCount(), drawn) << "the one used last is still there";
    request(urlOf(c, 0), 640);
    EXPECT_EQ(ThumbnailProvider::renderCount(), drawn + 1) << "the least recently used one went first";
    ThumbnailProvider::setCacheLimit(ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024);
}

TEST(Thumbnails, closingATabForgetsItsThumbnails) {
    AppController c;
    openLecture(c);
    const qint64 before = ThumbnailProvider::cacheBytes();
    request(urlOf(c, 0), 256);
    ASSERT_GT(ThumbnailProvider::cacheBytes(), before);
    c.closeTab(c.tabManager().currentIndex());
    processEvents(20);
    EXPECT_EQ(ThumbnailProvider::cacheBytes(), before);
}

TEST(Thumbnails, aPageMovesWithItsKeptThumbnail) {
    AppController c;
    openPages(c);
    const QString second = urlOf(c, 1);
    request(second, 256);
    const int drawn = ThumbnailProvider::renderCount();
    c.insertPageBefore(0);
    ASSERT_EQ(urlOf(c, 2).section('/', -1), second.section('/', -1)) << "the page keeps its revision";
    request(urlOf(c, 2), 256);
    EXPECT_EQ(ThumbnailProvider::renderCount(), drawn) << "a page before it came: not drawn again";
}

// --- sketches: all pages drawn small in advance ---

class Sketches: public ::testing::Test {
protected:
    void SetUp() override { PageSketches::instance().setDelays(0, 0); }
    void TearDown() override {
        ThumbnailProvider::setCacheLimit(ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024);
        PageSketches::instance().setDelays(400, 1500);
    }
};

TEST_F(Sketches, allPagesAreSketchedWhenADocumentIsOpened) {
    AppController c;
    openPages(c);
    const int sharp = ThumbnailProvider::renderCount();
    ASSERT_TRUE(sketched());
    const int pages = pagesOf(c).rowCount();
    ASSERT_GT(pages, 5);
    for (int p = 0; p < pages; ++p) {
        const QString url = sketchOf(c, p);
        ASSERT_TRUE(url.startsWith("image://sketch/")) << "page " << p + 1;
        SketchProvider provider;
        QSize size;
        EXPECT_EQ(provider.requestImage(url.mid(15), &size, {}).width(), 128) << "page " << p + 1;
    }
    EXPECT_EQ(ThumbnailProvider::renderCount(), sharp) << "no sharp thumbnail drawn for them";
    EXPECT_LE(PageSketches::instance().bytes(), ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024 / 4);
}

TEST_F(Sketches, smallThumbnailsComeFromTheSketch) {
    AppController c;
    openPages(c);
    ASSERT_TRUE(sketched());
    const int drawn = ThumbnailProvider::renderCount();
    EXPECT_EQ(request(urlOf(c, 3), 100).width(), 128);
    EXPECT_EQ(ThumbnailProvider::renderCount(), drawn) << "not drawn: the sketch is as big";
}

TEST_F(Sketches, aSharpThumbnailGivesThePageItsSketch) {
    PageSketches::instance().setDelays(60000, 60000);  // (no sketching in advance now)
    AppController c;
    openPages(c);
    ASSERT_TRUE(sketchOf(c, 2).isEmpty());
    const int drawn = PageSketches::instance().drawCount();
    request(urlOf(c, 2), 300);
    EXPECT_TRUE(sketchOf(c, 2).startsWith("image://sketch/")) << "scaled from the sharp one";
    EXPECT_EQ(PageSketches::instance().drawCount(), drawn);
}

TEST_F(Sketches, aChangedPageKeepsItsOldSketchUntilItIsSketchedAgain) {
    AppController c;
    openPages(c);
    ASSERT_TRUE(sketched());
    const QString before = sketchOf(c, 1);
    PageSketches::instance().setDelays(0, 60000);  // (the edits did not pause yet)
    c.tabManager().currentSession()->firePageChanged(1);
    processEvents(20);
    EXPECT_EQ(sketchOf(c, 1), before) << "never blank: the old sketch until then";
    PageSketches::instance().setDelays(0, 0);
    c.tabManager().currentSession()->firePageChanged(1);
    ASSERT_TRUE(sketched());
    const QString after = sketchOf(c, 1);
    EXPECT_NE(after, before);
    EXPECT_EQ(after.section('/', -1), urlOf(c, 1).section('/', -1)) << "the page as it is now";
}

TEST_F(Sketches, theyGetSmallerWhenManyPagesAreOpen) {
    AppController c;
    openPages(c);
    const qint64 pages = pagesOf(c).rowCount();
    // Room for all pages at 64 px (11.5 kB), not at 96 (26 kB)
    const qint64 budget = pages * 20000;
    ThumbnailProvider::setCacheLimit(budget * 4);
    ASSERT_TRUE(sketched());
    EXPECT_EQ(PageSketches::instance().width(), 64);
    EXPECT_LE(PageSketches::instance().bytes(), budget);
    for (int p = 0; p < pages; ++p) {
        EXPECT_FALSE(sketchOf(c, p).isEmpty()) << "page " << p + 1;
    }
}

TEST_F(Sketches, theDocumentShownLastComesFirstWhenNotAllFit) {
    AppController c;
    openLecture(c);  // two pages
    openPages(c);    // shown now
    const qint64 pages = pagesOf(c).rowCount();
    ThumbnailProvider::setCacheLimit(pages * 12000 * 4);  // this one at 64 px, no more
    ASSERT_TRUE(sketched());
    EXPECT_EQ(PageSketches::instance().width(), 64);
    for (int p = 0; p < pages; ++p) {
        EXPECT_FALSE(sketchOf(c, p).isEmpty()) << "page " << p + 1;
    }
    c.setCurrentTab(c.tabManager().currentIndex() - 1);  // the lecture
    ASSERT_TRUE(sketched());
    EXPECT_FALSE(sketchOf(c, 0).isEmpty()) << "shown now: it gets its sketches";
    EXPECT_FALSE(sketchOf(c, 1).isEmpty());
    EXPECT_LE(PageSketches::instance().bytes(), pages * 12000);
}

TEST_F(Sketches, closingADocumentForgetsItsSketches) {
    AppController c;
    openPages(c);
    ASSERT_TRUE(sketched());
    ASSERT_GT(PageSketches::instance().bytes(), 0);
    const qint64 before = PageSketches::instance().bytes();
    c.closeTab(c.tabManager().currentIndex());
    processEvents(20);
    EXPECT_LT(PageSketches::instance().bytes(), before);
}

// XQT_BENCH_PDF=<big pdf>: how long sketching all its pages takes, and whether edits (the document lock) or the UI
// have to wait for it.
TEST_F(Sketches, benchBigPdf) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    QTemporaryDir dir;  // (a copy: nothing is written next to the original)
    const QString copy = dir.filePath("bench.pdf");
    ASSERT_TRUE(QFile::copy(pdf, copy));
    AppController c;
    QElapsedTimer t;
    t.start();
    ASSERT_TRUE(c.openPath(copy));
    std::cout << "opened: " << t.elapsed() << " ms, " << pagesOf(c).rowCount() << " pages\n";
    t.restart();
    Document* doc = c.tabManager().currentSession()->getDocument();
    qint64 maxLockWait = 0, maxGap = 0, last = 0, maxCanvasDraw = 0;
    int canvasDraws = 0;
    const int drawn = PageSketches::instance().drawCount();
    while (!PageSketches::instance().idle() && t.elapsed() < 120000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        maxGap = std::max(maxGap, t.elapsed() - last);
        QElapsedTimer l;
        l.start();
        doc->lock();  // like an edit
        doc->unlock();
        maxLockWait = std::max(maxLockWait, l.nsecsElapsed() / 1000);
        // Like the canvas: a PDF page with the document's own instance
        if (++canvasDraws % 20 == 0) {
            QElapsedTimer d;
            d.start();
            ThumbnailProvider::renderDocument(*doc, 0, 64);
            maxCanvasDraw = std::max(maxCanvasDraw, d.elapsed());
        }
        last = t.elapsed();
    }
    std::cout << "sketched " << PageSketches::instance().drawCount() - drawn << " pages at "
              << PageSketches::instance().width() << " px in " << t.elapsed() << " ms; longest wait of an edit "
              << maxLockWait / 1000.0 << " ms; longest UI gap " << maxGap << " ms; "
              << PageSketches::instance().bytes() / 1024 << " kB; longest PDF draw of the canvas meanwhile "
              << maxCanvasDraw << " ms\n";
}
