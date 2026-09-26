/*
 * xournal-qt: bookmarks on pages (qt/docs/bookmarks.md): the .xopp page attribute (and that upstream Xournal++ still
 * opens such a file without a message), one undo step per change, bookmarks following their pages, and the PDF
 * outline item "Bookmarks" (written in full and as incremental updates, the document's own outline kept).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <sstream>

#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/IncrementalPdf.h"
#include "session/PageBookmarks.h"
#include "session/PdfBookmarks.h"
#include "undo/UndoRedoHandler.h"

#include "config-test.h"

using namespace xqt;

namespace {
/// A PDF of `n` pages with an outline of its own: "Chapter 1" (page 1) with a section, "Chapter 2" (page 3).
void makeBook(const fs::path& p, int n) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    const int ch1 = cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 1", "page=1",
                                                  CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(s, ch1, "Section 1.1", "page=2", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 2", "page=3", CAIRO_PDF_OUTLINE_FLAG_OPEN);
    for (int i = 0; i < n; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("page " + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

int qpdfCheck(const fs::path& pdf, std::string& output) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    output = out.str() + err.str();
    return job.getExitCode();
}

/// The top-level titles of a PDF's outline (qpdf).
std::vector<std::string> topTitles(QPDF& q) {
    std::vector<std::string> titles;
    for (QPDFObjectHandle c = q.getRoot().getKey("/Outlines").getKey("/First"); c.isDictionary();
         c = c.getKey("/Next")) {
        titles.push_back(c.getKey("/Title").getUTF8Value());
    }
    return titles;
}

std::vector<std::pair<int, std::string>> entriesOf(QPDF& q) {
    std::vector<std::pair<int, std::string>> out;
    const auto pages = QPDFPageDocumentHelper(q).getAllPages();
    for (const auto& e: PdfBookmarks::read(q)) {
        int index = -1;
        for (size_t i = 0; i < pages.size(); ++i) {
            if (pages[i].getObjectHandle().getObjGen() == e.page.getObjGen()) {
                index = static_cast<int>(i);
            }
        }
        out.emplace_back(index, e.title);
    }
    return out;
}

using Marks = std::vector<std::pair<size_t, std::string>>;
Marks marksOf(const Document& doc) {
    Marks m;
    for (const auto& b: PageBookmarks::of(doc)) {
        m.emplace_back(b.page, b.label);
    }
    return m;
}

class BookmarksTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    /// A new document of three pages.
    std::unique_ptr<DocumentSession> threePages() {
        auto s = std::make_unique<DocumentSession>(*app);
        s->insertNewPage(1);
        s->insertNewPage(2);
        s->getUndoRedoHandler()->clearContents();
        return s;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

// A bookmark is the page attribute xqt-bookmark: written only on bookmarked pages, read back with its label (quotes,
// ampersands and all), and the automatic one as an empty label; nothing else in the file warns
TEST_F(BookmarksTest, aXoppKeepsThemAsAPageAttribute) {
    auto s = threePages();
    ASSERT_TRUE(s->setBookmark(0, std::string("Intro & \"<summary>\" – café")));
    ASSERT_TRUE(s->setBookmark(2, std::string()));
    ASSERT_TRUE(s->saveAs(path("notes.xopp")).ok);
    auto loaded = DocumentSession::loadFile(path("notes.xopp"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.warnings.empty()) << loaded.warnings.front();
    EXPECT_EQ(marksOf(*loaded.document), (Marks{{0, "Intro & \"<summary>\" – café"}, {2, ""}}));
    EXPECT_FALSE(loaded.document->getPage(1)->getBookmark());
    EXPECT_EQ(PageBookmarks::displayLabel("", 2), "Page 3");
    // The raw XML: one attribute per bookmarked page
    QProcess gz;
    gz.start("gzip", {"-dc", tmp.filePath("notes.xopp")});
    ASSERT_TRUE(gz.waitForFinished());
    const QString xml = QString::fromUtf8(gz.readAllStandardOutput());
    EXPECT_EQ(xml.count("xqt-bookmark="), 2);
    EXPECT_TRUE(xml.contains("xqt-bookmark=\"Intro &amp; &quot;&lt;summary&gt;&quot; – café\"")) << xml.toStdString();
}

// Upstream Xournal++ looks attributes up by name: it opens (and exports) such a file without any message
TEST_F(BookmarksTest, upstreamXournalppOpensItWithoutAMessage) {
    const QString upstream = qEnvironmentVariableIsSet("XOJ_UPSTREAM_BIN") ? qEnvironmentVariable("XOJ_UPSTREAM_BIN")
                                                                          : QString(XQT_UPSTREAM_BIN);
    if (!QFileInfo(upstream).isExecutable()) {
        GTEST_SKIP() << "no upstream xournalpp at " << upstream.toStdString() << " (set XOJ_UPSTREAM_BIN)";
    }
    auto s = threePages();
    ASSERT_TRUE(s->setBookmark(1, std::string("Exercises")));
    ASSERT_TRUE(s->saveAs(path("notes.xopp")).ok);
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();  // (its own configuration: AGENTS.md)
    for (const char* var: {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME"}) {
        env.insert(var, tmp.filePath(QString("upstream-") + var));
    }
    p.setProcessEnvironment(env);
    p.start(upstream, {tmp.filePath("notes.xopp"), "--create-pdf=" + tmp.filePath("upstream.pdf")});
    ASSERT_TRUE(p.waitForFinished(60000)) << "upstream did not finish";
    const QString output = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
    ASSERT_EQ(p.exitCode(), 0) << output.toStdString();
    EXPECT_FALSE(output.contains("error", Qt::CaseInsensitive)) << output.toStdString();
    EXPECT_FALSE(output.contains("xqt-bookmark")) << output.toStdString();
    EXPECT_TRUE(QFileInfo(tmp.filePath("upstream.pdf")).size() > 0);
}

// Setting, renaming and removing are one undo step each; the document counts as changed
TEST_F(BookmarksTest, eachChangeIsOneUndoStep) {
    auto s = threePages();
    QSignalSpy changed(s.get(), &DocumentSession::bookmarksChanged);
    EXPECT_FALSE(s->setBookmark(1, std::nullopt)) << "nothing to remove";
    ASSERT_TRUE(s->setBookmark(1, std::string()));
    EXPECT_FALSE(s->setBookmark(1, std::string())) << "the same";
    ASSERT_TRUE(s->setBookmark(1, std::string("Proof")));
    EXPECT_TRUE(s->isModified());
    EXPECT_EQ(s->getUndoRedoHandler()->undoDescription(), "Undo: Rename bookmark");
    s->getUndoRedoHandler()->undo();
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{1, ""}}));
    s->getUndoRedoHandler()->undo();
    EXPECT_TRUE(marksOf(*s->getDocument()).empty());
    s->getUndoRedoHandler()->redo();
    s->getUndoRedoHandler()->redo();
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{1, "Proof"}}));
    ASSERT_TRUE(s->setBookmark(1, std::nullopt));
    EXPECT_EQ(s->getUndoRedoHandler()->undoDescription(), "Undo: Remove bookmark");
    s->getUndoRedoHandler()->undo();
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{1, "Proof"}}));
    EXPECT_EQ(changed.count(), 8);
}

// The bookmark is the page's: it moves with it, goes with it and comes back with the undone deletion; a copy of the
// page starts without one
TEST_F(BookmarksTest, itFollowsItsPage) {
    auto s = threePages();
    ASSERT_TRUE(s->setBookmark(1, std::string("Middle")));
    ASSERT_TRUE(s->movePages({1}, 0));
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{0, "Middle"}}));
    ASSERT_TRUE(s->deletePages({0}));
    EXPECT_TRUE(marksOf(*s->getDocument()).empty());
    s->getUndoRedoHandler()->undo();
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{0, "Middle"}}));
    s->setCurrentPageNo(0);
    s->duplicatePage();
    ASSERT_EQ(s->getDocument()->getPageCount(), 4u);
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{0, "Middle"}}));
    s->insertNewPage(0);
    EXPECT_EQ(marksOf(*s->getDocument()), (Marks{{1, "Middle"}}));
}

// Our outline item: added as the last top-level item, written again when the bookmarks differ, removed when there are
// none; the document's own outline stays as it was, and every revision passes qpdf --check
TEST_F(BookmarksTest, theOutlineItemInFullAndIncrementalWrites) {
    makeBook(path("book.pdf"), 4);
    const fs::path file = path("written.pdf");
    {
        QPDF q;
        q.processFile(path("book.pdf").string().c_str());
        const auto pages = QPDFPageDocumentHelper(q).getAllPages();
        EXPECT_TRUE(PdfBookmarks::write(q, {{pages[1].getObjectHandle(), "Proof"}, {pages[3].getObjectHandle(), "Page 4"}}));
        QPDFWriter w(q, file.string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);
        w.write();
    }
    std::string report;
    EXPECT_EQ(qpdfCheck(file, report), 0) << report;
    {
        QPDF q;
        q.processFile(file.string().c_str());
        EXPECT_EQ(topTitles(q), (std::vector<std::string>{"Chapter 1", "Chapter 2", "Bookmarks"}));
        EXPECT_EQ(entriesOf(q), (std::vector<std::pair<int, std::string>>{{1, "Proof"}, {3, "Page 4"}}));
        EXPECT_EQ(q.getRoot().getKey("/Outlines").getKey("/First").getKey("/First").getKey("/Title").getUTF8Value(),
                  "Section 1.1") << "the chapter keeps its section";
    }
    // Incremental updates: the same bookmarks append nothing of the outline; others rewrite our item; none remove it
    auto appendWith = [&](const std::vector<std::pair<int, std::string>>& marks) {
        IncrementalPdf::Tail tail;
        std::string error;
        EXPECT_TRUE(IncrementalPdf::readTail(file, tail, error)) << error;
        QPDF q;
        q.processFile(file.string().c_str());
        IncrementalPdf::Update u(q);
        const auto pages = QPDFPageDocumentHelper(q).getAllPages();
        std::vector<PdfBookmarks::Entry> entries;
        for (const auto& [page, title]: marks) {
            entries.push_back({pages[static_cast<size_t>(page)].getObjectHandle(), title});
        }
        const bool changed = PdfBookmarks::write(q, entries, &u);
        IncrementalPdf::Stats stats;
        const std::string bytes = u.serialize(tail, &stats);
        EXPECT_TRUE(IncrementalPdf::append(file, tail, bytes).ok);
        std::string check;
        EXPECT_EQ(qpdfCheck(file, check), 0) << check;
        return std::make_pair(changed, stats);
    };
    auto [same, sameStats] = appendWith({{1, "Proof"}, {3, "Page 4"}});
    EXPECT_FALSE(same);
    EXPECT_EQ(sameStats.changed + sameStats.added, 0u);
    auto [renamed, renamedStats] = appendWith({{0, "Start"}, {1, "Proof (long)"}, {3, "Page 4"}});
    EXPECT_TRUE(renamed);
    EXPECT_EQ(renamedStats.changed, 2u) << "our item and the outline dictionary (its /Count)";
    EXPECT_EQ(renamedStats.added, 3u) << "the new children";
    {
        QPDF q;
        q.processFile(file.string().c_str());
        EXPECT_EQ(topTitles(q), (std::vector<std::string>{"Chapter 1", "Chapter 2", "Bookmarks"}));
        EXPECT_EQ(entriesOf(q),
                  (std::vector<std::pair<int, std::string>>{{0, "Start"}, {1, "Proof (long)"}, {3, "Page 4"}}));
    }
    auto [removed, removedStats] = appendWith({});
    EXPECT_TRUE(removed);
    {
        QPDF q;
        q.processFile(file.string().c_str());
        EXPECT_EQ(topTitles(q), (std::vector<std::string>{"Chapter 1", "Chapter 2"}));
        EXPECT_TRUE(PdfBookmarks::read(q).empty());
        EXPECT_FALSE(q.getRoot().getKey("/Outlines").getKey("/Last").hasKey("/Next"));
    }
    auto [added, addedStats] = appendWith({{2, "Again"}});
    EXPECT_TRUE(added);
    QPDF q;
    q.processFile(file.string().c_str());
    EXPECT_EQ(topTitles(q), (std::vector<std::string>{"Chapter 1", "Chapter 2", "Bookmarks"}));
    EXPECT_EQ(entriesOf(q), (std::vector<std::pair<int, std::string>>{{2, "Again"}}));
}

// A PDF without our data whose outline has our item (e.g. a copy another app wrote again): its pages get the
// bookmarks, "Page N" to page N as the automatic label; the table of contents leaves the item out
TEST_F(BookmarksTest, aPlainPdfTakesThemFromItsOutline) {
    makeBook(path("book.pdf"), 4);
    {
        QPDF q;
        q.processFile(path("book.pdf").string().c_str());
        const auto pages = QPDFPageDocumentHelper(q).getAllPages();
        PdfBookmarks::write(q, {{pages[0].getObjectHandle(), "Page 1"}, {pages[2].getObjectHandle(), "Results"}});
        QPDFWriter w(q, path("marked.pdf").string().c_str());
        w.write();
    }
    auto loaded = DocumentSession::loadFile(path("marked.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_EQ(marksOf(*loaded.document), (Marks{{0, ""}, {2, "Results"}}));
    int ours = 0;
    for (const auto& e: loaded.document->getOutline()) {
        ours += PageBookmarks::isOutlineItem(e) ? 1 : 0;
    }
    EXPECT_EQ(ours, 1);
    // The book alone: its chapters are no bookmarks
    auto book = DocumentSession::loadFile(path("book.pdf"));
    ASSERT_TRUE(book.document);
    EXPECT_TRUE(marksOf(*book.document).empty());
    for (const auto& e: book.document->getOutline()) {
        EXPECT_FALSE(PageBookmarks::isOutlineItem(e)) << e.title;
    }
}
