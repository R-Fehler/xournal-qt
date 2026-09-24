/*
 * xournal-qt: open documents (.xopp, PDFs, PDFs with notes) whose files another program changes (a sync app, another
 * editor): read again when they have no unsaved changes here, else asked about; the app's own saves never count.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <memory>

#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "config-test.h"

using namespace xqt;

namespace {

void makePdf(const fs::path& p, int pages) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    for (int i = 0; i < pages; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("page " + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

void drawStroke(Document& doc, UndoRedoHandler* undo, double y = 100) {
    auto page = doc.getPage(0);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->addPoint(Point(100, y, 1.0));
    stroke->addPoint(Point(200, y + 80, 1.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    doc.lock();
    layer->addElement(std::move(stroke));
    doc.unlock();
    if (undo) {
        undo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    }
}
void drawStroke(DocumentSession& s, double y = 100) { drawStroke(*s.getDocument(), s.getUndoRedoHandler(), y); }

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

/// Another program writes the document: loaded on its own, a stroke added, written back (atomically, as sync apps do)
void changeElsewhere(const fs::path& file, double y = 400) {
    auto r = DocumentSession::loadFile(file);
    ASSERT_TRUE(r.document) << r.error;
    drawStroke(*r.document, nullptr, y);
    const auto written = DocumentSession::writeDocument(*r.document, file);
    ASSERT_TRUE(written.ok) << written.error;
}

class ExternalChangesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        xopp = root / "notes.xopp";
        fs::copy_file(GET_TESTFILE(u8"load/strokes.xopp"), xopp);
    }
    bool open(AppController& c, const fs::path& p) { return c.openPath(QString::fromStdString(p.string())); }
    DocumentSession& current(AppController& c) { return *c.tabManager().currentSession(); }
    QTemporaryDir tmp;
    fs::path root, xopp;
};
}  // namespace

// Unchanged here: read again, the tab in its place at its page; a note says so.
TEST_F(ExternalChangesTest, anUnchangedDocumentIsReadAgain) {
    AppController c;
    ASSERT_TRUE(open(c, xopp));
    c.newDocument();
    c.tabManager().setCurrentIndex(0);
    const QPointer<DocumentSession> before = &current(c);
    const size_t strokes = strokesIn(*before->getDocument());
    QSignalSpy notes(&c, &AppController::pageActionDone);
    QSignalSpy asked(&c, &AppController::documentChangedOnDisk);

    c.checkTextFiles();
    EXPECT_EQ(c.tabManager().session(0), before.data()) << "nothing changed";
    changeElsewhere(xopp);
    c.checkTextFiles();
    ASSERT_EQ(c.tabCount(), 2);
    DocumentSession* after = c.tabManager().session(0);
    EXPECT_NE(after, before.data()) << "read again";
    EXPECT_TRUE(before.isNull()) << "the old one is closed";
    EXPECT_EQ(strokesIn(*after->getDocument()), strokes + 1);
    EXPECT_EQ(after->getFilePath(), xopp);
    EXPECT_FALSE(after->isModified());
    EXPECT_EQ(c.currentTab(), 0);
    EXPECT_EQ(asked.count(), 0);
    ASSERT_EQ(notes.count(), 1);
    EXPECT_TRUE(notes.at(0).at(0).toString().startsWith("notes.xopp was changed by another app"));

    // Not again for the same version; a tab that is not the current one stays where it was
    c.checkTextFiles();
    EXPECT_EQ(c.tabManager().session(0), after);
    c.tabManager().setCurrentIndex(1);
    changeElsewhere(xopp, 600);
    c.checkTextFiles();
    EXPECT_NE(c.tabManager().session(0), after);
    EXPECT_EQ(c.currentTab(), 1) << "the current tab stays current";
    EXPECT_EQ(strokesIn(*c.tabManager().session(0)->getDocument()), strokes + 2);
}

// Unsaved changes here: asked. "Keep mine" keeps them (not asked again about that version; saving writes over it),
// "Reload" takes the other version.
TEST_F(ExternalChangesTest, withChangesHereTheWindowAsks) {
    AppController c;
    ASSERT_TRUE(open(c, xopp));
    const size_t strokes = strokesIn(*current(c).getDocument());
    drawStroke(current(c));
    QSignalSpy asked(&c, &AppController::documentChangedOnDisk);
    changeElsewhere(xopp);
    c.checkTextFiles();
    ASSERT_EQ(asked.count(), 1);
    EXPECT_EQ(asked.at(0).at(0).toString(), "notes.xopp");
    const QPointer<DocumentSession> mine = &current(c);
    c.resolveTextChange(false);  // keep mine
    EXPECT_EQ(&current(c), mine.data());
    EXPECT_TRUE(mine->isModified());
    c.checkTextFiles();
    EXPECT_EQ(asked.count(), 1) << "not asked again about that version";
    ASSERT_TRUE(c.save());
    c.checkTextFiles();
    EXPECT_EQ(asked.count(), 1) << "the app's own save";
    EXPECT_EQ(strokesIn(*DocumentSession::loadFile(xopp).document), strokes + 1) << "mine written over it";

    // Changed there again, and reloaded this time: the other version, the change here is gone
    drawStroke(current(c), 700);
    changeElsewhere(xopp, 300);
    c.checkTextFiles();
    ASSERT_EQ(asked.count(), 2);
    c.resolveTextChange(true);
    EXPECT_TRUE(mine.isNull());
    EXPECT_FALSE(current(c).isModified());
    EXPECT_EQ(strokesIn(*current(c).getDocument()), strokes + 2);
}

// The app's own saves never count: a .xopp renamed over the old file, a PDF with notes written anew and appended to
// (incremental update), a .xopp next to a PDF. A file that is only touched (same content) does not count either.
TEST_F(ExternalChangesTest, theAppsOwnSavesDoNotCount) {
    AppController c;
    QSignalSpy asked(&c, &AppController::documentChangedOnDisk);
    QSignalSpy notes(&c, &AppController::pageActionDone);
    ASSERT_TRUE(open(c, xopp));
    const QPointer<DocumentSession> x = &current(c);
    for (int i = 0; i < 3; ++i) {
        drawStroke(*x, 100 + 50 * i);
        ASSERT_TRUE(c.save());
        c.checkTextFiles();
    }
    // Saved in the background, checked while it runs and after it
    drawStroke(*x, 500);
    ASSERT_TRUE(c.saveInBackground());
    c.checkTextFiles();
    ASSERT_TRUE(x->waitForSaves());
    QCoreApplication::processEvents();
    c.checkTextFiles();
    EXPECT_EQ(&current(c), x.data()) << "not read again";

    // Touched only
    fs::last_write_time(xopp, fs::last_write_time(xopp) + std::chrono::seconds(5));
    c.checkTextFiles();
    EXPECT_EQ(&current(c), x.data());

    // A PDF with notes (PDF files mode): written into the PDF, then appended to
    const fs::path pdf = root / "lecture.pdf";
    makePdf(pdf, 2);
    DocumentMode::store(*c.context().getSettings(), DocumentMode::Mode::Pdf);
    ASSERT_TRUE(open(c, pdf));
    const QPointer<DocumentSession> h = &current(c);
    drawStroke(*h);
    ASSERT_TRUE(c.save());
    ASSERT_TRUE(HybridPdf::isHybrid(pdf));
    c.checkTextFiles();
    drawStroke(*h, 300);
    const auto sizeBefore = fs::file_size(pdf);
    ASSERT_TRUE(c.save());
    EXPECT_GT(fs::file_size(pdf), sizeBefore);
    c.checkTextFiles();
    DocumentMode::store(*c.context().getSettings(), DocumentMode::Mode::Unset);
    EXPECT_EQ(&current(c), h.data());

    // A .xopp that annotates a PDF: saved next to it
    const fs::path slides = root / "slides.pdf";
    makePdf(slides, 3);
    ASSERT_TRUE(open(c, slides));
    const QPointer<DocumentSession> a = &current(c);
    drawStroke(*a);
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "slides.xopp").string()))));
    c.checkTextFiles();
    drawStroke(*a, 400);
    ASSERT_TRUE(c.save());
    c.checkTextFiles();
    EXPECT_FALSE(x.isNull());
    EXPECT_FALSE(h.isNull());
    EXPECT_FALSE(a.isNull());
    EXPECT_EQ(asked.count(), 0);
    for (const auto& n: notes) {
        EXPECT_FALSE(n.at(0).toString().contains("changed by another app")) << n.at(0).toString().toStdString();
    }
    EXPECT_EQ(c.tabCount(), 3);
}

// A PDF annotated (a .xopp next to it, or not saved yet) whose PDF another program replaces: read again with the new
// PDF, when nothing is unsaved here.
TEST_F(ExternalChangesTest, aNewVersionOfTheAnnotatedPdfIsTakenIn) {
    const fs::path pdf = root / "slides.pdf";
    makePdf(pdf, 2);
    AppController c;
    ASSERT_TRUE(open(c, pdf));
    DocumentSession* s = &current(c);
    drawStroke(*s);
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "slides.xopp").string()))));
    EXPECT_EQ(s->filesOnDisk(), (std::vector<fs::path>{root / "slides.xopp", pdf}));
    const QPointer<DocumentSession> before = s;
    makePdf(root / "new.pdf", 5);
    fs::rename(root / "new.pdf", pdf);  // (a sync app: renamed over it)
    c.checkTextFiles();
    ASSERT_TRUE(before.isNull()) << "read again";
    EXPECT_EQ(current(c).getDocument()->getPdfPageCount(), 5u);
    EXPECT_EQ(strokesIn(*current(c).getDocument()), 1u);

    // A PDF opened and not saved (its notes are nowhere else): asked
    const fs::path other = root / "paper.pdf";
    makePdf(other, 1);
    ASSERT_TRUE(open(c, other));
    drawStroke(current(c));
    QSignalSpy asked(&c, &AppController::documentChangedOnDisk);
    makePdf(root / "new.pdf", 2);
    fs::rename(root / "new.pdf", other);
    c.checkTextFiles();
    EXPECT_EQ(asked.count(), 1);
    EXPECT_EQ(asked.at(0).at(0).toString(), "paper.pdf");
    c.resolveTextChange(false);
}
