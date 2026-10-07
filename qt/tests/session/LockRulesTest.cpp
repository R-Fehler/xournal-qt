/*
 * xournal-qt: the rules of AGENTS.md for the document lock and the UI thread, where the session broke them: no PDF is
 * drawn while the document's lock is held (the preview a file keeps of its first page), no file is written under it
 * on the UI thread (the attached PDF of a .xopp: on the save's worker), and loading a file from a worker does not
 * write a process-wide hook (LoadHandler::pdfPassword, installed once).
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/FileIo.h"
#include "session/PageNoteSpace.h"
#include "session/PdfPageKeeper.h"

using namespace xqt;
using namespace std::chrono_literals;

namespace {
void makePdf(const fs::path& p, int pages) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    for (int i = 0; i < pages; ++i) {
        cairo_rectangle(cr, 50, 50 + i * 10, 300, 200);
        cairo_stroke(cr);
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

void addStroke(Document& doc, size_t pageNo) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->addPoint(Point(50, 50, 1.0));
    stroke->addPoint(Point(90, 70, 0.8));
    stroke->getBoundingBox();
    doc.lock();
    doc.getPage(pageNo)->getSelectedLayer()->addElement(std::move(stroke));
    doc.unlock();
}

/// Whether another thread could take the document's lock now (false: someone holds it).
bool lockIsFree(Document& doc) {
    return std::async(std::launch::async, [&doc] {
               if (!doc.try_lock()) {
                   return false;
               }
               doc.unlock();
               return true;
           }).get();
}

/// Holds a save on its worker before it writes the .xopp (PdfPageKeeper::stopSaveAt, step 1) until released.
struct HeldSave {
    HeldSave() {
        PdfPageKeeper::stopSaveAt = [this](int step) {
            if (step == 1) {
                ++entered;
                released.wait_for(5s);  // (never forever: a broken test fails instead of hanging)
            }
            return false;
        };
    }
    ~HeldSave() {
        release();
        PdfPageKeeper::stopSaveAt = nullptr;
    }
    void release() {
        if (!done.exchange(true)) {
            promise.set_value();
        }
    }
    std::atomic<int> entered{0};
    std::promise<void> promise;
    std::shared_future<void> released = promise.get_future().share();
    std::atomic<bool> done{false};
};

bool waitFor(const std::function<bool()>& done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

class LockRulesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    void TearDown() override {
        notespace::beforePdfDrawn = nullptr;
        PdfPageKeeper::stopSaveAt = nullptr;
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

// The preview a .xopp keeps of its first page (DocumentSession::writeDocument: stickers, templates) draws the page's
// PDF without holding the document's lock: a render thread or the pen would wait for poppler meanwhile.
TEST_F(LockRulesTest, theFilesPreviewDrawsItsPdfWithoutTheDocumentLock) {
    makePdf(path("slides.pdf"), 2);
    auto loaded = DocumentSession::loadFile(path("slides.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    Document& doc = *loaded.document;
    int drawn = 0;
    bool heldWhileDrawn = false;
    notespace::beforePdfDrawn = [&] {
        ++drawn;
        heldWhileDrawn = heldWhileDrawn || !lockIsFree(doc);
    };
    const auto r = DocumentSession::writeDocument(doc, path("slides.xopp"));
    notespace::beforePdfDrawn = nullptr;
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(drawn, 1) << "the preview draws the first page's PDF";
    EXPECT_FALSE(heldWhileDrawn) << "the PDF was drawn while the document's lock was held";
    EXPECT_TRUE(doc.getPreview());
}

// A .xopp whose PDF is attached ("name.xopp.bg.pdf" next to it): that PDF is written by the save's worker, not on the
// UI thread while it holds the document's lock (a big PDF stalled the pen and the window).
TEST_F(LockRulesTest, anAttachedPdfIsWrittenByTheSavesWorker) {
    makePdf(path("source.pdf"), 3);
    auto loaded = DocumentSession::loadFile(path("source.pdf"), /*attachPdf=*/true);
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_TRUE(loaded.document->isAttachPdf());
    DocumentSession s(*app, std::move(loaded.document));
    addStroke(*s.getDocument(), 0);
    const fs::path attached = path("lecture.xopp.bg.pdf");

    HeldSave held;
    bool finished = false;
    DocumentSession::SaveResult result;
    s.saveInBackground({DocumentSession::SaveKind::SaveAs, path("lecture.xopp"), {},
                        [&](const DocumentSession::SaveResult& r) {
                            result = r;
                            finished = true;
                        }});
    ASSERT_TRUE(waitFor([&] { return held.entered > 0; }));  // (the worker runs: what the UI thread did is done)
    std::error_code ec;
    EXPECT_FALSE(fs::exists(attached, ec)) << "the attached PDF was written on the UI thread, under the document lock";
    held.release();
    ASSERT_TRUE(waitFor([&] { return finished; }));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_TRUE(fs::exists(attached, ec));
    EXPECT_EQ(fileio::readFile(attached), fileio::readFile(path("source.pdf"))) << "the PDF as it is";

    auto reopened = DocumentSession::loadFile(path("lecture.xopp"));
    ASSERT_TRUE(reopened.document) << reopened.error;
    EXPECT_EQ(reopened.document->getPdfPageCount(), 3u);
    EXPECT_TRUE(reopened.document->isAttachPdf());
}

// LoadHandler::pdfPassword (the password of a .xopp's encrypted background PDF) is installed once; loading a file (on
// library and preview workers too) does not write it again: a write of a global from several threads at once.
TEST_F(LockRulesTest, loadingAFileDoesNotWriteThePasswordHook) {
    makePdf(path("bg.pdf"), 1);
    {
        auto loaded = DocumentSession::loadFile(path("bg.pdf"));
        ASSERT_TRUE(loaded.document) << loaded.error;
        ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, path("notes.xopp")).ok);
    }
    ASSERT_TRUE(DocumentSession::loadFile(path("notes.xopp")).document);
    const auto installed = LoadHandler::pdfPassword;
    EXPECT_NE(installed, nullptr);
    LoadHandler::pdfPassword = [](const fs::path&) { return std::string(); };  // (another hook, set by a test)
    const auto mine = LoadHandler::pdfPassword;
    auto again = DocumentSession::loadFile(path("notes.xopp"));
    const auto after = LoadHandler::pdfPassword;
    LoadHandler::pdfPassword = installed;
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_EQ(after, mine) << "loading a .xopp wrote LoadHandler::pdfPassword again";
}
