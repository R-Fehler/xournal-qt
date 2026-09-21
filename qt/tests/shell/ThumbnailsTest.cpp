/*
 * xournal-qt: page thumbnails are kept (ThumbnailProvider) and named by the page's revision (DocumentSession), so a
 * page is drawn again only when it changed.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "session/DocumentSession.h"
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
    const qint64 limit = 3 * 1024 * 1024;  // one page at 640 px (2.3 MB), not two
    ThumbnailProvider::setCacheLimit(limit);
    request(urlOf(c, 0), 640);
    request(urlOf(c, 1), 640);
    EXPECT_LE(ThumbnailProvider::cacheBytes(), limit);
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
