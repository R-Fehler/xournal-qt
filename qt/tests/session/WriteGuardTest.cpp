/*
 * xournal-qt: one writer of a file at a time (fileio::FileWriteLock) between a DocumentSession's save and the
 * background writers of the app that read a file and write it again (a link rewrite, a to-do ticked in the library):
 * a save of a .xopp waits while another writer holds the file, then writes the document on top of what it wrote.
 * (PDFs with notes: TagsWriteTest.)
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/FileIo.h"

#include "support/TestSupport.h"

using namespace xqt;

namespace {
/// A stroke on a page, as the pen adds it.
void drawOn(DocumentSession& s, size_t pageNo, double y) {
    PageRef page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->setColor(Color(0xff008000U));
    for (int j = 0; j < 12; ++j) {
        stroke->addPoint(Point(80 + j * 20, y + 6 * std::sin(j / 2.0), 1 + (j % 4) / 4.0));
    }
    stroke->getBoundingBox();
    std::unique_lock lock(*s.getDocument());
    page->getSelectedLayer()->addElement(std::move(stroke));
}

size_t strokesIn(const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(xopp);
    EXPECT_TRUE(loaded.document) << loaded.error;
    size_t n = 0;
    for (size_t i = 0; loaded.document && i < loaded.document->getPageCount(); ++i) {
        for (const Layer* l: loaded.document->getPage(i)->getLayers()) {
            n += static_cast<size_t>(std::distance(l->getElementsView().begin(), l->getElementsView().end()));
        }
    }
    return n;
}

class WriteGuardTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

// Ctrl+S of a .xopp while another writer of the app holds the file (on a worker, as LinkRewrite::rewriteFile and
// todos::setInMarkdownFile do): the save waits for it, then writes the document. Neither writes over the other.
TEST_F(WriteGuardTest, aXoppSaveWaitsForAnotherWriterOfTheFile) {
    const fs::path file = path("notes.xopp");
    DocumentSession s(*app);
    drawOn(s, 0, 200);
    ASSERT_TRUE(s.saveAs(file).ok);
    drawOn(s, 0, 400);  // (a second stroke: saved by the Ctrl+S below)

    std::promise<void> holding;
    std::promise<void> release;
    std::thread writer([&, released = release.get_future()]() mutable {
        const fileio::FileWriteLock lock(file);
        holding.set_value();
        released.wait();
    });
    holding.get_future().wait();

    bool saved = false;
    DocumentSession::SaveResult result;
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::Save;
    request.done = [&](const DocumentSession::SaveResult& r) {
        saved = true;
        result = r;
    };
    s.saveInBackground(std::move(request));
    test::waitUpTo([&] { return saved; }, 300);  // (a bounded wait for what must not happen)
    const bool savedMeanwhile = saved;
    const size_t strokesMeanwhile = strokesIn(file);
    release.set_value();
    writer.join();
    s.waitForSaves();

    EXPECT_FALSE(savedMeanwhile) << "the save wrote the file while another writer held it";
    EXPECT_EQ(strokesMeanwhile, 1u);
    ASSERT_TRUE(saved);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(strokesIn(file), 2u) << "the save is in the file";
}
