/*
 * xournal-qt: text documents as PDF, the app's side (qt/docs/md-pdf.md): what a new text document is (the setting,
 * its default by the way documents are kept), "Open as PDF document" of a .md, "Export as Markdown", and the
 * library's search in a PDF text document.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <shared_mutex>
#include <string>

#include <QDateTime>
#include <QImage>
#include <QTemporaryDir>
#include <zlib.h>
#include <QUrl>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "session/AppContext.h"
#include "session/DocumentImages.h"
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
#include "MarkdownImages.h"
#include "MdImages.h"

using namespace xqt;

namespace {
std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeFile(const fs::path& p, const std::string& bytes) { std::ofstream(p, std::ios::binary) << bytes; }
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
/// The XML of a .xopp
std::string gunzip(const fs::path& p) {
    gzFile in = gzopen(p.string().c_str(), "r");
    std::string out;
    char buf[65536];
    for (int n; in && (n = gzread(in, buf, sizeof buf)) > 0;) {
        out.append(buf, static_cast<size_t>(n));
    }
    if (in) {
        gzclose(in);
    }
    return out;
}
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

// "Open as PDF document": a new PDF text document next to the .md, which is not touched
TEST_F(TextPdf, openAsPdfDocumentMakesAPdfAndLeavesTheMarkdownFile) {
    writeFile(root / "notes.md", TEXT);
    const auto before = fs::last_write_time(root / "notes.md");
    AppController c;
    c.setLibraryRoot(root);
    ASSERT_TRUE(c.openPath(qstr(root / "notes.md")));
    DocumentSession* md = &current(c);
    ASSERT_TRUE(c.openAsPdfDocument());
    ASSERT_EQ(c.tabManager().count(), 2);
    DocumentSession& pdf = current(c);
    ASSERT_NE(&pdf, md);
    EXPECT_EQ(pdf.getFilePath(), root / "notes.pdf");
    EXPECT_TRUE(pdf.isHybrid());
    EXPECT_FALSE(pdf.isModified());
    EXPECT_TRUE(c.textNotes());
    EXPECT_EQ(flowOf(pdf), TEXT);
    EXPECT_EQ(attachment(root / "notes.pdf", "notes.md"), TEXT);
    EXPECT_GE(pdf.getDocument()->getPageCount(), 2u) << "the page break";
    EXPECT_EQ(bytesOf(root / "notes.md"), TEXT);
    EXPECT_EQ(fs::last_write_time(root / "notes.md"), before);
    EXPECT_FALSE(c.openAsPdfDocument()) << "only from a .md";
    // Unsaved changes of the .md go along (the .md stays as it is on disk); the name is taken: "notes (2).pdf"
    c.tabManager().setCurrentIndex(c.tabManager().indexOf(md));
    MarkdownFile::setText(*md, "# Changed\n");
    ASSERT_TRUE(c.openAsPdfDocument());
    EXPECT_EQ(current(c).getFilePath(), root / "notes (2).pdf");
    EXPECT_EQ(flowOf(current(c)), "# Changed\n");
    EXPECT_EQ(bytesOf(root / "notes.md"), TEXT);
    // The library: the .md and the PDFs are documents of their own
    EXPECT_EQ(DocumentFiles::scan(root).items.size(), 3u);
}

// "Export as Markdown": the text of the page texts; next to the document in Xournal++ files mode, else where asked
TEST_F(TextPdf, exportAsMarkdownWritesTheText) {
    AppController c;
    Choice choice(c);
    Settings& settings = *c.context().getSettings();
    c.setLibraryRoot(root);
    DocumentMode::store(settings, DocumentMode::Mode::Pdf);
    ASSERT_TRUE(c.createTextDocument("Report"));
    view(c)->endTextEditing();
    MarkdownFile::setText(current(c), TEXT);
    EXPECT_TRUE(c.hasMarkdownText());
    EXPECT_TRUE(c.markdownExportFile().isEmpty()) << "PDF files: the window asks where";
    EXPECT_EQ(c.suggestedMarkdownExport(), url(root / "Report.md"));
    fs::create_directories(root / "out");
    ASSERT_TRUE(c.exportMarkdown(url(root / "out" / "Report")));  // (".md" added)
    EXPECT_EQ(bytesOf(root / "out" / "Report.md"), TEXT) << "unsaved changes included, no continuation lines";

    // Xournal++ files: next to the document, without asking
    DocumentMode::store(settings, DocumentMode::Mode::Xopp);
    EXPECT_EQ(c.markdownExportFile(), url(root / "Report.md"));
    ASSERT_TRUE(c.exportMarkdown(c.markdownExportFile()));
    EXPECT_EQ(bytesOf(root / "Report.md"), TEXT);

    // Notes without a page's Markdown text: nothing to export
    c.newDocument();
    EXPECT_FALSE(c.hasMarkdownText());
    EXPECT_FALSE(c.textNotes());
}

// qt/docs/md-images.md: a PDF text document carries its pictures as attachments "name.assets/…" (nothing is written
// next to it); opened, they are in its work folder in the app cache; an incremental save adds a new one and keeps the
// ones it has; a full write drops those the text does not link to; Export as Markdown writes them next to the .md;
// Open as PDF document packs a .md's pictures.
// A .md whose name has blanks: the link to its picture is written as an address ("my%20notes.assets/…"), so that it
// resolves (the author: a link with the blanks as they are did not).
TEST_F(TextPdf, aPictureOfAMarkdownFileWithBlanksInItsNameIsLinkedSoItResolves) {
    AppController c;
    c.setLibraryRoot(root);
    const fs::path md = root / "my notes (draft).md";
    std::ofstream(md) << "# Notes\n";
    ASSERT_TRUE(c.openPath(QString::fromStdString(md.string())));
    QImage green(30, 10, QImage::Format_RGB32);
    green.fill(Qt::green);
    QString error;
    const auto link = MarkdownImages::savePicture(current(c), green, error, QDateTime(QDate(2026, 9, 26), QTime(8, 0)));
    ASSERT_TRUE(link) << error.toStdString();
    EXPECT_EQ(*link, "my%20notes%20%28draft%29.assets/image-2026-09-26-080000.png");
    EXPECT_TRUE(fs::exists(root / "my notes (draft).assets" / "image-2026-09-26-080000.png"));
    const std::string found = md::images::resolve(*link);
    ASSERT_FALSE(found.empty()) << "the link resolves";
    EXPECT_TRUE(fs::equivalent(fs::path(std::u8string(found.begin(), found.end())),
                               root / "my notes (draft).assets" / "image-2026-09-26-080000.png"));
}

TEST_F(TextPdf, picturesAreCarriedInsideAPdfTextDocument) {
    AppController c;
    Choice choice(c);
    c.setLibraryRoot(root);
    DocumentMode::store(*c.context().getSettings(), DocumentMode::Mode::Pdf);
    ASSERT_TRUE(c.createTextDocument("Report"));
    view(c)->endTextEditing();
    const fs::path pdf = root / "Report.pdf";
    // A pasted picture: into the work folder, linked as Report.assets/…
    QImage red(40, 20, QImage::Format_RGB32);
    red.fill(Qt::red);
    QString error;
    const auto first = MarkdownImages::savePicture(current(c), red, error, QDateTime(QDate(2026, 9, 26), QTime(10, 11, 12)));
    ASSERT_TRUE(first) << error.toStdString();
    EXPECT_EQ(*first, "Report.assets/image-2026-09-26-101112.png");
    EXPECT_TRUE(fs::exists(DocumentImages::workFolder(pdf) / "Report.assets" / "image-2026-09-26-101112.png"));
    MarkdownFile::setText(current(c), "# Report\n\n![](" + *first + ")\n");
    ASSERT_TRUE(c.save());
    EXPECT_FALSE(fs::exists(root / "Report.assets")) << "nothing next to the PDF";
    const std::string png = bytesOf(DocumentImages::workFolder(pdf) / "Report.assets" / "image-2026-09-26-101112.png");
    EXPECT_EQ(attachment(pdf, "Report.assets/image-2026-09-26-101112.png"), png);
    EXPECT_EQ(attachment(pdf, "Report.md"), "# Report\n\n![](Report.assets/image-2026-09-26-101112.png)\n");

    // Opened again (its work folder gone: it comes from the PDF): the picture is found
    c.closeTab(c.tabManager().currentIndex());
    fs::remove_all(DocumentImages::workFolder(pdf));
    {
        auto loaded = DocumentSession::loadFile(pdf);
        ASSERT_TRUE(loaded.document);
        EXPECT_EQ(md::images::resolve(*first),
                  (DocumentImages::workFolder(pdf) / "Report.assets" / "image-2026-09-26-101112.png").string());
    }
    ASSERT_TRUE(c.openPath(qstr(pdf)));
    EXPECT_FALSE(md::images::resolve(*first).empty());

    // A second picture: an incremental save adds it, the first one stays as it was (the file is small: a picture
    // would make it grow by more than the share that writes it anew)
    const double compactAbove = HybridPdf::compactAbove;
    HybridPdf::compactAbove = 100;
    struct Restore {
        double v;
        ~Restore() { HybridPdf::compactAbove = v; }
    } restore{compactAbove};
    const auto second = MarkdownImages::savePicture(current(c), red, error, QDateTime(QDate(2026, 9, 26), QTime(10, 11, 13)));
    ASSERT_TRUE(second);
    MarkdownFile::setText(current(c), "# Report\n\n![](" + *first + ")\n\n![](" + *second + ")\n");
    const auto sizeBefore = fs::file_size(pdf);
    DocumentSession::SaveRequest save;
    const auto r = current(c).saveNow(save);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental) << "a new picture is appended";
    EXPECT_LT(fs::file_size(pdf) - sizeBefore, 2 * png.size() + 20000) << "the first one is not written again";
    EXPECT_EQ(attachment(pdf, *second), png);
    EXPECT_EQ(attachment(pdf, *first), png);

    // The first one no longer linked: kept by an incremental save, dropped by a full write
    MarkdownFile::setText(current(c), "# Report\n\n![](" + *second + ")\n");
    ASSERT_TRUE(current(c).saveNow(save).ok);
    EXPECT_EQ(attachment(pdf, *first), png);
    save.compact = true;
    ASSERT_TRUE(current(c).saveNow(save).ok);
    EXPECT_EQ(attachment(pdf, *first), "<none>");
    EXPECT_EQ(attachment(pdf, *second), png);
    EXPECT_FALSE(HybridPdf::hasEarlierRevisions(pdf));
    // (the clean copy the document shows its pages from never carries them)

    // Export as Markdown: the text and Report.assets/ next to it
    fs::create_directories(root / "out");
    ASSERT_TRUE(c.exportMarkdown(url(root / "out" / "Report.md")));
    EXPECT_EQ(bytesOf(root / "out" / "Report.md"), "# Report\n\n![](" + *second + ")\n");
    EXPECT_EQ(bytesOf(root / "out" / "Report.assets" / "image-2026-09-26-101113.png"), png);
    // Under another name: the links follow the name of its folder
    ASSERT_TRUE(c.exportMarkdown(url(root / "out" / "Other name.md")));
    EXPECT_EQ(bytesOf(root / "out" / "Other name.md"), "# Report\n\n![](Other%20name.assets/image-2026-09-26-101113.png)\n");
    EXPECT_TRUE(fs::exists(root / "out" / "Other name.assets" / "image-2026-09-26-101113.png"));

    // Open as PDF document of a .md with a picture: the PDF carries it
    writeFile(root / "notes.md", "# Notes\n\n![](notes.assets/a.png)\n");
    fs::create_directories(root / "notes.assets");
    ASSERT_TRUE(red.save(qstr(root / "notes.assets" / "a.png")));
    ASSERT_TRUE(c.openPath(qstr(root / "notes.md")));
    ASSERT_TRUE(c.openAsPdfDocument());
    EXPECT_EQ(attachment(root / "notes.pdf", "notes.assets/a.png"), bytesOf(root / "notes.assets" / "a.png"));
}

// qt/docs/md-images.md: the Markdown of notes (.xopp) carries its pictures inside the .xopp, as extra <preview xqt-file>
// elements at the end (Xournal++ ignores them); a .xopp without pictures is written as before; opened again, they
// are in its work folder; a PDF with notes made from it carries them as attachments.
TEST_F(TextPdf, picturesOfNotesAreCarriedInsideTheXopp) {
    AppController c;
    c.setLibraryRoot(root);
    c.newDocument();
    const fs::path xopp = root / "lecture.xopp";
    ASSERT_TRUE(current(c).saveAs(xopp).ok);
    const std::string before = gunzip(xopp);
    EXPECT_EQ(before.find("xqt-file"), std::string::npos) << "no pictures: as upstream writes it";

    // A picture pasted into the page's Markdown text of the saved notes: kept in its work folder until saved
    QImage blue(30, 10, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    QString error;
    const auto link = MarkdownImages::savePicture(current(c), blue, error, QDateTime(QDate(2026, 9, 26), QTime(9, 0)));
    ASSERT_TRUE(link) << error.toStdString();
    EXPECT_EQ(*link, "lecture.assets/image-2026-09-26-090000.png");
    MarkdownFile::setText(current(c), "# Lecture\n\n![](" + *link + ")\n");
    ASSERT_TRUE(current(c).save().ok);
    EXPECT_FALSE(fs::exists(root / "lecture.assets")) << "nothing next to the .xopp";
    const std::string xml = gunzip(xopp);
    EXPECT_NE(xml.find("<preview xqt-file=\"lecture.assets/image-2026-09-26-090000.png\">"), std::string::npos);
    EXPECT_LT(xml.find("<preview>"), xml.find("<preview xqt-file")) << "the document's own preview first";
    if (const char* keep = std::getenv("XQT_KEEP_XOPP")) {  // (to open it in Xournal++: qt/docs/md-images.md)
        fs::copy_file(xopp, keep, fs::copy_options::overwrite_existing);
    }
    const auto carried = DocumentImages::xoppPictures(xopp);
    ASSERT_EQ(carried.size(), 1u);
    EXPECT_EQ(carried[0].second,
              bytesOf(DocumentImages::workFolder(xopp) / "lecture.assets" / "image-2026-09-26-090000.png"));

    // Opened again, its work folder gone: the picture comes from the file
    c.closeTab(c.tabManager().currentIndex());
    fs::remove_all(DocumentImages::workFolder(xopp));
    {
        auto loaded = DocumentSession::loadFile(xopp);
        ASSERT_TRUE(loaded.document);
        EXPECT_EQ(md::images::info(*link).state, md::images::Info::State::Ok);
        EXPECT_EQ(md::images::info(*link).width, 30);
    }
    // Moved in the library (not open): the picture is still in it
    fs::create_directories(root / "Physics");
    auto moved = DocumentFiles::move(DocumentFiles::itemOf(xopp), root / "Physics");
    ASSERT_TRUE(moved.ok) << moved.error;
    EXPECT_EQ(DocumentImages::xoppPictures(root / "Physics" / "lecture.xopp").size(), 1u);
    // Saved as a PDF with notes: an attachment
    ASSERT_TRUE(c.openPath(qstr(root / "Physics" / "lecture.xopp")));
    ASSERT_TRUE(current(c).saveAsHybrid(root / "Physics" / "lecture.pdf").ok);
    EXPECT_EQ(attachment(root / "Physics" / "lecture.pdf", *link), carried[0].second);
}

// The work folders in the app cache have an owner (qt/docs/md-images.md): those not used for 60 days go at start; a
// document opened marks its own as used.
TEST_F(TextPdf, oldWorkFoldersArePruned) {
    const fs::path used = DocumentImages::workFolder(root / "used.xopp");
    const fs::path old = DocumentImages::workFolder(root / "old.xopp");
    fs::create_directories(used / "used.assets");
    fs::create_directories(old / "old.assets");
    std::ofstream(old / "old.assets" / "a.png") << "x";
    fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(24 * 61));
    fs::last_write_time(used, fs::file_time_type::clock::now() - std::chrono::hours(24 * 61));
    DocumentImages::touchWorkFolder(root / "used.xopp");
    EXPECT_GE(DocumentImages::pruneWorkFolders(), 1u);
    EXPECT_FALSE(fs::exists(old));
    EXPECT_TRUE(fs::exists(used / "used.assets"));
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
