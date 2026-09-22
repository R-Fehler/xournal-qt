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
#include <QUrl>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/DocumentSession.h"
#include "shell/PageSketches.h"
#include "CanvasMemory.h"
#include "CanvasView.h"
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

// --- previews: bigger pictures of all pages, for the canvas and for thumbnails ---

TEST_F(Sketches, pagesAreDrawnOnceForTheirPreviewAndSketch) {
    AppController c;
    openPages(c);
    const int drawn = PageSketches::instance().drawCount(), read = PageSketches::instance().readCount();
    ASSERT_TRUE(sketched());
    DocumentSession* s = c.tabManager().currentSession();
    const quint64 id = ThumbnailProvider::idOf(s);
    const int pages = pagesOf(c).rowCount();
    for (int p = 0; p < pages; ++p) {
        EXPECT_EQ(PageSketches::instance().preview(id, s->pageId(static_cast<size_t>(p))).width(), 768) << "page " << p + 1;
        EXPECT_EQ(PageSketches::instance().image(id, s->pageId(static_cast<size_t>(p))).width(), 128) << "page " << p + 1;
    }
    EXPECT_EQ(PageSketches::instance().drawCount() - drawn + PageSketches::instance().readCount() - read, pages)
            << "each page drawn (or read) once: the sketch is scaled from the preview";
    EXPECT_FALSE(c.tabManager().currentView()->preview(3).isNull()) << "the canvas shows them until it rendered";
}

TEST_F(Sketches, thumbnailsUpToThePreviewWidthAreNotDrawn) {
    AppController c;
    openPages(c);
    ASSERT_TRUE(sketched());
    const int drawn = ThumbnailProvider::renderCount();
    EXPECT_EQ(request(urlOf(c, 4), 360).width(), 384);
    EXPECT_EQ(request(urlOf(c, 5), 700).width(), 704);
    EXPECT_EQ(ThumbnailProvider::renderCount(), drawn) << "scaled from the previews";
    request(urlOf(c, 5), 1000);
    EXPECT_EQ(ThumbnailProvider::renderCount(), drawn + 1) << "bigger than the preview: drawn";
}

TEST_F(Sketches, previewsGetSmallerWhenTheMemoryForPagesIsShort) {
    AppController c;
    openPages(c);
    const qint64 pages = pagesOf(c).rowCount();
    // A tenth of it for previews: room for all pages at 384 px (~ 417 kB), not at 512 (~ 741 kB)
    CanvasMemory::instance().setLimit(pages * 600000 * 10);
    ASSERT_TRUE(sketched());
    EXPECT_EQ(PageSketches::instance().previewWidth(), 384);
    EXPECT_LE(PageSketches::instance().previewBytes(), pages * 600000);
    CanvasMemory::instance().setLimit(CanvasMemory::defaultLimit());
}

// --- stored previews: the next opening reads them instead of drawing ---

namespace {
size_t storedFiles(const fs::path& folder) {
    size_t n = 0;
    std::error_code ec;
    for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        n += it->path().extension() == ".jpg";
    }
    return n;
}

QByteArray contentOf(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void scribble(DocumentSession& s) {
    auto page = s.getDocument()->getPage(0);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(4);
    stroke->addPoint(Point(10, 10, 1));
    stroke->addPoint(Point(500, 700, 1));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}
}  // namespace

TEST_F(Sketches, previewsAreStoredForTheNextOpening) {
    fs::path folder;
    int pages = 0;
    {
        AppController c;
        openPages(c);
        ASSERT_TRUE(sketched());
        folder = PageSketches::instance().diskFolder(ThumbnailProvider::idOf(c.tabManager().currentSession()));
        pages = pagesOf(c).rowCount();
        ASSERT_FALSE(folder.empty());
        EXPECT_EQ(storedFiles(folder), static_cast<size_t>(pages));
    }
    AppController c;
    const int drawn = PageSketches::instance().drawCount(), read = PageSketches::instance().readCount();
    openPages(c);
    ASSERT_TRUE(sketched());
    EXPECT_EQ(PageSketches::instance().drawCount(), drawn) << "nothing drawn";
    EXPECT_EQ(PageSketches::instance().readCount() - read, pages) << "all read";
    DocumentSession* s = c.tabManager().currentSession();
    EXPECT_EQ(PageSketches::instance().preview(ThumbnailProvider::idOf(s), s->pageId(3)).width(), 768);
}

