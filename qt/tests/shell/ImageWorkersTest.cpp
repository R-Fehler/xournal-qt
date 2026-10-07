/*
 * xournal-qt: the image workers (ImageWorkers.h): one owner for the pools of the image providers, at idle priority,
 * stopped by shutdown; responses QML cancelled are not drawn.
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <memory>
#include <vector>

#include <QQuickImageResponse>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include "shell/AsyncImage.h"
#include "shell/DocumentFiles.h"
#include "shell/ImageWorkers.h"
#include "shell/DocumentCovers.h"

#include "support/TestSupport.h"

using namespace xqt;
using xqt::test::waitFor;

TEST(ImageWorkers, everyPoolRunsAtIdlePriority) {
    for (int i = 0; i < static_cast<int>(ImageWorkers::Pool::Count); ++i) {
        const auto pool = static_cast<ImageWorkers::Pool>(i);
        std::atomic<int> priority{-1};
        ASSERT_TRUE(ImageWorkers::start(pool, [&] { priority = QThread::currentThread()->priority(); }));
        ImageWorkers::waitForDone(pool);
        EXPECT_EQ(priority.load(), QThread::IdlePriority) << "pool " << i;
        EXPECT_GE(ImageWorkers::threadsOf(pool), 1);
    }
}

TEST(ImageWorkers, nothingStartsAfterShutdownAndResponsesStillFinish) {
    ImageWorkers::shutdown();
    EXPECT_TRUE(ImageWorkers::isShutDown());
    bool ran = false;
    EXPECT_FALSE(ImageWorkers::start(ImageWorkers::Pool::Thumbnails, [&] { ran = true; }));
    // A provider's response is finished all the same (QML waits for it), without an image
    std::unique_ptr<AsyncImageResponse> response(new AsyncImageResponse);
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    ImageWorkers::respond(ImageWorkers::Pool::HitPages, response.get(), [&] {
        ran = true;
        return QImage(4, 4, QImage::Format_RGB32);
    });
    EXPECT_TRUE(finished.wait(2000));
    EXPECT_FALSE(ran);
    ImageWorkers::reopen();
    EXPECT_TRUE(ImageWorkers::start(ImageWorkers::Pool::Thumbnails, [&] { ran = true; }));
    ImageWorkers::waitForDone(ImageWorkers::Pool::Thumbnails);
    EXPECT_TRUE(ran);
}

// Flinging through the library grid cancels the cards that scrolled past: their documents are not loaded and drawn
// (shell review §6.3: before, every card queued a whole document load).
TEST(ImageWorkers, aCancelledCoverIsNotDrawn) {
    QTemporaryDir dir;
    std::vector<DocumentItem> items;
    for (int i = 0; i < 30; ++i) {
        const fs::path pdf = fs::path(dir.filePath(QString("doc%1.pdf").arg(i)).toStdString());
        test::makeTextPdf(pdf, test::numbered("Page ", 3));
        items.push_back(DocumentFiles::itemOf(pdf));
        ASSERT_TRUE(items.back().valid());
        ASSERT_FALSE(fs::exists(DocumentCovers::outsideFile(items.back())));
    }
    CoverProvider provider;
    std::vector<std::unique_ptr<QQuickImageResponse>> responses;
    for (const DocumentItem& item: items) {
        responses.emplace_back(
                provider.requestImageResponse(DocumentCovers::url(item).mid(QString("image://cover/").size()), {}));
        responses.back()->cancel();  // (scrolled past at once)
    }
    int finished = 0;
    for (auto& r: responses) {
        QObject::connect(r.get(), &QQuickImageResponse::finished, [&] { ++finished; });
    }
    ASSERT_TRUE(waitFor([&] { return finished == static_cast<int>(responses.size()); }, 20000));
    int drawn = 0;
    for (const DocumentItem& item: items) {
        drawn += fs::exists(DocumentCovers::outsideFile(item));
    }
    EXPECT_LE(drawn, ImageWorkers::threadsOf(ImageWorkers::Pool::Covers))
            << "only those whose worker began before they were cancelled";
}
