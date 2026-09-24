/*
 * xournal-qt: PDF files mode (qt/docs/hybrid-pdf.md, "PDF-only mode"): every document is one PDF with notes, and
 * nothing is written next to the user's files.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
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
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION == 11
#define POINTERHOLDER_TRANSITION 4
#endif
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"

#include "AppController.h"

using namespace xqt;

namespace {
void makeTextPdf(const fs::path& p, const std::vector<std::string>& words) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    for (const auto& w: words) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, w.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

void makePng(const fs::path& p) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 64, 48);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0.2, 0.5, 0.9);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(s, p.string().c_str());
    cairo_surface_destroy(s);
}

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

size_t count(const std::string& haystack, const std::string& needle) {
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        ++n;
    }
    return n;
}

/// Every file in a folder, hidden ones too (sorted).
std::vector<std::string> filesIn(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e: fs::directory_iterator(dir)) {
        out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// A stroke on a page, through the undo stack (the document is changed).
void drawStroke(DocumentSession& s, size_t pageNo, double y = 100) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->setColor(Color(0xffcc0000U));
    stroke->addPoint(Point(100, y, 1.0));
    stroke->addPoint(Point(200, y + 80, 3.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

size_t strokesIn(Document& doc) {
    size_t n = 0;
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        for (const Layer* l: doc.getPage(p)->getLayers()) {
            for (const auto& e: l->getElementsView()) {
                n += e->getType() == ELEMENT_STROKE;
            }
        }
    }
    return n;
}

/// The raw bytes of each page's content streams (as the file has them: the original pages are never rewritten).
std::vector<std::string> pageContents(const fs::path& pdf) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    std::vector<std::string> out;
    for (auto& page: QPDFPageDocumentHelper(q).getAllPages()) {
        std::string data;
        QPDFObjectHandle contents = page.getObjectHandle().getKey("/Contents");
        std::vector<QPDFObjectHandle> streams =
                contents.isArray() ? contents.getArrayAsVector() : std::vector<QPDFObjectHandle>{contents};
        for (auto& c: streams) {
            auto buf = c.getRawStreamData();
            data.append(reinterpret_cast<const char*>(buf->getBuffer()), buf->getSize());
        }
        out.push_back(data);
    }
    return out;
}

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

QUrl url(const fs::path& p) { return QUrl::fromLocalFile(QString::fromStdString(p.string())); }

class PdfOnlyMode: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        makeTextPdf(root / "lecture.pdf", {"lectureone", "lecturetwo", "lecturethree"});
    }
    /// Sets the mode in the controller's settings, as the question or Settings store it; back to "not chosen" when
    /// it goes (before the controller: declared after it). The tests of one run share the config folder.
    struct Mode {
        Mode(AppController& c, DocumentMode::Mode m): settings(*c.context().getSettings()) { set(m); }
        ~Mode() { DocumentMode::store(settings, DocumentMode::Mode::Unset); }
        void set(DocumentMode::Mode m) { DocumentMode::store(settings, m); }
        Settings& settings;
    };
    bool open(AppController& c, const fs::path& p) { return c.openPath(QString::fromStdString(p.string())); }
    DocumentSession& current(AppController& c) { return *c.tabManager().currentSession(); }

    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

// New documents are "name.pdf", PDFs with notes: from the library (saved at once) and from Save as (the PDF type
// first, the name "name.pdf").
TEST_F(PdfOnlyMode, newDocumentsArePdfsWithNotes) {
    AppController c;
    Mode mode(c, DocumentMode::Mode::Pdf);
    fs::remove(root / "lecture.pdf");
    c.setLibraryRoot(root);
    ASSERT_TRUE(c.createDocument("Physics", true));
    EXPECT_TRUE(c.isHybrid());
    EXPECT_EQ(current(c).getFilePath(), root / "Physics.pdf");
    EXPECT_TRUE(HybridPdf::isHybrid(root / "Physics.pdf"));
    EXPECT_EQ(filesIn(root), std::vector<std::string>{"Physics.pdf"}) << "nothing else next to it";
    drawStroke(current(c), 0);
    ASSERT_TRUE(c.save());
    EXPECT_EQ(filesIn(root), std::vector<std::string>{"Physics.pdf"});

    // A new tab: Save as starts on the PDF with notes
    c.newDocument();
    EXPECT_EQ(c.saveFormat(), "pdf");
    EXPECT_FALSE(c.savesWithoutDialog()) << "it needs a name";
    EXPECT_EQ(c.suggestedHybridFile().toLocalFile().right(4), ".pdf");
    drawStroke(current(c), 0);
    ASSERT_TRUE(c.saveAsHybrid(url(root / "Chemistry.pdf")));
    EXPECT_TRUE(HybridPdf::isHybrid(root / "Chemistry.pdf"));
    EXPECT_EQ(filesIn(root), (std::vector<std::string>{"Chemistry.pdf", "Physics.pdf"}));

    // The same in Xournal++ files mode: a .xopp, as before
    mode.set(DocumentMode::Mode::Xopp);
    ASSERT_TRUE(c.createDocument("Biology", true));
    EXPECT_FALSE(c.isHybrid());
    EXPECT_EQ(current(c).getFilePath(), root / "Biology.xopp");
    c.newDocument();
    EXPECT_EQ(c.saveFormat(), "xopp");
}

// A plain PDF annotated and saved: Ctrl+S writes the notes into that PDF (no dialog, no .xopp, no
// "name.original.pdf" next to it; the original is kept in the app cache). The original pages are not rewritten. The
// next Ctrl+S appends an incremental update. A one-time notice says where the notes went.
TEST_F(PdfOnlyMode, annotatingAPdfSavesIntoItThenAppends) {
    const fs::path pdf = root / "lecture.pdf";
    const std::string original = bytesOf(pdf);
    const std::vector<std::string> originalPages = pageContents(pdf);
    const fs::path originals = Util::getCacheSubfolder("originals");
    fs::remove_all(originals);  // (the test's cache: what other tests of this run kept)
    ASSERT_EQ(originalPages.size(), 3u);
    AppController c;
    Mode mode(c, DocumentMode::Mode::Pdf);
    c.context().getSettings()->getCustomElement("xournalQt").setBool("pdfOnlyIntoPdfNoticed", false);  // (this run)
    QSignalSpy notes(&c, &AppController::pageActionDone);
    ASSERT_TRUE(open(c, pdf));
    EXPECT_FALSE(c.modified());
    drawStroke(current(c), 1);
    EXPECT_TRUE(c.savesWithoutDialog()) << "no \"where to save the .xopp\" step";
    EXPECT_EQ(c.suggestedHybridFile().toLocalFile().toStdString(), pdf.string()) << "Save as: the PDF itself";
    EXPECT_EQ(c.saveFormat(), "pdf");

    ASSERT_TRUE(c.save());
    EXPECT_FALSE(c.modified());
    EXPECT_TRUE(c.isHybrid());
    EXPECT_EQ(current(c).getFilePath(), pdf);
    EXPECT_TRUE(HybridPdf::isHybrid(pdf));
    EXPECT_EQ(filesIn(root), std::vector<std::string>{"lecture.pdf"}) << "no .xopp, no original, no sidecar";
    EXPECT_EQ(pageContents(pdf), originalPages) << "the original pages are not rewritten";

    // The original, once, in the app cache
    std::vector<fs::path> kept;
    for (const auto& e: fs::recursive_directory_iterator(originals)) {
        if (e.is_regular_file()) {
            kept.push_back(e.path());
        }
    }
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0].filename(), "lecture.pdf");
    EXPECT_EQ(bytesOf(kept[0]), original);

    // Said once
    auto noticed = [&] {
        int n = 0;
        for (const auto& args: notes) {
            n += args.at(0).toString().startsWith("Your notes are saved in lecture.pdf");
        }
        return n;
    };
    EXPECT_EQ(noticed(), 1);

    // The second save appends: the file as it was, followed by an update (a small file soon grows by a quarter,
    // when it is written anew: not here)
    const double compactAbove = HybridPdf::compactAbove;
    HybridPdf::compactAbove = 100;
    const std::string first = bytesOf(pdf);
    drawStroke(current(c), 2, 300);
    ASSERT_TRUE(c.save());
    HybridPdf::compactAbove = compactAbove;
    const std::string second = bytesOf(pdf);
    ASSERT_GT(second.size(), first.size());
    EXPECT_EQ(second.compare(0, first.size(), first), 0) << "an incremental update: the earlier bytes stay";
    EXPECT_EQ(count(second, "startxref"), count(first, "startxref") + 1);
    EXPECT_LT(second.size() - first.size(), first.size()) << "only what changed";
    EXPECT_EQ(pageContents(pdf), originalPages);
    EXPECT_EQ(filesIn(root), std::vector<std::string>{"lecture.pdf"});
    EXPECT_EQ(noticed(), 1) << "once";
    EXPECT_EQ(bytesOf(kept[0]), original) << "the kept original stays the first one";

    // Opened again: both strokes, editable
    c.closeTab(0);
    ASSERT_TRUE(open(c, pdf));
    EXPECT_TRUE(c.isHybrid());
    EXPECT_EQ(strokesIn(*current(c).getDocument()), 2u);

    // Another PDF: no notice any more
    makeTextPdf(root / "second.pdf", {"secondone"});
    ASSERT_TRUE(open(c, root / "second.pdf"));
    drawStroke(current(c), 0);
    ASSERT_TRUE(c.save());
    EXPECT_TRUE(HybridPdf::isHybrid(root / "second.pdf"));
    EXPECT_EQ(noticed(), 1);
}

// Xournal++ files mode keeps today's way: the notes of a PDF need Save as, and go to "name.notes.pdf" or a .xopp.
TEST_F(PdfOnlyMode, xournalFilesModeLeavesPdfsAlone) {
    AppController c;
    Mode mode(c, DocumentMode::Mode::Xopp);
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    drawStroke(current(c), 0);
    EXPECT_FALSE(c.savesWithoutDialog());
    EXPECT_EQ(c.saveFormat(), "xopp");
    EXPECT_EQ(c.suggestedHybridFile().toLocalFile().toStdString(), (root / "lecture.notes.pdf").string());
    EXPECT_FALSE(c.save()) << "no file yet: Save as";
}

// Pages pasted from another PDF go into the PDF: after the save nothing lies next to the document (no ".pages.pdf",
// no ".next.pdf"), and the pasted page is a real PDF page with its text.
TEST_F(PdfOnlyMode, pastedPagesLeaveNoSidecars) {
    makeTextPdf(root / "other.pdf", {"pastedalpha", "pastedbeta"});
    AppController c;
    Mode mode(c, DocumentMode::Mode::Pdf);
    ASSERT_TRUE(open(c, root / "other.pdf"));
    c.copyPages({1});  // "pastedbeta"
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    DocumentSession& s = current(c);
    QSignalSpy notes(&c, &AppController::pageActionDone);
    ASSERT_EQ(c.pastePages(1), 1);
    ASSERT_FALSE(notes.isEmpty());
    EXPECT_TRUE(notes.last().at(0).toString().contains("goes into the PDF")) << notes.last().at(0).toString().toStdString();
    drawStroke(s, 1);
    ASSERT_TRUE(c.save());
    EXPECT_EQ(filesIn(root), (std::vector<std::string>{"lecture.pdf", "other.pdf"}));
    // Once more (an incremental save after the paste)
    drawStroke(s, 0);
    ASSERT_TRUE(c.save());
    ASSERT_EQ(c.pastePages(0), 1);
    ASSERT_TRUE(c.save());
    EXPECT_EQ(filesIn(root), (std::vector<std::string>{"lecture.pdf", "other.pdf"}));

    c.closeTab(c.tabManager().indexOf(&s));
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    DocumentSession& again = current(c);
    ASSERT_EQ(again.getDocument()->getPageCount(), 5u);
    EXPECT_TRUE(again.getDocument()->getPage(2)->getBackgroundType().isPdfPage()) << "a PDF page, not an image";
    EXPECT_FALSE(DocumentSearch::findOnPage(*again.getDocument(), 2, "pastedbeta").empty());
    EXPECT_FALSE(DocumentSearch::findOnPage(*again.getDocument(), 0, "pastedbeta").empty());

    // A new document with pasted pages
    c.newDocument();
    ASSERT_EQ(c.pastePages(0), 1);
    ASSERT_TRUE(c.saveAsHybrid(url(root / "collected.pdf")));
    EXPECT_EQ(filesIn(root), (std::vector<std::string>{"collected.pdf", "lecture.pdf", "other.pdf"}));
}

// An image written on is saved as "photo.pdf" with the image inside: the PDF opens with its picture also when the
// image file is gone.
TEST_F(PdfOnlyMode, anImageGoesInsideThePdf) {
    makePng(root / "photo.png");
    AppController c;
    Mode mode(c, DocumentMode::Mode::Pdf);
    ASSERT_TRUE(open(c, root / "photo.png"));
    EXPECT_EQ(c.saveFormat(), "pdf");
    EXPECT_EQ(c.suggestedHybridFile().toLocalFile().toStdString(), (root / "photo.pdf").string());
    EXPECT_TRUE(c.shownFileNote().contains("photo.pdf")) << c.shownFileNote().toStdString();
    drawStroke(current(c), 0);
    ASSERT_TRUE(c.saveAsHybrid(c.suggestedHybridFile()));
    EXPECT_EQ(filesIn(root), (std::vector<std::string>{"lecture.pdf", "photo.pdf", "photo.png"}));
    c.closeTab(0);
    fs::remove(root / "photo.png");
    ASSERT_TRUE(open(c, root / "photo.pdf"));
    const PageRef page = current(c).getDocument()->getPage(0);
    ASSERT_TRUE(page->getBackgroundType().isImagePage());
    EXPECT_NE(page->getBackgroundImage().getPixbuf(), nullptr) << "the image comes from the PDF";
}

// A .xopp keeps its format in PDF files mode: Ctrl+S writes the .xopp, Save as starts on .xopp.
TEST_F(PdfOnlyMode, aXoppStaysAXopp) {
    AppController c;
    Mode mode(c, DocumentMode::Mode::Xopp);
    c.newDocument();
    drawStroke(current(c), 0);
    ASSERT_TRUE(c.saveAs(url(root / "notes.xopp")));
    mode.set(DocumentMode::Mode::Pdf);
    drawStroke(current(c), 0, 300);
    EXPECT_EQ(c.saveFormat(), "xopp");
    EXPECT_TRUE(c.savesWithoutDialog());
    ASSERT_TRUE(c.save());
    EXPECT_FALSE(c.isHybrid());
    EXPECT_EQ(current(c).getFilePath(), root / "notes.xopp");
    EXPECT_EQ(filesIn(root), (std::vector<std::string>{"lecture.pdf", "notes.xopp"}));
}

// Autosaves stay in the app cache in PDF files mode, also for a saved document (Xournal++ files mode: next to it,
// as upstream). Recovery: RecoveryTest.pdfFilesModeAutosavesInTheCacheAndRecovers.
TEST_F(PdfOnlyMode, autosavesStayInTheCache) {
    const fs::path pdf = root / "lecture.pdf";
    AppController c;
    Mode mode(c, DocumentMode::Mode::Pdf);
    ASSERT_TRUE(open(c, pdf));
    DocumentSession& s = current(c);
    drawStroke(s, 0);
    ASSERT_TRUE(s.autosave().ok);  // (not saved yet)
    EXPECT_EQ(s.getLastAutosaveFile().parent_path(), Util::getAutosaveFilepath().parent_path());
    ASSERT_TRUE(c.save());
    drawStroke(s, 1);
    ASSERT_TRUE(s.autosave().ok);  // (saved: it has a file now)
    const fs::path autosave = s.getLastAutosaveFile();
    EXPECT_EQ(autosave.parent_path(), Util::getAutosaveFilepath().parent_path()) << "the app cache";
    EXPECT_TRUE(fs::exists(autosave));
    EXPECT_EQ(filesIn(root), std::vector<std::string>{"lecture.pdf"}) << "nothing next to the PDF";

    mode.set(DocumentMode::Mode::Xopp);
    EXPECT_EQ(s.autosavePath(), DocumentSession::namedAutosavePath(pdf)) << "Xournal++ files: as before";
    c.closeTab(0);
    EXPECT_FALSE(fs::exists(autosave)) << "closed without losing anything: removed";
}
