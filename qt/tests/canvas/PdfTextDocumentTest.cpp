/*
 * xournal-qt: text documents as PDF (qt/docs/md-pdf.md): a text document of notes (page 1 starts the page's Markdown
 * text) saved as a PDF with notes opens again with its text editable, carries the flow as a plain "name.md" after
 * full and incremental saves (read with qpdf), and typing goes into its text.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <shared_mutex>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <sstream>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PageBookmarks.h"
#include "session/PageNoteSpace.h"
#include "session/PdfBookmarks.h"
#include "session/TextDocument.h"

#include "CanvasView.h"
#include "MarkdownBookmarks.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MarkdownSession.h"
#include "MdBookmarks.h"
#include "MdBox.h"
#include "MdPaginate.h"
#include "control/layer/LayerController.h"

using namespace xqt;

namespace {
/// A text of several pages: headings, lists, a table, code, a formula, and a page break.
std::string longText() {
    std::string s = "# Report\n\nThe *first* page.\n\n";
    s += std::string(TextDocument::PAGE_BREAK) + "\n\n## After the break\n\n";
    for (int i = 0; i < 30; ++i) {
        s += "Paragraph " + std::to_string(i) + " with **bold** words, long enough to take two lines of the page "
             "when it is laid out at eleven points.\n\n";
        if (i % 10 == 0) {
            s += "- one\n- two\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```cpp\nint x = 1;\n```\n\n$$x^2$$\n\n";
        }
    }
    return s + "The end.\n";
}

/// An attachment of a PDF (its data), read with qpdf; `found` says whether it is there.
std::string attachment(const fs::path& pdf, const std::string& name, bool* found = nullptr,
                       std::string* relationship = nullptr, std::string* subtype = nullptr) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    QPDFEmbeddedFileDocumentHelper efdh(q);
    auto spec = efdh.getEmbeddedFile(name);
    if (found) {
        *found = spec != nullptr;
    }
    if (!spec) {
        return {};
    }
    if (relationship) {
        QPDFObjectHandle r = spec->getObjectHandle().getKey("/AFRelationship");
        *relationship = r.isName() ? r.getName() : std::string();
    }
    QPDFObjectHandle stream = spec->getEmbeddedFileStream();
    if (subtype) {
        QPDFObjectHandle st = stream.getDict().getKey("/Subtype");
        *subtype = st.isName() ? st.getName() : std::string();
    }
    auto buffer = stream.getStreamData(qpdf_dl_all);
    return {reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()};
}

std::string flowOf(Document& doc) {
    std::shared_lock lock(doc);
    return TextDocument::flowText(doc);
}

class PdfTextDocumentTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        HybridPdf::compactAbove = 1000;  // (small test files: appended to, not written anew when they grow)
    }
    void TearDown() override {
        HybridPdf::compactAbove = 0.25;
        view.reset();
        session.reset();
        app.reset();
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    void show(std::unique_ptr<Document> doc) {
        view.reset();
        session = std::make_unique<DocumentSession>(*app, std::move(doc));
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        processEvents();
    }
    void processEvents(int ms = 20) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    void type(const std::string& s) {
        ASSERT_NE(view->getMarkdownEditor(), nullptr);
        for (const char c: s) {
            QKeyEvent e(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QString(QChar(c)));
            bool finish = false;
            view->getMarkdownEditor()->keyPressed(&e, finish);
        }
    }
    /// The flow gets `text` (as writing it on the page does: one undo step).
    void setFlow(const std::string& text) {
        MarkdownSession md(*session);
        md.begin(0, MarkdownFile::style());
        md.update(text);
        md.finish();
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
};
}  // namespace

TEST_F(PdfTextDocumentTest, theFlowIsTheTextAndOnlyPageOneMakesATextDocument) {
    const std::string text = longText();
    auto doc = MarkdownFile::notesDocument(text);
    {
        std::shared_lock lock(*doc);
        EXPECT_GT(doc->getPageCount(), 3u);
        EXPECT_TRUE(TextDocument::isTextDocument(*doc));
        EXPECT_TRUE(TextDocument::hasMarkdownText(*doc));
        EXPECT_EQ(TextDocument::flowText(*doc), text) << "the parts joined, without the continuation lines";
        EXPECT_EQ(TextDocument::flowText(*doc).find(md::CONTINUATION), std::string::npos);
        EXPECT_EQ(TextDocument::markdown(*doc), text);
        // The page break ends page 1
        const Text* first = TextDocument::pageBoxOf(doc->getPage(0));
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->getText().find("After the break"), std::string::npos);
    }
    // An empty text document has its (empty) box on page 1
    auto empty = MarkdownFile::notesDocument("");
    {
        std::shared_lock lock(*empty);
        EXPECT_EQ(empty->getPageCount(), 1u);
        EXPECT_TRUE(TextDocument::isTextDocument(*empty));
        EXPECT_EQ(TextDocument::flowText(*empty), "");
    }
    // Notes whose Markdown text starts on page 2: not a text document, but it has Markdown to export
    auto later = MarkdownFile::notesDocument("# Two\n");
    auto blank = std::make_shared<XojPage>(MarkdownFile::PAGE_WIDTH, MarkdownFile::PAGE_HEIGHT);
    blank->getLayers().push_back(new Layer());
    later->insertPage(blank, 0);
    {
        std::shared_lock lock(*later);
        EXPECT_FALSE(TextDocument::isTextDocument(*later));
        EXPECT_TRUE(TextDocument::hasMarkdownText(*later));
        EXPECT_EQ(TextDocument::markdown(*later), "# Two\n");
        EXPECT_TRUE(TextDocument::attachments(*later, "later.pdf").empty()) << "no name.md for notes";
    }
    // Two flows: joined with a page break
    auto two = MarkdownFile::notesDocument("# One\n");
    auto second = MarkdownFile::notesDocument("# Two\n");
    two->addPage(second->getPage(0));
    {
        std::shared_lock lock(*two);
        EXPECT_EQ(TextDocument::markdown(*two), std::string("# One\n\n") + TextDocument::PAGE_BREAK + "\n\n# Two\n");
    }
    EXPECT_EQ(TextDocument::markdownName("Report.pdf"), "Report.md");
    EXPECT_EQ(TextDocument::markdownName("Report.archive.pdf"), "Report.md");
}

// Saved as a PDF with notes it opens again as the same editable text; it carries the flow as "name.md", after a full
// save and after incremental saves
TEST_F(PdfTextDocumentTest, aPdfTextDocumentCarriesItsMarkdownAndOpensEditable) {
    const std::string text = longText();
    show(MarkdownFile::notesDocument(text));
    const fs::path pdf = path("Report.pdf");
    auto r = session->saveAsHybrid(pdf);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.incremental);
    bool found = false;
    std::string subtype;
    EXPECT_EQ(attachment(pdf, "Report.md", &found, nullptr, &subtype), text);
    EXPECT_TRUE(found);
    EXPECT_EQ(subtype, "/text/markdown");
    EXPECT_TRUE(HybridPdf::isHybrid(pdf));

    // Opened again: the same text, editable, a text document
    {
        auto loaded = DocumentSession::loadFile(pdf);
        ASSERT_TRUE(loaded.document) << loaded.error;
        EXPECT_EQ(flowOf(*loaded.document), text);
        std::shared_lock lock(*loaded.document);
        EXPECT_TRUE(TextDocument::isTextDocument(*loaded.document));
    }
    // The clean copy (the background when it is open) does not carry it
    const HybridPdf::Opened opened = HybridPdf::open(pdf);
    ASSERT_TRUE(opened.document) << opened.error;
    attachment(opened.base, "Report.md", &found);
    EXPECT_FALSE(found) << "the clean copy has no name.md";

    // An edit, saved incrementally: the new text
    setFlow("# Changed\n\n" + text);
    r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(attachment(pdf, "Report.md"), "# Changed\n\n" + text);
    // With ink drawn on it (it stays where it is), again incrementally
    {
        std::unique_lock lock(*session->getDocument());
        PageRef p = session->getDocument()->getPage(1);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(2);
        stroke->addPoint(Point(100, 100));
        stroke->addPoint(Point(200, 150));
        p->getSelectedLayer()->addElement(std::move(stroke));
    }
    session->getDocument()->getPage(1)->firePageChanged();
    std::string changed = "# Changed again\n\n" + text;
    changed.replace(changed.find("The end."), 8, "The real end.");
    setFlow(changed);
    r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(attachment(pdf, "Report.md"), changed);
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(flowOf(*loaded.document), changed);

    // No longer a text document (page 1's Markdown text removed): the file is written in full, without name.md
    {
        PageRef p = session->getDocument()->getPage(0);
        Layer* layer = md::markdownLayer(p);
        ASSERT_NE(layer, nullptr);
        session->getLayerController()->removeLayer(p, layer);
        delete layer;
    }
    r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.incremental) << "the attachments changed";
    attachment(pdf, "Report.md", &found);
    EXPECT_FALSE(found);
}

namespace {
/// The width of the media box of each page of a PDF (qpdf).
std::vector<double> mediaWidths(const fs::path& pdf) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    std::vector<double> out;
    for (auto& p: QPDFPageDocumentHelper(q).getAllPages()) {
        const auto m = p.getMediaBox().getArrayAsRectangle();
        out.push_back(m.urx - m.llx);
    }
    return out;
}
}  // namespace

// Space for notes (qt/note-space) on a page of a PDF text document: its larger page box and the name.md both survive
// full and incremental saves
TEST_F(PdfTextDocumentTest, spaceForNotesAndTheMarkdownBothSurviveSaves) {
    const std::string text = longText();
    show(MarkdownFile::notesDocument(text));
    const fs::path pdf = path("Spaced.pdf");
    notespace::Amounts right;
    right.right = 100;
    ASSERT_EQ(notespace::apply(*session, {0}, right), 1u);
    auto r = session->saveAsHybrid(pdf);  // (in full)
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(attachment(pdf, "Spaced.md"), text);
    auto widths = mediaWidths(pdf);
    ASSERT_GE(widths.size(), 2u);
    EXPECT_NEAR(widths[0], MarkdownFile::PAGE_WIDTH + 100, 0.01);
    EXPECT_NEAR(widths[1], MarkdownFile::PAGE_WIDTH, 0.01);

    // The text changed and space on page 2 too: appended; both are in the file
    setFlow("# Spaced\n\n" + text);
    ASSERT_EQ(notespace::apply(*session, {1}, right), 1u);
    r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(attachment(pdf, "Spaced.md"), "# Spaced\n\n" + text);
    widths = mediaWidths(pdf);
    EXPECT_NEAR(widths[0], MarkdownFile::PAGE_WIDTH + 100, 0.01);
    EXPECT_NEAR(widths[1], MarkdownFile::PAGE_WIDTH + 100, 0.01);
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(flowOf(*loaded.document), "# Spaced\n\n" + text);
    EXPECT_EQ(loaded.document->getPage(1)->getNoteSpace(), (NoteSpace{0, 0, 100, 0}));
}

// An empty PDF text document (New text document) keeps its empty text on page 1: it opens as a text document
TEST_F(PdfTextDocumentTest, anEmptyTextDocumentStaysATextDocument) {
    show(MarkdownFile::notesDocument(""));
    const fs::path pdf = path("Empty.pdf");
    ASSERT_TRUE(session->saveAsHybrid(pdf).ok);
    bool found = false;
    EXPECT_EQ(attachment(pdf, "Empty.md", &found), "");
    EXPECT_TRUE(found);
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    show(std::move(loaded.document));
    EXPECT_TRUE(view->typesIntoFlow());
}

// The archive export carries name.md too, as an associated file with the relationship "Alternative"
TEST_F(PdfTextDocumentTest, theArchivePdfCarriesTheMarkdownAsAnAlternative) {
    const std::string text = "# Kept\n\nFor decades.\n";
    auto doc = MarkdownFile::notesDocument(text);
    const fs::path pdf = path("Kept.archive.pdf");
    const HybridPdf::Result r = HybridPdf::writeArchive(*doc, pdf);
    ASSERT_TRUE(r.ok) << r.error;
    bool found = false;
    std::string relationship;
    EXPECT_EQ(attachment(pdf, "Kept.md", &found, &relationship), text);
    EXPECT_TRUE(found);
    EXPECT_EQ(relationship, "/Alternative");
    EXPECT_TRUE(r.pdfa) << (r.notPdfA.empty() ? std::string() : r.notPdfA.front());
}

// Typing into a text document of notes: without a click, into its text (on the page in view, or at the end of the
// text when that page is after it); the pen stays the pen
TEST_F(PdfTextDocumentTest, typingGoesIntoTheText) {
    auto doc = MarkdownFile::notesDocument("# Title\n\nFirst paragraph.\n");
    auto extra = std::make_shared<XojPage>(MarkdownFile::PAGE_WIDTH, MarkdownFile::PAGE_HEIGHT);
    extra->getLayers().push_back(new Layer());
    doc->addPage(extra);  // (a page of notes after the text)
    show(std::move(doc));
    EXPECT_FALSE(view->textMode()) << "the notes model: the tools stay as they are";
    EXPECT_TRUE(view->typesIntoFlow());
    ASSERT_TRUE(view->ensureTextEditor());
    type("Hello ");
    view->endTextEditing();
    EXPECT_EQ(flowOf(*session->getDocument()), "# Title\n\nFirst paragraph.Hello \n") << "at the end of the page's part";
    EXPECT_TRUE(session->isModified());

    // The page in view is after the text: typing goes to the end of the text
    session->setCurrentPageNo(1);
    ASSERT_TRUE(view->ensureTextEditor());
    type("End");
    view->endTextEditing();
    EXPECT_EQ(flowOf(*session->getDocument()), "# Title\n\nFirst paragraph.Hello End\n");
    EXPECT_EQ(session->getDocument()->getPageCount(), 2u);

    // Not for notes without a text on page 1
    show(MarkdownFile::document("", MarkdownFile::style()));
    EXPECT_FALSE(view->typesIntoFlow());
    EXPECT_FALSE(view->ensureTextEditor());
}

namespace {
/// qpdf --check: its exit code (0: no errors or warnings).
int qpdfCheck(const fs::path& pdf, std::string* report = nullptr) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    if (report) {
        *report = out.str() + err.str();
    }
    return job.getExitCode();
}

/// Our "Bookmarks" item of a PDF's outline: (page index, title) of each entry.
std::vector<std::pair<int, std::string>> outlineBookmarks(const fs::path& pdf) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    const auto pages = QPDFPageDocumentHelper(q).getAllPages();
    std::vector<std::pair<int, std::string>> out;
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
}  // namespace

// A PDF text document: the bookmarks are comments in its Markdown (and so in its name.md), and every save writes them
// into the PDF's outline too ("Bookmarks"), for other PDF viewers
TEST_F(PdfTextDocumentTest, theTextsBookmarksGoIntoTheOutline) {
    std::string text = longText();
    text.insert(text.find("Paragraph 12 "), "<!-- xqt:bookmark Twelve -->\n");
    show(MarkdownFile::notesDocument(text));
    const size_t page12 = MarkdownFile::pageOf(*session->getDocument(), text.find("Paragraph 12 "));
    ASSERT_GT(page12, 0u);
    const fs::path pdf = path("Marks.pdf");
    auto r = session->saveAsHybrid(pdf);
    ASSERT_TRUE(r.ok) << r.error;
    using Entries = std::vector<std::pair<int, std::string>>;
    EXPECT_EQ(outlineBookmarks(pdf), (Entries{{static_cast<int>(page12), "Twelve"}}));
    std::string report;
    EXPECT_EQ(qpdfCheck(pdf, &report), 0) << report;
    EXPECT_NE(attachment(pdf, "Marks.md").find("<!-- xqt:bookmark Twelve -->\n"), std::string::npos);

    // "Bookmark this page" on page 1 (named after its heading), saved incrementally
    auto added = MarkdownBookmarks::edit(*session, view.get(), 0, MarkdownBookmarks::Change::Add);
    ASSERT_TRUE(added.changed);
    r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(outlineBookmarks(pdf), (Entries{{0, "Report"}, {static_cast<int>(page12), "Twelve"}}));
    EXPECT_EQ(qpdfCheck(pdf, &report), 0) << report;
    EXPECT_EQ(attachment(pdf, "Marks.md").rfind("<!-- xqt:bookmark -->\n# Report", 0), 0u);

    // Opened again: the same bookmarks, from the text
    {
        auto loaded = DocumentSession::loadFile(pdf);
        ASSERT_TRUE(loaded.document) << loaded.error;
        std::shared_lock lock(*loaded.document);
        const auto marks = PageBookmarks::of(*loaded.document);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_EQ(marks[0].label, "Report");
        EXPECT_EQ(marks[1].page, page12);
    }
    // Removed in the text: gone from the outline
    ASSERT_TRUE(MarkdownBookmarks::edit(*session, view.get(), page12, MarkdownBookmarks::Change::Remove).changed);
    ASSERT_TRUE(MarkdownBookmarks::edit(*session, view.get(), 0, MarkdownBookmarks::Change::Remove).changed);
    r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(outlineBookmarks(pdf).empty());
    EXPECT_EQ(qpdfCheck(pdf, &report), 0) << report;
    EXPECT_EQ(flowOf(*session->getDocument()), longText());

    // A page of notes after the text keeps a bookmark of its own (the page's, not the text's)
    {
        std::shared_lock lock(*session->getDocument());
        EXPECT_FALSE(MarkdownBookmarks::isTextPage(*session->getDocument(), session->getDocument()->getPageCount()));
    }
}

// Bookmarks written by the qt/bookmarks build as page attributes on pages of a text become comments in the text when
// the document is opened (before the first block starting on the page, the label kept; a page with a comment keeps
// only that), without marking it modified; saved and opened again, they are comments (and nothing is added twice)
TEST_F(PdfTextDocumentTest, pageAttributeBookmarksOfATextBecomeComments) {
    std::string text = longText();
    text.insert(text.find("Paragraph 20 "), "<!-- xqt:bookmark Twenty -->\n");
    const size_t page20 = [&] {
        auto d = MarkdownFile::notesDocument(text);
        return MarkdownFile::pageOf(*d, text.find("Paragraph 20 "));
    }();
    const auto withAttributes = [&] {
        auto doc = MarkdownFile::notesDocument(text);
        std::unique_lock lock(*doc);
        doc->getPage(0)->setBookmark(std::string());       // automatic
        doc->getPage(1)->setBookmark(std::string("Old"));   // named
        doc->getPage(page20)->setBookmark(std::string("Duplicate"));  // the page has a comment already
        return doc;
    };
    ASSERT_GT(page20, 1u);
    using Marks = std::vector<std::pair<size_t, std::string>>;
    const auto marksOf = [](Document& d) {
        std::shared_lock lock(d);
        Marks out;
        for (const auto& m: PageBookmarks::of(d)) {
            out.emplace_back(m.page, m.label);
        }
        return out;
    };
    const Marks expected{{0, "Report"}, {1, "Old"}, {page20, "Twenty"}};
    const auto count = [](const std::string& s, const std::string& what) {
        size_t n = 0;
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) {
            ++n;
        }
        return n;
    };

    for (const char* name: {"Old.pdf", "Old.xopp"}) {
        const fs::path file = path(name);
        const bool pdf = std::string(name).find(".pdf") != std::string::npos;
        {
            auto doc = withAttributes();
            if (pdf) {
                show(std::move(doc));
                ASSERT_TRUE(session->saveAsHybrid(file).ok);
            } else {
                ASSERT_TRUE(DocumentSession::writeDocument(*doc, file).ok);
            }
        }
        // Opened: comments in the text, the bookmarks on their pages
        auto loaded = DocumentSession::loadFile(file);
        ASSERT_TRUE(loaded.document) << loaded.error;
        const std::string migrated = flowOf(*loaded.document);
        EXPECT_EQ(marksOf(*loaded.document), expected) << name;
        EXPECT_EQ(migrated.rfind("<!-- xqt:bookmark -->\n# Report", 0), 0u) << name;
        EXPECT_EQ(count(migrated, "<!-- xqt:bookmark Old -->\n"), 1u) << name;
        EXPECT_EQ(count(migrated, "xqt:bookmark"), 3u) << name << ": no comment for the page that had one";
        EXPECT_EQ(migrated.find("Duplicate"), std::string::npos);
        {
            std::shared_lock lock(*loaded.document);
            size_t page = 0;
            for (size_t i = 0; i < loaded.document->getPageCount(); ++i) {
                const Text* box = TextDocument::pageBoxOf(loaded.document->getPage(i));
                if (box && box->getText().find("<!-- xqt:bookmark Old -->") != std::string::npos) {
                    page = i;
                }
            }
            EXPECT_EQ(page, 1u) << "the comment is in page 2's part";
        }
        // In a session: not modified; saved and opened again: the same comments, nothing added twice
        show(std::move(loaded.document));
        EXPECT_FALSE(session->isModified());
        const fs::path again = path(pdf ? "Again.pdf" : "Again.xopp");
        ASSERT_TRUE((pdf ? session->saveAsHybrid(again) : session->saveAs(again)).ok);
        auto reloaded = DocumentSession::loadFile(again);
        ASSERT_TRUE(reloaded.document) << reloaded.error;
        EXPECT_EQ(flowOf(*reloaded.document), migrated) << name;
        EXPECT_EQ(marksOf(*reloaded.document), expected) << name;
        // The text is what carries them: without it (the comments taken out) there are none
        {
            std::unique_lock lock(*reloaded.document);
            for (size_t i = 0; i < reloaded.document->getPageCount(); ++i) {
                if (Text* box = TextDocument::pageBoxOf(reloaded.document->getPage(i))) {
                    std::string t = box->getText();
                    for (auto marks = md::bookmarks::find(t); !marks.empty(); marks = md::bookmarks::find(t)) {
                        t.erase(marks.front().begin, marks.front().end - marks.front().begin);
                    }
                    box->setText(t);
                }
            }
            TextDocument::syncBookmarks(*reloaded.document);
        }
        EXPECT_TRUE(marksOf(*reloaded.document).empty()) << name;
    }
}