TEST_F(Sketches, changedPagesAreStoredOnceTheDocumentIsSaved) {
    QTemporaryDir dir;
    AppController c;
    c.newDocument();
    c.insertPages(1, 0, -1, false, 3);
    DocumentSession* s = c.tabManager().currentSession();
    const QString path = dir.filePath("doc.xopp");
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(path)));
    ASSERT_TRUE(sketched());
    const quint64 id = ThumbnailProvider::idOf(s);
    const fs::path saved = PageSketches::instance().diskFolder(id);
    ASSERT_FALSE(saved.empty());
    EXPECT_EQ(storedFiles(saved), 4u);
    const QByteArray before = contentOf(saved / "0.jpg");

    scribble(*s);
    ASSERT_TRUE(sketched());
    EXPECT_EQ(contentOf(saved / "0.jpg"), before) << "changed, not saved: the stored page stays as in the file";

    processEvents(20);  // (a new modification time)
    ASSERT_TRUE(c.save());
    ASSERT_TRUE(sketched());
    const fs::path now = PageSketches::instance().diskFolder(id);
    EXPECT_NE(now, saved) << "the file changed: another folder";
    EXPECT_EQ(storedFiles(now), 4u);
    EXPECT_NE(contentOf(now / "0.jpg"), before) << "the page as saved now";
}

TEST_F(Sketches, theStoredPreviewsUsedLongestAgoGoFirst) {
    const fs::path root = Util::getCacheSubfolder("pages");
    std::error_code ec;
    qint64 others = 0;  // (stored by other tests, newer)
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            others += static_cast<qint64>(it->file_size(ec));
        }
    }
    const auto now = fs::file_time_type::clock::now();
    for (int i = 0; i < 3; ++i) {
        const fs::path folder = root / ("test-" + std::to_string(i));
        fs::create_directories(folder, ec);
        QFile f(QString::fromStdString((folder / "0.jpg").string()));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(100000, 'x'));
        f.close();
        fs::last_write_time(folder, now - std::chrono::hours(10 - i), ec);  // test-0 used longest ago
    }
    PageSketches::trimDisk(others + 150000);  // room for one of the three
    EXPECT_FALSE(fs::exists(root / "test-0", ec));
    EXPECT_FALSE(fs::exists(root / "test-1", ec));
    EXPECT_TRUE(fs::exists(root / "test-2", ec)) << "used last";
    for (int i = 0; i < 3; ++i) {
        fs::remove_all(root / ("test-" + std::to_string(i)), ec);
    }
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

// XQT_BENCH_PDF=<big pdf>: opening it a second time reads the stored previews
TEST_F(Sketches, benchReopen) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    QTemporaryDir dir;
    const QString copy = dir.filePath("bench.pdf");
    ASSERT_TRUE(QFile::copy(pdf, copy));
    for (const char* opening: {"first opening (drawn)", "second opening (read)"}) {
        AppController c;
        const int drawn = PageSketches::instance().drawCount(), read = PageSketches::instance().readCount();
        QElapsedTimer t;
        t.start();
        ASSERT_TRUE(c.openPath(copy));
        ASSERT_TRUE(sketched());
        std::cout << opening << ": all " << c.tabManager().currentSession()->getDocument()->getPageCount()
                  << " pages in " << t.elapsed() << " ms (" << PageSketches::instance().drawCount() - drawn
                  << " drawn, " << PageSketches::instance().readCount() - read << " read)\n";
        qint64 bytes = 0;
        std::error_code ec;
        const fs::path folder = PageSketches::instance().diskFolder(ThumbnailProvider::idOf(c.tabManager().currentSession()));
        for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            bytes += static_cast<qint64>(it->file_size(ec));
        }
        std::cout << "  stored: " << bytes / 1024 << " kB\n";
    }
}

// XQT_BENCH_PDF=<pdf>: what drawing a page costs at different widths (poppler: parsing and decoding are paid at any
// size), each width with a fresh instance of the PDF.
TEST_F(Sketches, benchWidths) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    QTemporaryDir dir;
    const QString copy = dir.filePath("bench.pdf");
    ASSERT_TRUE(QFile::copy(pdf, copy));
    PageSketches::instance().setDelays(600000, 600000);
    AppController c;
    ASSERT_TRUE(c.openPath(copy));
    Document* doc = c.tabManager().currentSession()->getDocument();
    const size_t pages = std::min<size_t>(doc->getPageCount(), 40);
    for (int width: {128, 512, 1024, 2400}) {
        XojPdfDocument own;
        GError* error = nullptr;
        ASSERT_TRUE(own.load(copy.toStdString(), "", &error));
        qint64 total = 0, worst = 0;
        for (size_t p = 0; p < pages; ++p) {
            PageRef page = doc->getPage(p);
            QElapsedTimer t;
            t.start();
            ThumbnailProvider::renderPage(*doc, page, width, &own);
            total += t.elapsed();
            worst = std::max(worst, t.elapsed());
        }
        std::cout << width << " px: " << total / static_cast<qint64>(pages) << " ms per page, worst " << worst
                  << " ms\n";
    }
}
