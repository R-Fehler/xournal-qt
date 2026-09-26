/*
 * xournal-qt: text documents as PDF, the app's side (qt/docs/md-pdf.md): what a new text document is (the setting,
 * its default by the way documents are kept), "Open as PDF document" of a .md, "Export as Markdown", and the
 * library's search in a PDF text document.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <iterator>
#include <shared_mutex>
#include <string>

#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/TextDocument.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/TabManager.h"

#include "AppController.h"
#include "CanvasView.h"
#include "MarkdownFile.h"

using namespace xqt;

namespace {
std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeFile(const fs::path& p, const std::string& bytes) { std::ofstream(p, std::ios::binary) << bytes; }
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
QUrl url(const fs::path& p) { return QUrl::fromLocalFile(qstr(p)); }

/// The data of an attachment of a PDF (read with qpdf); "<none>" when it has none of that name.
std::string attachment(const fs::path& pdf, const std::string& name) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    auto spec = QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(name);
    if (!spec) {
        return "<none>";
    }
    auto buffer = spec->getEmbeddedFileStream().getStreamData(qpdf_dl_all);
    return {reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()};
}

std::string flowOf(DocumentSession& s) {
    std::shared_lock lock(*s.getDocument());
    return TextDocument::flowText(*s.getDocument());
}

const std::string TEXT = "# Field notes\n\nA *quokka* smiled.\n\n<div style=\"page-break-after: always\"></div>\n\n"
                         "## Later\n\n- one\n- two\n";

class TextPdf: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    /// The way documents are kept and the kind of new text documents, as Settings store them; back to "not chosen"
    /// when it goes (the tests of one run share the config folder).
    struct Choice {
        explicit Choice(AppController& c): settings(*c.context().getSettings()) {}
        ~Choice() {
            DocumentMode::store(settings, DocumentMode::Mode::Unset);
            settings.getCustomElement("xournalQt").setString("newTextDocuments", "");
            settings.customSettingsChanged();
        }
        Settings& settings;
    };
    DocumentSession& current(AppController& c) { return *c.tabManager().currentSession(); }
    CanvasView* view(AppController& c) { return c.tabManager().view(c.tabManager().currentIndex()); }

    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

// New text documents follow the way documents are kept, unless the setting says otherwise; a .md stays a .md
TEST_F(TextPdf, newTextDocumentsFollowTheSettingAndMarkdownFilesStayMarkdown) {
    AppController c;
    Choice choice(c);
    Settings& settings = *c.context().getSettings();
    c.setLibraryRoot(root);

    // Xournal++ files (the tests' default): a Markdown file
    EXPECT_EQ(DocumentMode::newTextDocuments(settings), DocumentMode::TextKind::Markdown);
    EXPECT_FALSE(c.newTextAsPdf());
    ASSERT_TRUE(c.createTextDocument("Ideas"));
    EXPECT_TRUE(fs::exists(root / "Ideas.md"));
    EXPECT_EQ(c.textDocument(), "markdown");

    // PDF files: a PDF text document, saved at once, with the cursor in its text
    DocumentMode::store(settings, DocumentMode::Mode::Pdf);
    EXPECT_EQ(DocumentMode::newTextDocuments(settings), DocumentMode::TextKind::Pdf);
    EXPECT_TRUE(c.newTextAsPdf());
    ASSERT_TRUE(c.createTextDocument("Report"));
    EXPECT_EQ(current(c).getFilePath(), root / "Report.pdf");
    EXPECT_TRUE(HybridPdf::isHybrid(root / "Report.pdf"));
    EXPECT_FALSE(current(c).isModified());
    EXPECT_TRUE(c.textNotes());
    EXPECT_EQ(c.textDocument(), "") << "notes, not a text file";
    ASSERT_NE(view(c), nullptr);
    EXPECT_NE(view(c)->getMarkdownEditor(), nullptr) << "the cursor is in its text";
    EXPECT_EQ(attachment(root / "Report.pdf", "Report.md"), "");
    // Written in, saved: the PDF has the text, as name.md too
    view(c)->endTextEditing();
    MarkdownFile::setText(current(c), TEXT);
    ASSERT_TRUE(c.save());
    EXPECT_EQ(attachment(root / "Report.pdf", "Report.md"), TEXT);
    {
        auto loaded = DocumentSession::loadFile(root / "Report.pdf");
        ASSERT_TRUE(loaded.document);
        std::shared_lock lock(*loaded.document);
        EXPECT_EQ(TextDocument::flowText(*loaded.document), TEXT);
    }
    // Taken names: the next free one, for every kind of document
    ASSERT_TRUE(c.createTextDocument("Ideas"));
    EXPECT_EQ(current(c).getFilePath(), root / "Ideas (2).pdf");

    // The existing .md is still a .md: opened in PDF files mode, edited and saved, it is written as itself
    ASSERT_TRUE(c.openPath(qstr(root / "Ideas.md")));
    EXPECT_EQ(c.textDocument(), "markdown");
    MarkdownFile::setText(current(c), "Still Markdown\n");
    ASSERT_TRUE(c.save());
    EXPECT_EQ(bytesOf(root / "Ideas.md"), "Still Markdown\n");
    EXPECT_FALSE(fs::exists(root / "Ideas.pdf"));

    // The setting wins over the mode, both ways
    DocumentMode::setNewTextDocuments(settings, DocumentMode::TextKind::Markdown);
    EXPECT_FALSE(c.newTextAsPdf());
    DocumentMode::store(settings, DocumentMode::Mode::Xopp);
    DocumentMode::setNewTextDocuments(settings, DocumentMode::TextKind::Pdf);
    EXPECT_TRUE(c.newTextAsPdf());
    ASSERT_TRUE(c.createTextDocument("Memo"));
    EXPECT_TRUE(HybridPdf::isHybrid(root / "Memo.pdf"));
    EXPECT_FALSE(fs::exists(root / "Memo.md"));
}

// The library's index reads the text of a PDF text document: its words are found
TEST_F(TextPdf, theLibrarySearchFindsTheTextOfAPdfTextDocument) {
    {
        QTemporaryDir config;
        AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(config.filePath("settings.xml").toStdString()), 1);
        auto doc = MarkdownFile::notesDocument(TEXT + "\nThe wombat wandered off.\n");
        ASSERT_TRUE(HybridPdf::write(*doc, root / "field.pdf").ok);
    }
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    auto hits = index.search("wombat");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].file, root / "field.pdf");
    EXPECT_EQ(hits[0].firstPage, 1) << "the second page (after the page break)";
    EXPECT_EQ(index.search("quokka").size(), 1u);
}
