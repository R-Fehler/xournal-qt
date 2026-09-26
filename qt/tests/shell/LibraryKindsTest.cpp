/*
 * xournal-qt: the kinds of PDFs in the library (qt/docs/library.md, "Kinds of PDFs"): the index keeps what each PDF
 * is (plain, with notes, a text document, an archive PDF) from the read it does anyway, the cards and the "Show"
 * filter take it from there (no PDF is looked into on the UI thread), and saving in the app keeps it current.
 *
 * XQT_BENCH_KINDS=<n> [XQT_BENCH_PDF=<pdf>] xqt-shell-tests --gtest_filter='LibraryKindsTest.bench*' measures the
 * filter on a library of n PDFs (copies of that PDF, else small generated ones).
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>

#include <QCborMap>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryModel.h"
#include "shell/RecentFiles.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "MarkdownFile.h"

using namespace xqt;

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

bool waitFor(const std::function<bool()>& done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

/// A one-page PDF with a word on it.
void makePlainPdf(const fs::path& p, const char* word = "lecture") {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    cairo_move_to(cr, 72, 100);
    cairo_show_text(cr, word);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// A PDF with notes: a page with a stroke, written as a hybrid PDF.
void makeNotesPdf(const fs::path& p) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(595, 842);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->addPoint(Point(100, 100, 1.0));
    stroke->addPoint(Point(200, 180, 1.0));
    page->getSelectedLayer()->addElement(std::move(stroke));
    doc.addPage(page);
    ASSERT_TRUE(HybridPdf::write(doc, p).ok);
}

const std::string TEXT = "# Field notes\n\nA *quokka* smiled.\n";

/// A PDF text document: page 1 starts the page's Markdown text (it carries "name.md").
void makeTextPdf(const fs::path& p) {
    QTemporaryDir config;
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(config.filePath("settings.xml").toStdString()), 1);
    auto doc = MarkdownFile::notesDocument(TEXT);
    ASSERT_TRUE(HybridPdf::write(*doc, p).ok);
}

/// An archive PDF (PDF/A-3 with its notes).
void makeArchivePdf(const fs::path& p) {
    DocumentHandler handler;
    Document doc(&handler);
    doc.addPage(std::make_shared<XojPage>(595, 842));
    ASSERT_TRUE(HybridPdf::writeArchive(doc, p).ok);
}

/// A time of their own for the files: what this process remembers about them (HybridPdf's markers, by path, size and
/// time) does not count.
void freshStamps(const std::vector<fs::path>& files) {
    static int shift = 0;
    for (const auto& f: files) {
        fs::last_write_time(f, fs::file_time_type::clock::now() - std::chrono::hours(2) - std::chrono::seconds(++shift));
    }
}

/// A stroke on a page, through the undo stack (the document is changed).
void drawStroke(DocumentSession& s, size_t pageNo) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->addPoint(Point(100, 100, 1.0));
    stroke->addPoint(Point(200, 180, 3.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

class LibraryKindsTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    /// plain.pdf, notes.pdf, text.pdf, archive.pdf and a Markdown file, with fresh stamps
    void fill() {
        makePlainPdf(root / "plain.pdf");
        makeNotesPdf(root / "notes.pdf");
        makeTextPdf(root / "text.pdf");
        makeArchivePdf(root / "archive.pdf");
        std::ofstream(root / "readme.md") << "# Read me\n";
        freshStamps({root / "plain.pdf", root / "notes.pdf", root / "text.pdf", root / "archive.pdf"});
    }
    void expectKinds(const LibraryIndex& index) {
        EXPECT_EQ(index.pdfKind(root / "plain.pdf"), PdfKind::Plain);
        EXPECT_EQ(index.pdfKind(root / "notes.pdf"), PdfKind::Notes);
        EXPECT_EQ(index.pdfKind(root / "text.pdf"), PdfKind::Text);
        EXPECT_EQ(index.pdfKind(root / "archive.pdf"), PdfKind::Archive);
        EXPECT_EQ(index.pdfKind(root / "readme.md"), PdfKind::Unknown) << "not a PDF";
    }
    void index(LibraryIndex& idx) {
        idx.update(DocumentFiles::scanRecursive(root));
        idx.waitForDone();
    }
    QTemporaryDir tmp;
    fs::path root;
};

/// The rows of a library model by name, and what each says about its PDF
std::map<std::string, std::string> kindsOf(const LibraryModel& m) {
    std::map<std::string, std::string> out;
    for (int i = 0; i < m.count(); ++i) {
        out[m.data(m.index(i), LibraryModel::NameRole).toString().toStdString()] =
                m.data(m.index(i), LibraryModel::PdfKindRole).toString().toStdString();
    }
    return out;
}
}  // namespace

// The index finds out what each PDF is while it reads it anyway (one read of its marker, when it is opened), keeps it
// in notes.pack, and reads only that for entries written before kinds were kept.
TEST_F(LibraryKindsTest, theIndexKeepsWhatEachPdfIs) {
    fill();
    {
        const int markers = HybridPdf::markerReads();
        LibraryIndex idx(root);
        index(idx);
        expectKinds(idx);
        EXPECT_EQ(HybridPdf::markerReads() - markers, 4) << "each PDF's marker once (to open it), not again for its kind";
        idx.flush();
    }
    // Read back: nothing is read again
    {
        const int markers = HybridPdf::markerReads();
        LibraryIndex again(root);
        index(again);
        EXPECT_EQ(again.documentsRead(), 0);
        EXPECT_EQ(again.pdfKindsRead(), 0);
        EXPECT_EQ(HybridPdf::markerReads(), markers);
        expectKinds(again);
    }
    // Entries written before kinds were kept: only their kind is read (the marker), once
    const fs::path cache = root / DocumentFiles::META_DIR;
    auto notes = Packs::read(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
    ASSERT_TRUE(notes.has_value());
    int withKind = 0;
    for (auto it = notes->begin(); it != notes->end(); ++it) {
        QCborMap entry = it.value().toMap();
        withKind += entry.contains(QStringLiteral("pdfKind"));
        entry.remove(QStringLiteral("pdfKind"));
        it.value() = entry;
    }
    EXPECT_EQ(withKind, 4) << "the PDFs, not the Markdown file";
    ASSERT_TRUE(Packs::write(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT, *notes, true));
    {
        LibraryIndex old(root);
        EXPECT_EQ(old.pdfKind(root / "text.pdf"), PdfKind::Unknown) << "(not read yet)";
        index(old);
        EXPECT_EQ(old.pdfKindsRead(), 4);
        EXPECT_EQ(old.documentsRead(), 0) << "not the documents";
        EXPECT_EQ(old.pdfPagesRead(), 0);
        EXPECT_EQ(old.titlesRead(), 0);
        expectKinds(old);
        old.flush();
    }
    LibraryIndex later(root);
    index(later);
    EXPECT_EQ(later.pdfKindsRead(), 0) << "once";
    expectKinds(later);

    // Changed by another program: read again through its stamp (a text document that became a plain PDF)
    fs::remove(root / "text.pdf");
    makePlainPdf(root / "text.pdf", "flattened");
    index(later);
    EXPECT_EQ(later.pdfKind(root / "text.pdf"), PdfKind::Plain);
}

// The cards tell the kinds apart, and the "Show" filter takes them from the index: nothing is looked into on the UI
// thread, a PDF not indexed yet is shown once the index knows it.
TEST_F(LibraryKindsTest, theCardsAndTheFilterTakeTheKindsFromTheIndex) {
    fill();
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    ASSERT_TRUE(waitFor([&] { return !model.indexing() && kindsOf(model)["text"] == "text"; }));
    EXPECT_EQ(kindsOf(model), (std::map<std::string, std::string>{{"archive", "archive"},
                                                                   {"notes", "notes"},
                                                                   {"plain", "plain"},
                                                                   {"readme", ""},
                                                                   {"text", "text"}}));

    const int markers = HybridPdf::markerReads();
    model.setShown("onlyPdfsWithNotes", true);
    EXPECT_EQ(kindsOf(model), (std::map<std::string, std::string>{
                                      {"archive", "archive"}, {"notes", "notes"}, {"readme", ""}, {"text", "text"}}));
    model.setShown("onlyTextDocuments", true);
    EXPECT_EQ(kindsOf(model), (std::map<std::string, std::string>{{"readme", ""}, {"text", "text"}}));
    model.setShown("onlyPdfsWithNotes", false);
    EXPECT_EQ(kindsOf(model), (std::map<std::string, std::string>{{"readme", ""}, {"text", "text"}}));
    model.setFlat(true);
    EXPECT_EQ(model.count(), 2);
    EXPECT_EQ(HybridPdf::markerReads(), markers) << "no PDF looked into by the filter";
    EXPECT_TRUE(model.show()["onlyTextDocuments"].toBool());

    // Another library opened with the filter on: its text documents appear as the index finds them
    QTemporaryDir other;
    const fs::path second(other.path().toStdString());
    makeTextPdf(second / "memo.pdf");
    makePlainPdf(second / "scan.pdf");
    Library(second).setShowFilter([] {
        ShowFilter f;
        f.onlyTextDocuments = true;
        return f;
    }());
    LibraryModel fresh;
    fresh.setLibrary(std::make_unique<Library>(second));
    EXPECT_TRUE(waitFor([&] { return fresh.rowOf(qstr(second / "memo.pdf")) >= 0; }, 10000));
    EXPECT_LT(fresh.rowOf(qstr(second / "scan.pdf")), 0);
}

// Saving in the app keeps the kind current: a PDF annotated and saved becomes a PDF with notes, a new text document
// and "Open as PDF document" are text documents (the cards of the library and of Recent).
TEST_F(LibraryKindsTest, savingInTheAppUpdatesTheKind) {
    makePlainPdf(root / "lecture.pdf");
    std::ofstream(root / "draft.md") << TEXT;
    AppController c;
    Settings& settings = *c.context().getSettings();
    struct Reset {
        Settings& s;
        ~Reset() {
            DocumentMode::store(s, DocumentMode::Mode::Unset);
            s.getCustomElement("xournalQt").setString("newTextDocuments", "");
            s.customSettingsChanged();
        }
    } reset{settings};
    DocumentMode::store(settings, DocumentMode::Mode::Pdf);
    settings.getCustomElement("xournalQt").setBool("pdfOnlyIntoPdfNoticed", true);  // (no notice)
    c.setLibraryRoot(root);
    auto* model = qobject_cast<LibraryModel*>(c.libraryModel());
    ASSERT_NE(model, nullptr);
    auto kindOf = [&](const char* file) {
        const int row = model->rowOf(qstr(root / file));
        return row < 0 ? std::string("<none>")
                       : model->data(model->index(row), LibraryModel::PdfKindRole).toString().toStdString();
    };
    ASSERT_TRUE(waitFor([&] { return kindOf("lecture.pdf") == "plain"; }));

    // Annotated and saved: into the PDF, which is a PDF with notes now
    ASSERT_TRUE(c.openPath(qstr(root / "lecture.pdf")));
    drawStroke(*c.tabManager().currentSession(), 0);
    ASSERT_TRUE(c.save());
    EXPECT_TRUE(waitFor([&] { return kindOf("lecture.pdf") == "notes"; })) << kindOf("lecture.pdf");

    // A new text document
    ASSERT_TRUE(c.createTextDocument("Report"));
    EXPECT_TRUE(waitFor([&] { return kindOf("Report.pdf") == "text"; })) << kindOf("Report.pdf");

    // "Open as PDF document" of a .md
    ASSERT_TRUE(c.openPath(qstr(root / "draft.md")));
    ASSERT_TRUE(c.openAsPdfDocument());
    EXPECT_TRUE(waitFor([&] { return kindOf("draft.pdf") == "text"; })) << kindOf("draft.pdf");

    // The Recent cards say the same
    auto* recent = qobject_cast<RecentFiles*>(c.recentModel());
    ASSERT_NE(recent, nullptr);
    std::map<std::string, std::string> recentKinds;
    EXPECT_TRUE(waitFor([&] {
        recentKinds.clear();
        for (int i = 0; i < recent->count(); ++i) {
            const QModelIndex at = recent->index(i);
            recentKinds[fs::path(recent->data(at, RecentFiles::PathRole).toString().toStdString()).filename().string()] =
                    recent->data(at, RecentFiles::PdfKindRole).toString().toStdString();
        }
        return recentKinds["draft.pdf"] == "text" && recentKinds["lecture.pdf"] == "notes";
    }));
    EXPECT_EQ(recentKinds["draft.md"], "");
}

// The filter on a library of many PDFs: before, "Only PDFs with notes" looked into each lone PDF on the UI thread
// (qpdf, remembered per file version); now it looks the kinds up in the index.
TEST_F(LibraryKindsTest, benchTheFilterOnManyPdfs) {
    const int n = qEnvironmentVariableIntValue("XQT_BENCH_KINDS");
    if (n <= 0) {
        GTEST_SKIP() << "XQT_BENCH_KINDS=<number of PDFs> (XQT_BENCH_PDF=<a PDF to copy>)";
    }
    const QString sample = qEnvironmentVariable("XQT_BENCH_PDF");
    std::vector<fs::path> pdfs;
    for (int i = 0; i < n; ++i) {
        const fs::path p = root / ("Folder " + std::to_string(i % 5)) / ("paper " + std::to_string(i) + ".pdf");
        fs::create_directories(p.parent_path());
        if (i % 4 == 1) {
            makeNotesPdf(p);
        } else if (i % 8 == 3) {
            makeTextPdf(p);
        } else if (!sample.isEmpty()) {
            fs::copy_file(fs::path(sample.toStdString()), p);
        } else {
            makePlainPdf(p, "paper");
        }
        pdfs.push_back(p);
    }
    freshStamps(pdfs);
    const auto items = DocumentFiles::scanRecursive(root);
    auto ms = [](const QElapsedTimer& t) { return static_cast<double>(t.nsecsElapsed()) / 1e6; };

    // Before: each lone PDF looked into (the first listing reads every marker, later ones find them remembered)
    QElapsedTimer t;
    t.start();
    int hybrids = 0;
    for (const auto& item: items) {
        hybrids += HybridPdf::isHybrid(item.pdf);
    }
    const double coldBefore = ms(t);
    t.restart();
    for (const auto& item: items) {
        HybridPdf::isHybrid(item.pdf);
    }
    const double warmBefore = ms(t);

    // Now: from the index
    freshStamps(pdfs);  // (the index reads them anew)
    LibraryModel model;
    t.restart();
    model.setLibrary(std::make_unique<Library>(root));
    model.setFlat(true);
    ASSERT_TRUE(waitFor([&] { return !model.indexing(); }, 600000));
    const double indexing = ms(t);
    const int markers = HybridPdf::markerReads();
    t.restart();
    model.setShown("onlyPdfsWithNotes", true);
    const double filterOn = ms(t);
    t.restart();
    model.setShown("onlyTextDocuments", true);
    const double textOnly = ms(t);
    t.restart();
    model.refresh();
    const double relist = ms(t);
    EXPECT_EQ(HybridPdf::markerReads(), markers);
    model.setShown("onlyTextDocuments", false);
    EXPECT_EQ(model.count(), hybrids);

    // Entries from before kinds were kept: the kind alone, per PDF (a marker read, cold)
    freshStamps(pdfs);
    t.restart();
    for (const auto& p: pdfs) {
        HybridPdf::markerOf(p);
    }
    const double kindAlone = ms(t) / n;

    std::printf("library kinds, %d PDFs (%d with notes)%s:\n"
                "  before: the filter looks into each lone PDF on the UI thread: %.1f ms the first time, %.1f ms later\n"
                "  now:    indexing all %.0f ms (in the background); \"Only PDFs with notes\" %.1f ms, text documents "
                "%.1f ms, the listing again %.1f ms (no PDF looked into)\n"
                "  the kind alone for an old entry: %.2f ms per PDF\n",
                n, hybrids, sample.isEmpty() ? "" : (" copies of " + sample).toUtf8().constData(), coldBefore,
                warmBefore, indexing, filterOn, textOnly, relist, kindAlone);
}
