/*
 * xournal-qt: the PDF caches of a view and the render threads (infra review 2026-10 §6.1/6.2). Renders take a cache
 * from the view and hold it while they draw; the UI thread replaces the caches when the document's PDF changes. A
 * render must never see no cache, the cache it holds must outlive the replacement, and a replaced cache must go once
 * the last render that holds it is done (they used to stay with the view for as long as it lived).
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThreadPool>
#include <gtest/gtest.h>

#include "control/PdfCache.h"
#include "model/Document.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "config-test.h"

using namespace xqt;

namespace {
class PdfCacheTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp"));
        ASSERT_TRUE(loaded.document);
        session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1200));
        settle();
    }
    void TearDown() override {
        view.reset();
        session.reset();
        app.reset();
    }
    /// The renders and the workers that empty replaced caches are done
    void settle() {
        for (int i = 0; i < 3; ++i) {
            app->getRenderService()->waitForIdle();
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::processEvents();
        }
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
};
}  // namespace

TEST_F(PdfCacheTest, aRenderKeepsItsCacheWhileItIsReplacedAndItGoesAfterwards) {
    // A render of a page in view and one of a page drawn in advance take their caches
    std::shared_ptr<PdfCache> visible = view->rasterPdfCache(false);
    std::shared_ptr<PdfCache> inAdvance = view->rasterPdfCache(true);
    ASSERT_TRUE(visible);
    ASSERT_TRUE(inAdvance);
    const std::weak_ptr<PdfCache> oldVisible = visible;
    const std::weak_ptr<PdfCache> oldInAdvance = inAdvance;

    // Meanwhile the document's PDF is replaced
    view->recreatePdfCache();
    settle();
    EXPECT_NE(view->rasterPdfCache(false), visible) << "a new cache for the new PDF";
    EXPECT_FALSE(oldVisible.expired()) << "the render still draws with the cache it took";
    EXPECT_FALSE(oldInAdvance.expired());

    // The renders end: nothing keeps the old caches
    visible.reset();
    inAdvance.reset();
    settle();
    EXPECT_TRUE(oldVisible.expired()) << "a replaced cache goes with the last render that held it";
    EXPECT_TRUE(oldInAdvance.expired());
}

TEST_F(PdfCacheTest, replacingTheCacheOftenKeepsNoneOfTheOldOnes) {
    std::vector<std::weak_ptr<PdfCache>> replaced;
    for (int i = 0; i < 20; ++i) {
        replaced.emplace_back(view->rasterPdfCache(false));
        replaced.emplace_back(view->rasterPdfCache(true));
        view->replacePdfCache(i % 2 == 0);  // (a reload; pasted pages joining the merged PDF)
    }
    settle();
    size_t alive = 0;
    for (const auto& cache: replaced) {
        alive += !cache.expired();
    }
    EXPECT_EQ(alive, 0u) << "every replaced cache is gone";
}

TEST_F(PdfCacheTest, aRenderNeverFindsNoCacheWhileTheCacheIsReplaced) {
    // Render threads take the caches while the UI thread replaces them
    std::atomic<bool> stop{false};
    std::atomic<int> missing{0};
    std::atomic<int> taken[2] = {0, 0};
    std::vector<std::thread> renders;
    for (int background: {0, 1}) {
        renders.emplace_back([&, background] {
            while (!stop) {
                missing += view->rasterPdfCache(background) == nullptr;
                ++taken[background];
            }
        });
    }
    // (both take caches already: the replacements fall into their renders)
    while (taken[0] == 0 || taken[1] == 0) {
        std::this_thread::yield();
    }
    for (int i = 0; i < 300; ++i) {
        view->replacePdfCache(false);
    }
    stop = true;
    for (auto& t: renders) {
        t.join();
    }
    settle();
    EXPECT_EQ(missing.load(), 0) << "a render took the cache between the old one going and the new one coming";
}
