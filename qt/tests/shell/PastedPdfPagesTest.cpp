/*
 * xournal-qt: PDF pages pasted from another PDF stay PDF pages (their text searchable and selectable): they go into
 * the document's merged background PDF (MergedPdf.h), next to its .xopp.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/TabManager.h"

#include "AppController.h"

using namespace xqt;

namespace {
/// A PDF with one page per word, each word as real text.
void makeTextPdf(const fs::path& p, const std::vector<std::string>& words) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    for (const auto& w: words) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, w.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// "<name>.xopp" annotating `pdf` (as upstream saves it).
void annotate(const fs::path& pdf, const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, xopp).ok);
}

/// Whether the PDF page the document's page shows has this text (the search's own function).
bool pageHasText(DocumentSession& s, size_t page, const char* text) {
    return !DocumentSearch::findOnPage(*s.getDocument(), page, text).empty();
}

fs::path backgroundOf(DocumentSession& s) { return s.getDocument()->getPdfFilepath(); }

/// The .pdf files in a folder (hidden ones too).
std::vector<std::string> pdfsIn(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e: fs::directory_iterator(dir)) {
        if (e.path().extension() == ".pdf") {
            out.push_back(e.path().filename().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

class PastedPdfPages: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        makeTextPdf(root / "lecture.pdf", {"lectureone", "lecturetwo", "lecturethree"});
        makeTextPdf(root / "other.pdf", {"pastedalpha", "pastedbeta"});
        makeTextPdf(root / "third.pdf", {"pastedgamma"});
    }
    bool open(AppController& c, const fs::path& p) { return c.openPath(QString::fromStdString(p.string())); }
    DocumentSession& current(AppController& c) { return *c.tabManager().currentSession(); }
    /// A document without PDF, saved as `xopp`.
    void newSaved(AppController& c, const fs::path& xopp) {
        c.newDocument();
        ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(QString::fromStdString(xopp.string()))));
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(PastedPdfPages, stayPdfPagesWithTheirTextInAHiddenSidecar) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    const std::string original = bytesOf(root / "lecture.pdf");
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({1});  // "pastedbeta"
    ASSERT_TRUE(open(c, root / "lecture.xopp"));
    DocumentSession& s = current(c);
    QSignalSpy notes(&c, &AppController::pageActionDone);
    ASSERT_EQ(c.pastePages(1), 1);

    const PageRef pasted = s.getDocument()->getPage(1);
    ASSERT_TRUE(pasted->getBackgroundType().isPdfPage()) << "a PDF page, not an image";
    EXPECT_EQ(pasted->getPdfPageNr(), 3u) << "after the three pages of the lecture";
    EXPECT_EQ(backgroundOf(s), root / ".lecture.pages.pdf");
    EXPECT_TRUE(fs::exists(root / ".lecture.pages.pdf"));
    EXPECT_EQ(bytesOf(root / "lecture.pdf"), original) << "the lecture's PDF is never changed";
    ASSERT_EQ(notes.count(), 1);
    EXPECT_TRUE(notes.first().at(0).toString().contains(".lecture.pages.pdf")) << notes.first().at(0).toString().toStdString();

    // Its text: searched and selected like any PDF page; the other pages show what they showed.
    EXPECT_TRUE(pageHasText(s, 1, "pastedbeta"));
    EXPECT_TRUE(pageHasText(s, 0, "lectureone"));
    EXPECT_TRUE(pageHasText(s, 2, "lecturetwo"));
    s.search().setQuery("pastedbeta");
    QElapsedTimer t;
    t.start();
    while (s.search().isRunning() && t.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    ASSERT_EQ(s.search().hits().size(), 1u);
    EXPECT_EQ(s.search().hits()[0].page, 1u);
    const std::string selected = s.getDocument()->getPdfPage(pasted->getPdfPageNr())->selectText(
            XojPdfRectangle(0, 0, pasted->getWidth(), pasted->getHeight()), XojPdfPageSelectionStyle::Area);
    EXPECT_NE(selected.find("pastedbeta"), std::string::npos) << selected;

    // Undo and redo as before (the page keeps its number)
    c.undoPages();
    EXPECT_EQ(s.getDocument()->getPageCount(), 3u);
    c.redoPages();
    ASSERT_EQ(s.getDocument()->getPageCount(), 4u);
    EXPECT_TRUE(pageHasText(s, 1, "pastedbeta"));
}

TEST_F(PastedPdfPages, withinTheSamePdfTheyReferToItsPages) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.xopp"));
    DocumentSession& s = current(c);
    c.copyPages({2});
    ASSERT_EQ(c.pastePages(0), 1);
    EXPECT_EQ(s.getDocument()->getPage(0)->getPdfPageNr(), 2u);
    EXPECT_EQ(backgroundOf(s), root / "lecture.pdf");
    EXPECT_FALSE(fs::exists(root / ".lecture.pages.pdf")) << "no merged PDF needed";

    // Also after the document took pages from another PDF: its own pages keep their numbers in the merged PDF
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({0});
    c.tabManager().setCurrentIndex(0);
    ASSERT_EQ(&current(c), &s);
    ASSERT_EQ(c.pastePages(0), 1);
    ASSERT_EQ(backgroundOf(s), root / ".lecture.pages.pdf");
    c.copyPages({4});  // "lecturethree": pastedalpha, lecturethree, lectureone, lecturetwo, lecturethree
    ASSERT_EQ(c.pastePages(0), 1);
    EXPECT_EQ(s.getDocument()->getPage(0)->getPdfPageNr(), 2u);
    EXPECT_TRUE(pageHasText(s, 0, "lecturethree"));
    EXPECT_EQ(s.getDocument()->getPdfPageCount(), 4u) << "nothing added twice";
}

TEST_F(PastedPdfPages, aDocumentWithoutPdfGetsItsOwnPairedPdf) {
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({0, 1});
    newSaved(c, root / "notes.xopp");
    DocumentSession& s = current(c);
    ASSERT_EQ(c.pastePages(1), 2);
    EXPECT_EQ(backgroundOf(s), root / "notes.pdf");
    EXPECT_EQ(MergedPdf::kindOf(root / "notes.pdf"), MergedPdf::Kind::Own);
    EXPECT_EQ(DocumentFiles::itemOf(root / "notes.xopp").pdf, root / "notes.pdf") << "one document in the library";
    EXPECT_TRUE(pageHasText(s, 1, "pastedalpha"));
    EXPECT_TRUE(pageHasText(s, 2, "pastedbeta"));

    // "busy.pdf" is somebody else's: the pages go into the hidden sidecar
    makeTextPdf(root / "busy.pdf", {"somethingelse"});
    const std::string busy = bytesOf(root / "busy.pdf");
    newSaved(c, root / "busy.xopp");
    ASSERT_EQ(c.pastePages(0), 2);
    EXPECT_EQ(backgroundOf(current(c)), root / ".busy.pages.pdf");
    EXPECT_EQ(bytesOf(root / "busy.pdf"), busy);
    EXPECT_TRUE(pageHasText(current(c), 0, "pastedalpha"));
}

TEST_F(PastedPdfPages, severalPastesFromSeveralPdfsGoIntoOneFile) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    const auto before = pdfsIn(root);
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.xopp"));
    DocumentSession& s = current(c);
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({0});
    c.tabManager().setCurrentIndex(0);
    ASSERT_EQ(c.pastePages(3), 1);
    ASSERT_EQ(c.pastePages(4), 1);  // the same page again: it is in the PDF already
    ASSERT_TRUE(open(c, root / "third.pdf"));
    c.copyPages({0});
    c.tabManager().setCurrentIndex(0);
    ASSERT_EQ(c.pastePages(5), 1);

    auto expected = before;
    expected.push_back(".lecture.pages.pdf");
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(pdfsIn(root), expected) << "one merged PDF, nothing else";
    EXPECT_EQ(s.getDocument()->getPdfPageCount(), 5u);
    EXPECT_TRUE(pageHasText(s, 3, "pastedalpha"));
    EXPECT_TRUE(pageHasText(s, 4, "pastedalpha"));
    EXPECT_TRUE(pageHasText(s, 5, "pastedgamma"));
    EXPECT_TRUE(pageHasText(s, 0, "lectureone"));
}

TEST_F(PastedPdfPages, aDocumentNeverSavedKeepsThemInTheCache) {
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({1});
    c.newDocument();
    DocumentSession& blank = current(c);
    QSignalSpy notes(&c, &AppController::pageActionDone);
    ASSERT_EQ(c.pastePages(1), 1);
    EXPECT_TRUE(MergedPdf::inCache(backgroundOf(blank))) << backgroundOf(blank);
    EXPECT_TRUE(pageHasText(blank, 1, "pastedbeta"));
    EXPECT_TRUE(blank.documentFile().empty()) << "still a new document";
    EXPECT_EQ(blank.getDisplayName(), "Untitled");
    ASSERT_EQ(notes.count(), 1);
    EXPECT_TRUE(notes.first().at(0).toString().contains("saved next to the document"));

    // A PDF opened to annotate it, not saved yet: still that PDF's document
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    DocumentSession& annotated = current(c);
    ASSERT_EQ(c.pastePages(0), 1);
    EXPECT_TRUE(MergedPdf::inCache(backgroundOf(annotated)));
    EXPECT_EQ(annotated.documentFile(), root / "lecture.pdf");
    EXPECT_EQ(annotated.getDisplayName(), "lecture.pdf");
    EXPECT_EQ(annotated.suggestSavePath(), root / "lecture.xopp");
    EXPECT_TRUE(pageHasText(annotated, 0, "pastedbeta"));
    EXPECT_TRUE(pageHasText(annotated, 1, "lectureone"));

    // Closed without saving: the cached PDF goes too
    const fs::path cached = backgroundOf(annotated);
    c.closeTab(c.currentTab());
    EXPECT_FALSE(fs::exists(cached));
}

namespace {
/// The word each page of a saved .xopp shows from its PDF, loaded with upstream's loader (as Xournal++ opens it).
std::vector<std::string> wordsAsUpstreamLoadsThem(const fs::path& xopp, const std::vector<std::string>& words) {
    LoadHandler handler;
    auto doc = handler.loadDocument(xopp);
    EXPECT_TRUE(doc) << xopp;
    EXPECT_TRUE(handler.getMissingPdfFilename().empty());
    std::vector<std::string> out;
    for (size_t i = 0; doc && i < doc->getPageCount(); ++i) {
        std::string found;
        const PageRef page = doc->getPage(i);
        if (page->getBackgroundType().isPdfPage()) {
            if (auto pdf = doc->getPdfPage(page->getPdfPageNr())) {
                for (const auto& w: words) {
                    if (!pdf->findText(w).empty()) {
                        found = w;
                        break;
                    }
                }
            }
        }
        out.push_back(found);
    }
    return out;
}

const std::vector<std::string> WORDS{"lectureone", "lecturetwo", "lecturethree", "pastedalpha", "pastedbeta",
                                     "pastedgamma"};

size_t pdfPagesOf(const fs::path& pdf) {
    XojPdfDocument d;
    GError* e = nullptr;
    const bool ok = d.load(pdf, "", &e);
    if (e) {
        g_error_free(e);
    }
    return ok ? d.getPageCount() : 0;
}
}  // namespace

TEST_F(PastedPdfPages, savingDropsThePdfPagesNoLongerUsed) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    const std::string original = bytesOf(root / "lecture.pdf");
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({0, 1});
    ASSERT_TRUE(open(c, root / "lecture.xopp"));
    DocumentSession& s = current(c);
    ASSERT_EQ(c.pastePages(3), 2);  // lectureone, lecturetwo, lecturethree, pastedalpha, pastedbeta
    ASSERT_TRUE(c.save());
    const fs::path merged = root / ".lecture.pages.pdf";
    EXPECT_EQ(pdfPagesOf(merged), 5u);
    const auto time = fs::last_write_time(merged);
    ASSERT_TRUE(c.save());
    EXPECT_EQ(fs::last_write_time(merged), time) << "nothing changed: not written again";

    ASSERT_TRUE(c.deletePages({3}));  // pastedalpha
    ASSERT_TRUE(c.deletePages({1}));  // lecturetwo
    ASSERT_TRUE(c.save());
    EXPECT_EQ(pdfPagesOf(merged), 3u) << "only the pages that are used";
    EXPECT_EQ(s.getDocument()->getPdfPageCount(), 3u);
    EXPECT_EQ(s.getDocument()->getPage(2)->getPdfPageNr(), 2u) << "renumbered";
    EXPECT_TRUE(pageHasText(s, 0, "lectureone"));
    EXPECT_TRUE(pageHasText(s, 1, "lecturethree"));
    EXPECT_TRUE(pageHasText(s, 2, "pastedbeta"));
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "lecture.xopp", WORDS),
              (std::vector<std::string>{"lectureone", "lecturethree", "pastedbeta"}))
            << "the saved .xopp opens in Xournal++ with the right pages";
    EXPECT_EQ(bytesOf(root / "lecture.pdf"), original);

    // The deleted pages come back with undo, with their PDF pages (kept in memory when they were dropped)
    c.undoPages();
    EXPECT_TRUE(pageHasText(s, 1, "lecturetwo"));
    c.undoPages();
    EXPECT_TRUE(pageHasText(s, 0, "lectureone"));
    EXPECT_TRUE(pageHasText(s, 3, "pastedalpha"));
    EXPECT_TRUE(pageHasText(s, 4, "pastedbeta"));
    ASSERT_TRUE(c.save());
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "lecture.xopp", WORDS),
              (std::vector<std::string>{"lectureone", "lecturetwo", "lecturethree", "pastedalpha", "pastedbeta"}));
    EXPECT_EQ(pdfPagesOf(merged), 5u);
}

TEST_F(PastedPdfPages, anUndoneBackgroundChangeShowsItsPdfPageAfterASave) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({0});
    ASSERT_TRUE(open(c, root / "lecture.xopp"));
    DocumentSession& s = current(c);
    ASSERT_EQ(c.pastePages(3), 1);
    c.copyPages({2});
    ASSERT_EQ(c.pastePages(3), 1);  // lectureone, lecturetwo, lecturethree, lecturethree, pastedalpha
    // A pattern on the first, the second "lecturethree" and the pasted page: their PDF pages are not shown now
    ASSERT_TRUE(c.changePageBackground({0, 3, 4}, 0));
    ASSERT_FALSE(s.getDocument()->getPage(4)->getBackgroundType().isPdfPage());
    ASSERT_TRUE(c.save());
    EXPECT_EQ(s.getDocument()->getPdfPageCount(), 2u) << "lecturetwo and lecturethree";
    c.undoPages();  // (the undo sets the numbers the pages had before the save)
    EXPECT_TRUE(pageHasText(s, 0, "lectureone"));
    EXPECT_TRUE(pageHasText(s, 1, "lecturetwo"));
    EXPECT_TRUE(pageHasText(s, 2, "lecturethree"));
    EXPECT_TRUE(pageHasText(s, 3, "lecturethree"));
    EXPECT_TRUE(pageHasText(s, 4, "pastedalpha"));
    ASSERT_TRUE(c.save());
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "lecture.xopp", WORDS),
              (std::vector<std::string>{"lectureone", "lecturetwo", "lecturethree", "lecturethree", "pastedalpha"}));
}

TEST_F(PastedPdfPages, theFirstSavePutsThemNextToTheDocument) {
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({1});
    c.newDocument();
    ASSERT_EQ(c.pastePages(1), 1);
    const fs::path cached = backgroundOf(current(c));
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "fresh.xopp").string()))));
    EXPECT_EQ(backgroundOf(current(c)), root / "fresh.pdf") << "a document without PDF: its own, paired";
    EXPECT_FALSE(fs::exists(cached)) << "moved out of the cache";
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "fresh.xopp", WORDS), (std::vector<std::string>{"", "pastedbeta"}));

    // A PDF annotated without .xopp yet: its pages and the pasted ones go into the hidden sidecar
    const std::string original = bytesOf(root / "lecture.pdf");
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    ASSERT_EQ(c.pastePages(1), 1);
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "lecture.xopp").string()))));
    EXPECT_EQ(backgroundOf(current(c)), root / ".lecture.pages.pdf");
    EXPECT_EQ(bytesOf(root / "lecture.pdf"), original);
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "lecture.xopp", WORDS),
              (std::vector<std::string>{"lectureone", "pastedbeta", "lecturetwo", "lecturethree"}));

    // "Save as" another name: it gets its own copy, the first one keeps its file
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "copy.xopp").string()))));
    EXPECT_EQ(backgroundOf(current(c)), root / ".copy.pages.pdf");
    EXPECT_TRUE(fs::exists(root / ".lecture.pages.pdf"));
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "copy.xopp", WORDS),
              (std::vector<std::string>{"lectureone", "pastedbeta", "lecturetwo", "lecturethree"}));
}

TEST_F(PastedPdfPages, copiedPagesStayRightWhenASaveRenumbers) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    AppController c;
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({0});
    ASSERT_TRUE(open(c, root / "lecture.xopp"));
    DocumentSession& s = current(c);
    ASSERT_EQ(c.pastePages(3), 1);  // lectureone, lecturetwo, lecturethree, pastedalpha
    c.copyPages({3});
    ASSERT_TRUE(c.deletePages({0}));
    ASSERT_TRUE(c.save());  // pastedalpha: 3 -> 2
    ASSERT_EQ(c.pastePages(0), 1);
    EXPECT_TRUE(pageHasText(s, 0, "pastedalpha"));
}

// The library shows the document as one card; renaming, moving, copying and trashing take ".name.pages.pdf" along,
// and the search index reads the pasted pages' text through the .xopp's reference.
TEST_F(PastedPdfPages, travelWithTheirDocumentInTheLibrary) {
    annotate(root / "lecture.pdf", root / "lecture.xopp");
    {
        AppController c;
        ASSERT_TRUE(open(c, root / "other.pdf"));
        c.copyPages({1});
        ASSERT_TRUE(open(c, root / "lecture.xopp"));
        ASSERT_EQ(c.pastePages(1), 1);
        ASSERT_TRUE(c.save());
    }
    const std::vector<std::string> shown{"lectureone", "pastedbeta", "lecturetwo", "lecturethree"};
    ASSERT_EQ(wordsAsUpstreamLoadsThem(root / "lecture.xopp", WORDS), shown);
    fs::remove(root / "other.pdf");
    fs::remove(root / "third.pdf");
    const auto listing = DocumentFiles::scan(root);
    ASSERT_EQ(listing.items.size(), 1u) << "one card";
    EXPECT_EQ(listing.items[0], (DocumentItem{root / "lecture.xopp", root / "lecture.pdf"}));
    EXPECT_EQ(DocumentFiles::filesOf(listing.items[0]),
              (std::vector<fs::path>{root / "lecture.xopp", root / ".lecture.pages.pdf", root / "lecture.pdf"}))
            << "what goes to the trash";

    // The search index finds the pasted page's text
    {
        LibraryIndex index(root, root / ".xournal_library" / "index");
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        const auto hits = index.search("pastedbeta");
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].file, root / "lecture.xopp");
        EXPECT_EQ(hits[0].firstPage, 1);
    }

    auto r = DocumentFiles::rename(listing.items[0], "renamed");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / ".renamed.pages.pdf"));
    EXPECT_FALSE(fs::exists(root / ".lecture.pages.pdf"));
    EXPECT_NE(std::find(r.moved.begin(), r.moved.end(), std::make_pair(root / ".lecture.pages.pdf", root / ".renamed.pages.pdf")),
              r.moved.end()) << "an open tab follows";
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "renamed.xopp", WORDS), shown);

    fs::create_directories(root / "Week 1");
    r = DocumentFiles::move(r.item, root / "Week 1");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "Week 1" / ".renamed.pages.pdf"));
    EXPECT_FALSE(fs::exists(root / ".renamed.pages.pdf"));
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "Week 1" / "renamed.xopp", WORDS), shown);

    fs::create_directories(root / "Other library");
    const auto copied = DocumentFiles::import(root / "Week 1" / "renamed.xopp", root / "Other library");
    ASSERT_TRUE(copied.ok) << copied.error;
    EXPECT_TRUE(fs::exists(root / "Other library" / ".renamed.pages.pdf"));
    EXPECT_TRUE(fs::exists(root / "Week 1" / ".renamed.pages.pdf")) << "the original keeps its file";
    EXPECT_EQ(wordsAsUpstreamLoadsThem(root / "Other library" / "renamed.xopp", WORDS), shown);
}
