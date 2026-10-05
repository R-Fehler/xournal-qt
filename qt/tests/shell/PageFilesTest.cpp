/*
 * xournal-qt: pages as files (qt/docs/page-files.md): inserting pages from a PDF, a PDF with notes or a protected PDF
 * (their text stays searchable, their notes come along, one undo step); extracting selected pages into a new PDF with
 * notes or .xopp (opened, the source unchanged or the pages removed in one undo step; a protected document gives a
 * protected PDF and no .xopp); splitting; exporting pages as pictures named "name-p003.png"; the ranges and names.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMimeData>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <cairo-pdf.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PageFiles.h"
#include "session/PdfEncryption.h"
#include "shell/TabManager.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

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

void protect(const fs::path& in, const fs::path& out, const char* password) {
    QPDF q;
    q.processFile(in.string().c_str());
    QPDFWriter w(q, out.string().c_str());
    w.setR6EncryptionParameters(password, "owner of it", true, true, true, true, true, true, qpdf_r3p_full, true);
    w.write();
}

/// A stroke on a page, through the undo stack (the document is changed).
void drawStroke(DocumentSession& s, size_t pageNo) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(4);
    stroke->setColor(Color(0xffcc0000U));
    stroke->addPoint(Point(100, 100, 1.0));
    stroke->addPoint(Point(300, 300, 1.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

size_t strokesOn(const XojPage& page) {
    size_t n = 0;
    for (const Layer* l: page.getLayersView()) {
        for (const auto& e: l->getElementsView()) {
            n += e->getType() == ELEMENT_STROKE;
        }
    }
    return n;
}

bool waitFor(const std::function<bool()>& done, int ms = 30000) {
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

bool hasText(Document& doc, size_t page, const char* text) { return !DocumentSearch::findOnPage(doc, page, text).empty(); }

class PageFilesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        makeTextPdf(root / "lecture.pdf", {"lectureone", "lecturetwo", "lecturethree"});
    }
    struct Mode {
        Mode(AppController& c, DocumentMode::Mode m): settings(*c.context().getSettings()) { DocumentMode::store(settings, m); }
        ~Mode() { DocumentMode::store(settings, DocumentMode::Mode::Unset); }
        Settings& settings;
    };
    bool open(AppController& c, const fs::path& p) { return c.openPath(QString::fromStdString(p.string())); }
    DocumentSession& current(AppController& c) { return *c.tabManager().currentSession(); }
    bool pageHasText(DocumentSession& s, size_t page, const char* text) {
        s.waitForSaves();
        return hasText(*s.getDocument(), page, text);
    }
    /// Read a file to insert from; what pageFileRead said
    QVariantMap read(AppController& c, const fs::path& file, const QString& password = {}) {
        QSignalSpy spy(&c, &AppController::pageFileRead);
        EXPECT_TRUE(c.readPageFile(url(file), password));
        EXPECT_TRUE(waitFor([&] { return spy.count() > 0; }));
        return spy.isEmpty() ? QVariantMap() : spy.first().at(0).toMap();
    }
    int insert(AppController& c, const QString& range, const QList<int>& picked, int position) {
        QSignalSpy spy(&c, &AppController::pagesFromFileInserted);
        EXPECT_TRUE(c.insertPagesFromFile(range, picked, position));
        EXPECT_TRUE(waitFor([&] { return spy.count() > 0; }));
        if (spy.isEmpty()) {
            return 0;
        }
        EXPECT_EQ(spy.first().at(1).toString(), QString());
        return spy.first().at(0).toInt();
    }
    /// Extract or split; the files written
    QStringList written(AppController& c, const std::function<bool()>& start) {
        QSignalSpy spy(&c, &AppController::pagesExtracted);
        EXPECT_TRUE(start());
        EXPECT_TRUE(waitFor([&] { return spy.count() > 0; }));
        if (spy.isEmpty()) {
            return {};
        }
        EXPECT_EQ(spy.first().at(1).toString(), QString());
        return spy.first().at(0).toStringList();
    }
    QStringList images(AppController& c, const QList<int>& pages, int dpi, bool transparent, const QString& format) {
        QSignalSpy spy(&c, &AppController::pageImagesExported);
        EXPECT_TRUE(c.exportPageImages(pages, url(root / "pictures"), dpi, transparent, format));
        EXPECT_TRUE(waitFor([&] { return spy.count() > 0; }));
        if (spy.isEmpty()) {
            return {};
        }
        EXPECT_EQ(spy.first().at(1).toString(), QString());
        return spy.first().at(0).toStringList();
    }
    std::vector<std::string> names(const QStringList& files) {
        std::vector<std::string> out;
        for (const QString& f: files) {
            out.push_back(fs::path(f.toStdString()).filename().string());
        }
        return out;
    }

    QTemporaryDir tmp;
    fs::path root;
};

}  // namespace

// --- ranges, parts and names ------------------------------------------------------------------------------------

TEST(PageFilesRanges, rangesPartsAndPictureNames) {
    using pagefiles::parseRange;
    EXPECT_EQ(parseRange("1-3, 5", 10), (std::vector<size_t>{0, 1, 2, 4}));
    EXPECT_EQ(parseRange(" 8- ", 10), (std::vector<size_t>{7, 8, 9}));
    EXPECT_EQ(parseRange("-2; 2", 10), (std::vector<size_t>{0, 1}));
    EXPECT_EQ(parseRange("3\xe2\x80\x93" "4", 10), (std::vector<size_t>{2, 3})) << "an en dash";
    EXPECT_EQ(parseRange("5-3", 10), (std::vector<size_t>{2, 3, 4}));
    EXPECT_EQ(parseRange("9-20", 10), (std::vector<size_t>{8, 9})) << "cut to the document";
    std::string error;
    EXPECT_TRUE(parseRange("0", 10, &error).empty());
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(parseRange("two", 10, &error).empty());
    EXPECT_TRUE(parseRange("12", 10, &error).empty()) << "no page of the document";
    EXPECT_TRUE(parseRange("", 10, &error).empty());
    EXPECT_EQ(pagefiles::rangeText({4, 0, 1, 2, 9}), "1-3, 5, 10");
    EXPECT_EQ(parseRange(pagefiles::rangeText({0, 1, 2, 4}), 10), (std::vector<size_t>{0, 1, 2, 4}));

    const auto every = pagefiles::splitEvery(7, 3);
    ASSERT_EQ(every.size(), 3u);
    EXPECT_EQ(every[2], (std::vector<size_t>{6}));
    const auto at = pagefiles::splitAt(6, {4, 2, 9});
    ASSERT_EQ(at.size(), 3u);
    EXPECT_EQ(at[0], (std::vector<size_t>{0, 1}));
    EXPECT_EQ(at[2], (std::vector<size_t>{4, 5}));

    EXPECT_EQ(pagefiles::imageName("lecture", 2, 10, ".png"), "lecture-p003.png");
    EXPECT_EQ(pagefiles::imageName("book", 41, 1200, ".jpg"), "book-p0042.jpg") << "sorted in page order";
}

// --- inserting pages from a file (A6) ---------------------------------------------------------------------------

TEST_F(PageFilesTest, pagesOfAPdfAreInsertedWithTheirTextSearchableInOneUndoStep) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.saveAs(url(root / "notes.xopp")));
    DocumentSession& s = current(c);
    const QVariantMap info = read(c, root / "lecture.pdf");
    ASSERT_TRUE(info.value("ok").toBool()) << info.value("error").toString().toStdString();
    EXPECT_EQ(info.value("pages").toInt(), 3);
    EXPECT_FALSE(info.value("thumbnails").toString().isEmpty()) << "pictures of its pages for the dialog";
    EXPECT_TRUE(c.checkPageRange("2-3", 3).isEmpty());
    EXPECT_FALSE(c.checkPageRange("4", 3).isEmpty());

    // Pages 2 and 3, before the first page
    ASSERT_EQ(insert(c, "2-3", {}, 0), 2);
    ASSERT_EQ(s.getDocument()->getPageCount(), 3u);
    for (size_t i: {0u, 1u}) {
        EXPECT_TRUE(s.getDocument()->getPage(i)->getBackgroundType().isPdfPage()) << "a PDF page, not a picture";
    }
    EXPECT_TRUE(pageHasText(s, 0, "lecturetwo"));
    EXPECT_TRUE(pageHasText(s, 1, "lecturethree"));
    EXPECT_FALSE(s.getDocument()->getPage(2)->getBackgroundType().isPdfPage()) << "the page that was there";

    // One undo step
    c.undoPages();
    EXPECT_EQ(s.getDocument()->getPageCount(), 1u);
    c.redoPages();
    ASSERT_EQ(s.getDocument()->getPageCount(), 3u);

    // Saved and opened again: still text
    ASSERT_TRUE(c.save());
    s.waitForSaves();
    auto loaded = DocumentSession::loadFile(root / "notes.xopp");
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_EQ(loaded.document->getPageCount(), 3u);
    EXPECT_TRUE(hasText(*loaded.document, 1, "lecturethree"));

    // Picked pages, after the last page; the file is let go after inserting
    ASSERT_TRUE(read(c, root / "lecture.pdf").value("ok").toBool());
    ASSERT_EQ(insert(c, {}, {0}, 3), 1);
    EXPECT_TRUE(pageHasText(s, 3, "lectureone"));
    EXPECT_FALSE(c.insertPagesFromFile({}, {0}, 0)) << "nothing read any more";
}

TEST_F(PageFilesTest, pagesOfAPdfWithNotesComeWithTheirNotes) {
    AppController c;
    {
        Mode mode(c, DocumentMode::Mode::Pdf);
        makeTextPdf(root / "paper.pdf", {"paperone", "papertwo"});
        ASSERT_TRUE(open(c, root / "paper.pdf"));
        drawStroke(current(c), 1);
        ASSERT_TRUE(c.save());
        current(c).waitForSaves();
        ASSERT_TRUE(HybridPdf::isHybrid(root / "paper.pdf"));
        c.closeTab(c.tabManager().indexOf(&current(c)));
    }
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    DocumentSession& s = current(c);
    ASSERT_TRUE(read(c, root / "paper.pdf").value("ok").toBool());
    ASSERT_EQ(insert(c, "2", {}, 1), 1);
    ASSERT_EQ(s.getDocument()->getPageCount(), 4u);
    EXPECT_TRUE(pageHasText(s, 1, "papertwo"));
    EXPECT_EQ(strokesOn(*s.getDocument()->getPage(1)), 1u) << "its ink as ink";
    EXPECT_TRUE(pageHasText(s, 2, "lecturetwo")) << "the pages after it moved on";
}

TEST_F(PageFilesTest, aProtectedPdfAsksForItsPassword) {
    protect(root / "lecture.pdf", root / "locked.pdf", "sesame");
    AppController c;
    c.newDocument();
    DocumentSession& s = current(c);
    QVariantMap info = read(c, root / "locked.pdf");
    EXPECT_FALSE(info.value("ok").toBool());
    EXPECT_TRUE(info.value("needsPassword").toBool());
    EXPECT_FALSE(c.insertPagesFromFile({}, {}, 0));
    info = read(c, root / "locked.pdf", "wrong");
    EXPECT_TRUE(info.value("wrongPassword").toBool());
    info = read(c, root / "locked.pdf", "sesame");
    ASSERT_TRUE(info.value("ok").toBool());
    EXPECT_TRUE(info.value("protectedFile").toBool());
    EXPECT_TRUE(info.value("thumbnails").toString().isEmpty()) << "no pictures drawn without its password";
    ASSERT_EQ(insert(c, "3", {}, 1), 1);
    EXPECT_TRUE(pageHasText(s, 1, "lecturethree"));
}

// --- extract and split (A7) -------------------------------------------------------------------------------------

TEST_F(PageFilesTest, selectedPagesBecomeANewPdfWithNotesOpenedInATab) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    DocumentSession& source = current(c);
    drawStroke(source, 1);
    const QVariantMap draft = c.extractDraft({1, 2});
    EXPECT_EQ(draft.value("name").toString(), "lecture (pages 2-3)");
    EXPECT_EQ(draft.value("range").toString(), "2-3");
    EXPECT_TRUE(draft.value("asPdf").toBool()) << "a PDF stays a PDF";
    EXPECT_EQ(fs::path(draft.value("folder").toString().toStdString()), root) << "next to it";

    const QStringList files = written(c, [&] { return c.extractPages({1, 2}, "Extract", true, false); });
    ASSERT_EQ(names(files), (std::vector<std::string>{"Extract.pdf"}));
    const fs::path file = root / "Extract.pdf";
    EXPECT_TRUE(HybridPdf::isHybrid(file));
    DocumentSession& opened = current(c);
    EXPECT_NE(&opened, &source) << "opened in a tab";
    EXPECT_EQ(opened.getFilePath(), file);
    ASSERT_EQ(opened.getDocument()->getPageCount(), 2u);
    EXPECT_TRUE(pageHasText(opened, 0, "lecturetwo"));
    EXPECT_TRUE(pageHasText(opened, 1, "lecturethree"));
    EXPECT_EQ(strokesOn(*opened.getDocument()->getPage(0)), 1u) << "its notes";
    EXPECT_EQ(source.getDocument()->getPageCount(), 3u) << "the source unchanged";

    // Any PDF app reads it: the base pages are the PDF's own (qpdf)
    QPDF q;
    q.processFile(file.string().c_str());
    EXPECT_EQ(q.getAllPages().size(), 2u);

    // The same name again: another file
    const QStringList again = written(c, [&] {
        c.tabManager().setCurrentIndex(c.tabManager().indexOf(&source));
        return c.extractPages({0}, "Extract", true, false);
    });
    EXPECT_EQ(names(again), (std::vector<std::string>{"Extract (2).pdf"}));
}

TEST_F(PageFilesTest, extractedToXoppAndRemovedFromTheDocumentInOneUndoStep) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    ASSERT_TRUE(c.saveAs(url(root / "lecture.xopp")));
    DocumentSession& source = current(c);
    drawStroke(source, 2);
    EXPECT_FALSE(c.extractDraft({2}).value("asPdf").toBool()) << "a .xopp stays a .xopp";
    const QStringList files = written(c, [&] { return c.extractPages({2}, "Last page", false, true); });
    ASSERT_EQ(names(files), (std::vector<std::string>{"Last page.xopp"}));
    EXPECT_TRUE(fs::exists(root / "Last page.pdf")) << "its PDF page next to it";
    EXPECT_EQ(source.getDocument()->getPageCount(), 2u) << "removed from the document";
    c.tabManager().setCurrentIndex(c.tabManager().indexOf(&source));
    c.undoPages();
    EXPECT_EQ(source.getDocument()->getPageCount(), 3u) << "one undo step";

    auto loaded = DocumentSession::loadFile(root / "Last page.xopp");
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_EQ(loaded.document->getPageCount(), 1u);
    EXPECT_TRUE(hasText(*loaded.document, 0, "lecturethree"));
    EXPECT_EQ(strokesOn(*loaded.document->getPage(0)), 1u);

    // Not all pages: a document keeps one
    EXPECT_FALSE(c.extractPages({0, 1, 2}, "All", false, true));
}

TEST_F(PageFilesTest, aDocumentIsSplitEveryNPagesOrAtTheSelectedPages) {
    makeTextPdf(root / "book.pdf", {"bookone", "booktwo", "bookthree", "bookfour", "bookfive"});
    AppController c;
    ASSERT_TRUE(open(c, root / "book.pdf"));
    DocumentSession& s = current(c);
    const QVariantMap plan = c.splitPlan("every", 2, {});
    ASSERT_EQ(plan.value("parts").toList().size(), 3);
    EXPECT_EQ(plan.value("parts").toList().at(2).toMap().value("range").toString(), "5");
    EXPECT_FALSE(c.splitPlan("every", 5, {}).value("error").toString().isEmpty()) << "one part: nothing to split";

    const QStringList files = written(c, [&] { return c.splitDocument("every", 2, {}, true); });
    EXPECT_EQ(names(files), (std::vector<std::string>{"book (part 1).pdf", "book (part 2).pdf", "book (part 3).pdf"}));
    const std::vector<size_t> counts{2, 2, 1};
    const std::vector<const char*> first{"bookone", "bookthree", "bookfive"};
    for (int i = 0; i < files.size(); ++i) {
        auto loaded = DocumentSession::loadFile(files[i].toStdString());
        ASSERT_TRUE(loaded.document) << loaded.error;
        EXPECT_EQ(loaded.document->getPageCount(), counts[static_cast<size_t>(i)]);
        EXPECT_TRUE(hasText(*loaded.document, 0, first[static_cast<size_t>(i)]));
    }
    EXPECT_EQ(&current(c), &s) << "the parts are not opened";
    EXPECT_EQ(s.getDocument()->getPageCount(), 5u) << "the document stays as it is";

    const QStringList at = written(c, [&] { return c.splitDocument("selected", 0, {3}, false); });
    ASSERT_EQ(at.size(), 2);
    auto second = DocumentSession::loadFile(at[1].toStdString());
    ASSERT_TRUE(second.document) << second.error;
    EXPECT_EQ(second.document->getPageCount(), 2u);
    EXPECT_TRUE(hasText(*second.document, 0, "bookfour"));
}

TEST_F(PageFilesTest, aProtectedDocumentIsExtractedProtectedAndNeverAsXopp) {
    protect(root / "lecture.pdf", root / "locked.pdf", "sesame");
    AppController c;
    EXPECT_FALSE(open(c, root / "locked.pdf")) << "it asks for the password";
    ASSERT_TRUE(c.openWithPassword("sesame"));
    DocumentSession& source = current(c);
    ASSERT_TRUE(source.isProtected());
    EXPECT_FALSE(c.extractDraft({0}).value("xoppAllowed").toBool());
    EXPECT_FALSE(c.extractPages({0}, "Open", false, false)) << "a .xopp cannot be protected";
    EXPECT_FALSE(fs::exists(root / "Open.xopp"));

    const QStringList files = written(c, [&] { return c.extractPages({1}, "Secret", true, false); });
    ASSERT_EQ(names(files), (std::vector<std::string>{"Secret.pdf"}));
    EXPECT_TRUE(PdfEncryption::probe(root / "Secret.pdf").isProtected()) << "protected with the same password";
    EXPECT_TRUE(PdfEncryption::probe(root / "Secret.pdf", "sesame").readable);
    DocumentSession& opened = current(c);
    EXPECT_EQ(opened.getFilePath(), root / "Secret.pdf") << "opened with the password it has";
    EXPECT_TRUE(pageHasText(opened, 0, "lecturetwo"));

    // Pictures cannot be protected: refused
    c.tabManager().setCurrentIndex(c.tabManager().indexOf(&source));
    EXPECT_FALSE(c.imageExportDraft().value("refused").toString().isEmpty());
    EXPECT_FALSE(c.exportPageImages({0}, url(root / "pictures"), 100, false, "png"));
    EXPECT_FALSE(fs::exists(root / "pictures"));
}

// --- pages as pictures (A8) -------------------------------------------------------------------------------------

TEST_F(PageFilesTest, pagesAreExportedAsPicturesNamedByTheirPage) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    const QStringList files = images(c, {0, 2}, 100, false, "png");
    ASSERT_EQ(names(files), (std::vector<std::string>{"lecture-p001.png", "lecture-p003.png"}));
    QImage page(files[1]);
    ASSERT_FALSE(page.isNull());
    EXPECT_EQ(page.width(), 827) << "595 pt at 100 dpi";
    EXPECT_EQ(page.height(), 1170);
    EXPECT_EQ(page.pixelColor(5, 5), QColor(Qt::white)) << "white paper";
    EXPECT_NEAR(page.dotsPerMeterX(), 3937, 2) << "its resolution in the file";
    EXPECT_EQ(c.imageExportDraft().value("dpi").toInt(), 100) << "remembered";
    EXPECT_EQ(fs::path(c.imageExportDraft().value("folder").toUrl().toLocalFile().toStdString()), root / "pictures");

    const QStringList jpeg = images(c, {1}, 72, false, "jpg");
    ASSERT_EQ(names(jpeg), (std::vector<std::string>{"lecture-p002.jpg"}));
    EXPECT_FALSE(QImage(jpeg[0]).isNull());
    // One page exported: on the clipboard too (as a PNG)
    const QImage pasted = QImage::fromData(QGuiApplication::clipboard()->mimeData()->data("image/png"), "PNG");
    EXPECT_EQ(pasted.size(), QSize(595, 842));
}

TEST_F(PageFilesTest, aPageIsCopiedAsAHighResolutionImage) {
    AppController c;
    ASSERT_TRUE(open(c, root / "lecture.pdf"));
    c.setPageImageDpi(300);
    EXPECT_EQ(c.pageImageDpi(), 300);
    QSignalSpy copied(&c, &AppController::pageImageCopied);
    QSignalSpy toast(&c, &AppController::pageActionDone);
    ASSERT_TRUE(c.copyPagesAsImage({2}));
    ASSERT_TRUE(waitFor([&] { return copied.count() > 0; }));
    EXPECT_EQ(copied.first().at(0).toInt(), 2);
    EXPECT_EQ(copied.first().at(3).toString(), QString());
    // 595 × 842 points at 300 dpi
    const QSize expected(2480, 3509);
    EXPECT_EQ(copied.first().at(1).toSize(), expected);
    EXPECT_EQ(copied.first().at(2).toInt(), 300);
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    ASSERT_TRUE(mime);
    ASSERT_TRUE(mime->hasFormat("image/png"));
    const QImage png = QImage::fromData(mime->data("image/png"), "PNG");
    EXPECT_EQ(png.size(), expected);
    EXPECT_NEAR(png.dotsPerMeterX() * 0.0254, 300, 1) << "its resolution in the PNG: pasted at the page's size";
    EXPECT_EQ(png.pixelColor(5, 5), QColor(Qt::white));
    ASSERT_FALSE(toast.isEmpty());
    EXPECT_EQ(toast.last().at(0).toString(), "Page 3 copied as an image (2480×3509)");

    // A page so large that it would be too many pixels: less, and the toast says so
    c.setPageImageDpi(1200);
    copied.clear();
    ASSERT_TRUE(c.copyPagesAsImage({0}));
    ASSERT_TRUE(waitFor([&] { return copied.count() > 0; }));
    const QSize capped = copied.first().at(1).toSize();
    EXPECT_LE(double(capped.width()) * capped.height(), 32.0 * 1024 * 1024);
    EXPECT_LT(copied.first().at(2).toInt(), 1200);
    EXPECT_TRUE(toast.last().at(0).toString().contains("too large")) << toast.last().at(0).toString().toStdString();
    c.setPageImageDpi(300);
}

TEST_F(PageFilesTest, inkOnlyPagesCanBeExportedOnATransparentBackground) {
    AppController c;
    c.newDocument();
    ASSERT_TRUE(c.saveAs(url(root / "sketch.xopp")));
    DocumentSession& s = current(c);
    drawStroke(s, 0);
    const QStringList clear = images(c, {}, 72, true, "png");
    ASSERT_EQ(names(clear), (std::vector<std::string>{"sketch-p001.png"}));
    QImage picture(clear[0]);
    ASSERT_FALSE(picture.isNull());
    EXPECT_EQ(picture.pixelColor(5, 5).alpha(), 0) << "no paper";
    EXPECT_GT(picture.pixelColor(200, 200).alpha(), 200) << "the ink";
    const QStringList paper = images(c, {}, 72, false, "png");
    EXPECT_EQ(QImage(paper[0]).pixelColor(5, 5).alpha(), 255) << "with its paper";
}
