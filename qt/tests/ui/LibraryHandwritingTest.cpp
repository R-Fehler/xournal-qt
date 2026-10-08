/*
 * xournal-qt: the library's search finds handwriting (qt/docs/features/handwriting-search.md, "Where the results are
 * kept"), in the real app: a library folder with a .xopp holding handwriting, read by a scripted recogniser. The
 * author (2026-10-08): "it is not finding hits when I search for handwriting on the library level, only on document
 * or all tabs level, even when I save the file".
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/HwrInfo.h"
#include "hwr/InkTextIndexer.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/LibraryInkJob.h"
#include "shell/LibraryModel.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"

#include "config-test.h"

#include "AppController.h"
#include "AppServices.h"
#include "UiFixture.h"

using namespace xqt;

namespace {
/// A written word: a zigzag of `letters` letters, 10 pt high, at x, y
std::unique_ptr<Stroke> written(double x, double y, int letters = 4) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.0);
    s->setColor(Colors::black);
    for (int i = 0; i <= 2 * letters; ++i) {
        s->addPoint(Point(x + i * 3.0, y + (i % 2 ? 10 : 0)));
    }
    return s;
}

/// A .xopp with a line of three handwritten words on each page: words of 4 letters on the first page (read as
/// "Alpha turbine gamma"), of 6 on the others ("delta rotor zeta"), and an empty last page
void writeNotes(const fs::path& file, int pages = 1) {
    fs::create_directories(file.parent_path());
    Document doc(nullptr);
    for (int p = 0; p <= pages; ++p) {
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        if (p < pages) {
            for (int w = 0; w < 3; ++w) {
                page->getSelectedLayer()->addElement(written(50 + 60 * w, 100, p == 0 ? 4 : 6));
            }
        }
        doc.addPage(std::move(page));
    }
    ASSERT_TRUE(DocumentSession::writeDocument(doc, file).ok);
}

class LibraryHandwritingTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString()) / "Library";
        fake = std::make_shared<hwr::FakeRecognizer>();
        fake->setScript([](const hwr::LineInput& line, size_t word) -> hwr::FakeRecognizer::Readings {
            static const QStringList first{"Alpha", "turbine", "gamma"};
            static const QStringList others{"delta", "rotor", "zeta"};
            const QStringList& words = !line.words.empty() && line.words[0].box.width() < 30 ? first : others;
            return {{words.value(static_cast<int>(word), QStringLiteral("w")), 1.0f}};
        });
        hwr::HandwritingSearch::setFactory([f = fake](const QString&) { return f; });
        hwr::InkTextIndexer::setDelay(50);
        LibraryInkJob::setPowerSource([this] { return mains; });
    }
    void TearDown() override {
        handwriting(false);
        closeApp();
        hwr::HandwritingSearch::setFactory({});
        hwr::InkTextIndexer::setDelay(hwr::InkTextIndexer::DELAY_MS);
        LibraryInkJob::setPowerSource({});
    }

    /// The app (again: a restart), on the library, with the handwriting search on
    void start() {
        makeController();
        controller->setLibraryRoot(root);
        qobject_cast<RecentFiles*>(controller->recentModel())->clear();
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1400, 900)}));
        library = qobject_cast<LibraryModel*>(controller->libraryModel());
        ASSERT_NE(library, nullptr);
        until([&] { return !library->indexing() && library->searchIndex(); });
        handwriting(true);
    }
    void handwriting(bool on) {
        if (controller) {
            controller->handwritingSettings()->setProperty("enabled", on);
        }
    }
    DocumentSession* current() const { return controller->tabManager().currentSession(); }
    /// The pages of the current document whose handwriting was read (-1: no indexer)
    int pagesKnown() const {
        auto* indexer = current() ? controller->services().handwriting().indexerOf(current()) : nullptr;
        if (!indexer) {
            return -1;
        }
        const auto pages = indexer->pages();
        return static_cast<int>(std::count_if(pages.begin(), pages.end(), [](const auto& p) { return p.known; }));
    }
    int pageCount() const { return static_cast<int>(current()->getDocument()->getPageCount()); }
    /// The documents the library's search lists for `query`
    std::vector<fs::path> librarySearch(const QString& query) {
        library->setSearchQuery(query);
        auto out = shown();
        library->setSearchQuery({});
        return out;
    }
    /// The documents the library lists now
    std::vector<fs::path> shown() const {
        std::vector<fs::path> out;
        for (int i = 0; i < library->rowCount(); ++i) {
            if (!library->data(library->index(i), LibraryModel::IsFolderRole).toBool()) {
                out.emplace_back(library->data(library->index(i), LibraryModel::PathRole).toString().toStdString());
            }
        }
        return out;
    }
    bool isShown(const fs::path& file) const {
        const auto found = shown();
        return std::find(found.begin(), found.end(), file) != found.end();
    }
    bool listed(const QString& query, const fs::path& file) {
        const auto found = librarySearch(query);
        return std::find(found.begin(), found.end(), file) != found.end();
    }

    QTemporaryDir tmp;
    fs::path root;
    bool mains = false;
    std::shared_ptr<hwr::FakeRecognizer> fake;
    LibraryModel* library = nullptr;
};
}  // namespace

// What the author did (on battery: the library is not read in the background): open a document of the library and let
// its handwriting be read (Ctrl+F finds it). The library's search finds it too, without saving, and after a restart
// (read back from the library's cache, nothing read again)
TEST_F(LibraryHandwritingTest, aDocumentReadWhileOpenIsFoundInTheLibrary) {
    const fs::path file = root / "Sub" / "notes.xopp";
    writeNotes(file);
    ASSERT_NO_FATAL_FAILURE(start());
    EXPECT_FALSE(listed("turbine", file));
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    ASSERT_TRUE(until([&] { return pagesKnown() == pageCount(); }, 10000));
    EXPECT_TRUE(until([&] { return listed("turbine", file); }, 5000)) << "found in the library once read";
    closeApp();
    const int calls = fake->calls();
    ASSERT_NO_FATAL_FAILURE(start());
    EXPECT_TRUE(until([&] { return listed("turbine", file); }, 10000)) << "found after a restart";
    EXPECT_EQ(fake->calls(), calls) << "nothing read again";
}

// Saved before all its pages were read: what was read is found at once, the rest when it is read
TEST_F(LibraryHandwritingTest, aDocumentSavedBeforeItsHandwritingIsReadIsFoundInTheLibrary) {
    const fs::path file = root / "notes.xopp";
    writeNotes(file, 3);
    fake->setDelay(400);  // (a line takes 0.4 s: the first page is read before the others)
    ASSERT_NO_FATAL_FAILURE(start());
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    ASSERT_TRUE(until([&] { return pagesKnown() >= 1; }, 10000));
    ASSERT_LT(pagesKnown(), pageCount());
    ASSERT_TRUE(controller->save());
    EXPECT_TRUE(until([&] { return listed("turbine", file); }, 5000)) << "the first page's words, after saving";
    ASSERT_TRUE(until([&] { return pagesKnown() == pageCount(); }, 10000));
    EXPECT_TRUE(until([&] { return listed("rotor", file); }, 5000)) << "the other pages' words, once read";
}

// A search typed in the library before the handwriting was read lists the document once it is (without typing again)
TEST_F(LibraryHandwritingTest, theLibrarysSearchFollowsTheHandwritingRead) {
    const fs::path file = root / "notes.xopp";
    writeNotes(file);
    ASSERT_NO_FATAL_FAILURE(start());
    library->setSearchQuery("turbine");
    wait(100);
    EXPECT_FALSE(isShown(file));
    mains = true;
    controller->services().libraryInk().check();
    EXPECT_TRUE(until([&] { return isShown(file); }, 10000)) << "listed when the background job read it";
}

// A document never opened: read by the library's background job (on mains power)
TEST_F(LibraryHandwritingTest, aDocumentNeverOpenedIsReadInTheBackground) {
    const fs::path file = root / "notes.xopp";
    writeNotes(file);
    mains = true;
    ASSERT_NO_FATAL_FAILURE(start());
    ASSERT_TRUE(until([&] { return listed("turbine", file); }, 10000)) << "found after the background reading";
    closeApp();
    const int calls = fake->calls();
    ASSERT_NO_FATAL_FAILURE(start());
    EXPECT_TRUE(until([&] { return listed("turbine", file); }, 10000)) << "found after a restart";
    EXPECT_EQ(fake->calls(), calls) << "nothing read again";
}

// End to end with the built-in model (XQT_ONNXRUNTIME): the benchmark's handwriting ("This is a dumb test") opened,
// read and saved; the library's search finds it, also after a restart
TEST_F(LibraryHandwritingTest, theBuiltInModelsReadingIsFoundInTheLibrary) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    hwr::HandwritingSearch::setFactory(&hwr::onnxRecognizerFor);  // (the app's own, as main.cpp sets it)
    const fs::path file = root / "handwritten.xopp";
    fs::create_directories(root);
    fs::copy_file(fs::path(GET_TESTFILE(u8"benchmark/handwritten-text.xopp")), file);
    ASSERT_NO_FATAL_FAILURE(start());
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    ASSERT_TRUE(until([&] { return pagesKnown() == pageCount(); }, 300000));
    ASSERT_TRUE(controller->save());
    EXPECT_TRUE(until([&] { return listed("dumb", file); }, 10000)) << "found in the library after saving";
    closeApp();
    ASSERT_NO_FATAL_FAILURE(start());
    EXPECT_TRUE(until([&] { return listed("dumb", file); }, 10000)) << "found after a restart";
}
