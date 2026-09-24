/*
 * xournal-qt: the hybrid PDF (qt/docs/hybrid-pdf.md): the file is valid, other apps see our drawing as we draw it,
 * and it opens again as the same document.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION == 11
#define POINTERHOLDER_TRANSITION 4
#endif
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "control/ExportHelper.h"
#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "undo/UndoRedoHandler.h"

using namespace xqt;

namespace {
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

Stroke* addStroke(Layer* layer, StrokeTool tool, Color color, double width, std::vector<Point> points) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(tool);
    s->setColor(color);
    s->setWidth(width);
    for (const auto& p: points) {
        s->addPoint(p);
    }
    Stroke* raw = s.get();
    layer->addElement(std::move(s));
    return raw;
}

Text* addText(Layer* layer, const std::string& text, double x, double y) {
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(XojFont("Sans", 14));
    t->setColor(Color(0xff000080U));
    t->move(x, y);
    Text* raw = t.get();
    layer->addElement(std::move(t));
    return raw;
}

/// A PDF of three pages, annotated: a pen stroke with pressure on page 1, a highlighter on a second layer of it, a
/// text on page 2; after page 1 a ruled page with a stroke; page 3 untouched.
std::unique_ptr<Document> annotated(const fs::path& pdf) {
    makeTextPdf(pdf, {"lectureone", "lecturetwo", "lecturethree"});
    auto loaded = DocumentSession::loadFile(pdf);
    EXPECT_TRUE(loaded.document) << loaded.error;
    auto doc = std::move(loaded.document);
    PageRef p1 = doc->getPage(0);
    addStroke(p1->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2.26,
              {Point(100, 200, 1.0), Point(150, 230, 3.0), Point(200, 210, 5.0), Point(260, 260, 2.0)});
    auto* highlights = new Layer();
    highlights->setName("Highlights");
    p1->getLayers().push_back(highlights);  // (addLayer is for the LayerController)
    addStroke(highlights, StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 12, {Point(60, 95), Point(220, 95)});
    addText(doc->getPage(1)->getSelectedLayer(), "a note in the margin", 80, 400);
    auto ruled = std::make_shared<XojPage>(595, 842);
    ruled->setBackgroundType(PageType(PageTypeFormat::Ruled));
    addStroke(ruled->getSelectedLayer(), StrokeTool::PEN, Color(0xff0000ffU), 1.41,
              {Point(300, 300), Point(400, 350), Point(420, 500)});
    doc->insertPage(ruled, 1);
    for (size_t i = 0; i < doc->getPageCount(); ++i) {  // (as loadFile does)
        for (const Layer* l: doc->getPage(i)->getLayers()) {
            for (const auto& e: l->getElementsView()) {
                e->getBoundingBox();
            }
        }
    }
    return doc;
}

/// qpdf --check
std::string qpdfCheck(const fs::path& pdf, int& code) {
    std::ostringstream out, err;
    QPDFJob job;
    job.setOutputStreams(&out, &err);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    code = job.getExitCode();
    return out.str() + err.str();
}

/// A page as poppler draws it (with annotations), white behind, 1 px per point.
cairo_surface_t* render(const fs::path& pdf, size_t page) {
    XojPdfDocument doc;
    GError* error = nullptr;
    EXPECT_TRUE(doc.load(pdf, "", &error)) << pdf;
    if (error) {
        g_error_free(error);
    }
    XojPdfPageSPtr p = doc.getPage(page);
    const int w = static_cast<int>(std::ceil(p->getWidth())), h = static_cast<int>(std::ceil(p->getHeight()));
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    p->render(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

struct Diff {
    double mean = 0;     ///< mean difference per channel (0..255)
    double differ = 0;   ///< share of pixels with a channel off by more than 48
};
Diff compare(cairo_surface_t* a, cairo_surface_t* b) {
    Diff d;
    const int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
    EXPECT_EQ(w, cairo_image_surface_get_width(b));
    EXPECT_EQ(h, cairo_image_surface_get_height(b));
    const unsigned char* pa = cairo_image_surface_get_data(a);
    const unsigned char* pb = cairo_image_surface_get_data(b);
    const int sa = cairo_image_surface_get_stride(a), sb = cairo_image_surface_get_stride(b);
    double sum = 0;
    long off = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int worst = 0;
            for (int c = 0; c < 3; ++c) {
                const int v = std::abs(int(pa[y * sa + 4 * x + c]) - int(pb[y * sb + 4 * x + c]));
                sum += v;
                worst = std::max(worst, v);
            }
            off += worst > 48;
        }
    }
    d.mean = sum / (3.0 * w * h);
    d.differ = double(off) / (double(w) * h);
    return d;
}

/// Whether the page has any pixel that is not white.
bool inked(cairo_surface_t* s, int x0, int y0, int x1, int y1) {
    const unsigned char* p = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (p[y * stride + 4 * x] < 200 || p[y * stride + 4 * x + 1] < 200 || p[y * stride + 4 * x + 2] < 200) {
                return true;
            }
        }
    }
    return false;
}

/// What a document holds, as text: pages (size, background, the PDF text on it), layers, elements.
std::string describe(Document& doc) {
    std::ostringstream out;
    out.precision(6);
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        PageRef p = doc.getPage(i);
        out << "page " << p->getWidth() << "x" << p->getHeight() << " " << int(p->getBackgroundType().format);
        if (p->getBackgroundType().isPdfPage()) {
            XojPdfPageSPtr pdf = doc.getPdfPage(p->getPdfPageNr());
            out << " pdf:" << (pdf ? pdf->selectText(XojPdfRectangle(0, 0, 595, 842), XojPdfPageSelectionStyle::Linear)
                                   : std::string("missing"));
        }
        out << "\n";
        for (const Layer* l: p->getLayers()) {
            out << " layer '" << l->getName() << "' " << l->isVisible() << "\n";
            for (const auto& e: l->getElementsView()) {
                out << "  " << int(e->getType()) << " #" << std::hex << uint32_t(e->getColor()) << std::dec;
                if (e->getType() == ELEMENT_STROKE) {
                    const auto* s = static_cast<const Stroke*>(e);
                    out << " tool " << int(s->getToolType()) << " w " << s->getWidth() << ":";
                    for (const Point& pt: s->getPointVector()) {
                        out << " " << pt.x << "," << pt.y << "," << pt.z;
                    }
                } else if (e->getType() == ELEMENT_TEXT) {
                    const auto* t = static_cast<const Text*>(e);
                    out << " '" << t->getText() << "' at " << t->getOrigin().x << "," << t->getOrigin().y << " "
                        << t->getFontName() << " " << t->getFontSize();
                }
                out << "\n";
            }
        }
    }
    return out.str();
}

/// describe() of the document after a round trip through a .xopp (what the .xopp format keeps: e.g. not the pressure
/// of a stroke's last point).
std::string describeAsXopp(Document& doc, const fs::path& xopp) {
    SaveHandler h;
    {
        std::shared_lock lock(doc);
        h.prepareSave(&doc, xopp);
    }
    h.saveTo(xopp);
    auto loaded = DocumentSession::loadFile(xopp);
    EXPECT_TRUE(loaded.document) << loaded.error;
    return loaded.document ? describe(*loaded.document) : std::string();
}

/// Change the file with qpdf (as another app would), keeping its size-and-time key new.
template <class F>
void editWithQpdf(const fs::path& file, F&& change) {
    QPDF q;
    q.processFile(file.string().c_str());
    change(q);
    const fs::path tmp = fs::path(file) += ".tmp";
    QPDFWriter w(q, tmp.string().c_str());
    w.write();
    fs::rename(tmp, file);
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
}

QPDFObjectHandle ourAnnot(QPDF& q, size_t page, const std::string& name) {
    for (auto& a: QPDFPageDocumentHelper(q).getAllPages().at(page).getAnnotations()) {
        if (a.getObjectHandle().getKey("/NM").getUTF8Value() == name) {
            return a.getObjectHandle();
        }
    }
    return QPDFObjectHandle::newNull();
}

class HybridPdfTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

TEST_F(HybridPdfTest, writesAValidPdfWithOurAnnotationsDataAndMarker) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    const auto r = HybridPdf::write(*doc, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.pages, 4u);
    EXPECT_EQ(r.annotations, 4u) << "one per layer with content: pen, highlighter, text, ruled page";

    if (const char* sample = std::getenv("XQT_HYBRID_SAMPLE")) {  // a sample for other PDF apps
        std::error_code ec;
        fs::copy_file(out, sample, fs::copy_options::overwrite_existing, ec);
    }
    int code = -1;
    const std::string report = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << report;
    EXPECT_NE(report.find("No syntax or stream encoding errors"), std::string::npos) << report;

    QPDF q;
    q.processFile(out.string().c_str());
    QPDFObjectHandle marker = q.getRoot().getKey("/XournalQt");
    ASSERT_TRUE(marker.isDictionary());
    EXPECT_EQ(marker.getKey("/Version").getIntValue(), HybridPdf::FORMAT_VERSION);
    EXPECT_EQ(marker.getKey("/Annots").getKeys().size(), 4u);
    EXPECT_TRUE(QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(HybridPdf::DATA_NAME));
    EXPECT_FALSE(q.getTrailer().getKey("/Info").hasKey("/XournalQtPages")) << "never a merged PDF";

    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 4u);
    std::vector<std::string> kinds;
    for (auto& page: pages) {
        std::string k;
        for (auto& a: page.getAnnotations()) {
            QPDFObjectHandle o = a.getObjectHandle();
            k += o.getKey("/Subtype").getName() + " ";
            EXPECT_EQ(o.getKey("/NM").getUTF8Value().rfind(HybridPdf::NAME_PREFIX, 0), 0u);
            EXPECT_TRUE(o.getKey("/AP").getKey("/N").isStream());
            EXPECT_TRUE(o.hasKey("/XournalQt"));
        }
        kinds.push_back(k);
    }
    EXPECT_EQ(kinds, (std::vector<std::string>{"/Ink /Ink ", "/Ink ", "/Stamp ", ""}));
    // The pen stroke's points, in PDF space (y up)
    QPDFObjectHandle ink = pages[0].getAnnotations()[0].getObjectHandle().getKey("/InkList").getArrayItem(0);
    ASSERT_EQ(ink.getArrayNItems(), 8);
    EXPECT_NEAR(ink.getArrayItem(0).getNumericValue(), 100, 0.05);
    EXPECT_NEAR(ink.getArrayItem(1).getNumericValue(), 842 - 200, 0.05);
    EXPECT_EQ(pages[2].getAnnotations()[0].getObjectHandle().getKey("/Contents").getUTF8Value(),
              "a note in the margin");
    // The text of the PDF is still there, on the right pages
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_FALSE(pdf.getPage(0)->findText("lectureone").empty());
    EXPECT_TRUE(pdf.getPage(1)->findText("lecturetwo").empty()) << "the ruled page";
    EXPECT_FALSE(pdf.getPage(2)->findText("lecturetwo").empty());
    EXPECT_FALSE(pdf.getPage(3)->findText("lecturethree").empty());
}

TEST_F(HybridPdfTest, popplerShowsItLikeOurPdfExport) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path hybrid = path("hybrid.pdf"), exported = path("export.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, hybrid).ok);
    ExportHelper::exportPdf(doc.get(), exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    for (size_t i = 0; i < 4; ++i) {
        cairo_surface_t* a = render(hybrid, i);
        cairo_surface_t* b = render(exported, i);
        const Diff d = compare(a, b);
        EXPECT_LT(d.mean, 0.5) << "page " << i + 1;
        EXPECT_LT(d.differ, 0.002) << "page " << i + 1;
        if (i == 0) {
            EXPECT_TRUE(inked(a, 140, 220, 160, 240)) << "the pen stroke is drawn";
        }
        cairo_surface_destroy(a);
        cairo_surface_destroy(b);
    }
}

TEST_F(HybridPdfTest, theEmbeddedDocumentOpensWithTheCleanCopyAsBackground) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    EXPECT_TRUE(HybridPdf::isHybrid(out));
    EXPECT_FALSE(HybridPdf::isHybrid(path("lecture.pdf")));

    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_TRUE(opened.changed.empty());
    Document& back = *opened.document;
    EXPECT_EQ(back.getFilepath(), out);
    EXPECT_EQ(back.getPdfFilepath(), opened.base);
    EXPECT_TRUE(HybridPdf::inCache(opened.base));
    ASSERT_EQ(back.getPageCount(), 4u);
    EXPECT_EQ(back.getPdfPageCount(), 4u);
    // The clean copy: no annotations of ours, no data, no marker; valid
    QPDF clean;
    clean.processFile(opened.base.string().c_str());
    EXPECT_FALSE(clean.getRoot().hasKey("/XournalQt"));
    EXPECT_FALSE(QPDFEmbeddedFileDocumentHelper(clean).hasEmbeddedFiles());
    for (auto& page: QPDFPageDocumentHelper(clean).getAllPages()) {
        EXPECT_TRUE(page.getAnnotations().empty());
    }
    int code = -1;
    EXPECT_EQ((qpdfCheck(opened.base, code), code), 0);
}

// --- reading ------------------------------------------------------------------------------------------------------

TEST_F(HybridPdfTest, opensAsTheSameDocument) {
    auto doc = annotated(path("lecture.pdf"));
    const std::string before = describeAsXopp(*doc, path("reference.xopp"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);

    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.hybrid);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_TRUE(loaded.warnings.empty());
    EXPECT_EQ(describe(*loaded.document), before);
    EXPECT_EQ(loaded.document->getFilepath(), out);

    // Opened in a tab, changed and saved: a hybrid PDF again, with both strokes
    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_TRUE(session.isHybrid());
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(3)->getSelectedLayer(), StrokeTool::PEN, Color(0xff008000U), 1.41,
                  {Point(10, 10, 1), Point(90, 90, 2)});
    }
    const auto r = session.save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(HybridPdf::isHybrid(out));
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describeAsXopp(*session.getDocument(), path("reference2.xopp")));
    EXPECT_NE(describe(*again.document), before);
}

TEST_F(HybridPdfTest, plainPdfsAndXoppFilesOpenAsBefore) {
    makeTextPdf(path("plain.pdf"), {"alpha", "beta"});
    auto loaded = DocumentSession::loadFile(path("plain.pdf"));
    ASSERT_TRUE(loaded.document);
    EXPECT_FALSE(loaded.hybrid);
    EXPECT_TRUE(loaded.document->getFilepath().empty());
    EXPECT_EQ(loaded.document->getPdfFilepath(), path("plain.pdf"));
    EXPECT_EQ(loaded.document->getPageCount(), 2u);

    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_FALSE(session.isHybrid());
    ASSERT_TRUE(session.saveAs(path("plain.xopp")).ok);
    EXPECT_FALSE(session.isHybrid()) << ".xopp stays the format of Save";
    EXPECT_FALSE(HybridPdf::isHybrid(path("plain.pdf")));
}

TEST_F(HybridPdfTest, annotationsOfOtherAppsStay) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    editWithQpdf(out, [](QPDF& q) {  // a comment on page 4, as another app adds it
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(3).getObjectHandle();
        QPDFObjectHandle note = q.makeIndirectObject(QPDFObjectHandle::parse(
                "<< /Type /Annot /Subtype /Text /Rect [100 100 120 120] /Contents (from another app) /NM (other-1) >>"));
        if (!page.getKey("/Annots").isArray()) {
            page.replaceKey("/Annots", QPDFObjectHandle::newArray());
        }
        page.getKey("/Annots").appendItem(note);
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty()) << "ours are unchanged";
    {
        QPDF clean;
        clean.processFile(loaded.document->getPdfFilepath().string().c_str());
        auto annots = QPDFPageDocumentHelper(clean).getAllPages().at(3).getAnnotations();
        ASSERT_EQ(annots.size(), 1u) << "the clean copy keeps it (shown by the background)";
        EXPECT_EQ(annots[0].getObjectHandle().getKey("/NM").getUTF8Value(), "other-1");
    }
    DocumentSession session(*app, std::move(loaded.document));
    ASSERT_TRUE(session.save().ok);
    QPDF saved;
    saved.processFile(out.string().c_str());
    auto pages = QPDFPageDocumentHelper(saved).getAllPages();
    EXPECT_EQ(pages.at(3).getAnnotations().size(), 1u);
    EXPECT_EQ(pages.at(3).getAnnotations()[0].getObjectHandle().getKey("/Contents").getUTF8Value(),
              "from another app");
    EXPECT_EQ(pages.at(0).getAnnotations().size(), 2u) << "ours, written once";
}

TEST_F(HybridPdfTest, inkChangedInAnotherAppIsReportedAndCanBeImported) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    const std::string pen = HybridPdf::nameOf(0, 0), ruled = HybridPdf::nameOf(1, 0);
    editWithQpdf(out, [&](QPDF& q) {
        // The pen stroke moved 50 pt to the right
        QPDFObjectHandle a = ourAnnot(q, 0, pen);
        ASSERT_TRUE(a.isDictionary());
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        for (int i = 0; i < 4; ++i) {
            rect.appendItem(QPDFObjectHandle::newReal(a.getKey("/Rect").getArrayItem(i).getNumericValue() +
                                                              (i % 2 == 0 ? 50 : 0),
                                                      1));
        }
        a.replaceKey("/Rect", rect);
        // The stroke on the ruled page deleted
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(1).getObjectHandle();
        page.replaceKey("/Annots", QPDFObjectHandle::newArray());
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(loaded.hybridChanged, (std::vector<std::string>{pen, ruled}));
    const std::string before = describe(*loaded.document);

    DocumentSession session(*app, std::move(loaded.document));
    session.setHybridChanges(loaded.hybridChanged);
    std::string error;
    ASSERT_TRUE(session.importHybridChanges(error)) << error;
    EXPECT_TRUE(session.isModified());
    Document& d = *session.getDocument();
    EXPECT_FALSE(d.getPage(0)->getLayers()[0]->getElementsView().begin() != d.getPage(0)->getLayers()[0]->getElementsView().end())
            << "the layer the other app changed is its annotation now";
    EXPECT_TRUE(d.getPage(0)->getLayers()[1]->getElementsView().begin() != d.getPage(0)->getLayers()[1]->getElementsView().end())
            << "the highlighter was not changed";
    {
        QPDF clean;
        clean.processFile(d.getPdfFilepath().string().c_str());
        auto annots = QPDFPageDocumentHelper(clean).getAllPages().at(0).getAnnotations();
        ASSERT_EQ(annots.size(), 1u);
        EXPECT_EQ(annots[0].getObjectHandle().getKey("/NM").getUTF8Value(), "imported:" + pen);
        EXPECT_FALSE(annots[0].getObjectHandle().hasKey("/XournalQt"));
    }
    session.getUndoRedoHandler()->undo();
    session.getUndoRedoHandler()->undo();
    EXPECT_EQ(describe(d), before) << "undone: the Xournal data is back";
    session.getUndoRedoHandler()->redo();
    session.getUndoRedoHandler()->redo();

    // Saved, it is ours again (a plain annotation kept, and a new one of ours for what is left)
    ASSERT_TRUE(session.save().ok);
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    EXPECT_TRUE(again.hybridChanged.empty());
}

TEST_F(HybridPdfTest, notesSavedIntoThePdfItselfKeepTheOriginalOnce) {
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo"});
    const std::string original = [&] {
        std::ifstream in(path("lecture.pdf"), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }();
    auto loaded = DocumentSession::loadFile(path("lecture.pdf"));
    DocumentSession session(*app, std::move(loaded.document));
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(1)->getSelectedLayer(), StrokeTool::PEN, Color(0xffff0000U), 2,
                  {Point(10, 10), Point(200, 300)});
    }
    ASSERT_TRUE(session.saveAsHybrid(path("lecture.pdf")).ok);
    EXPECT_TRUE(session.isHybrid());
    EXPECT_TRUE(HybridPdf::isHybrid(path("lecture.pdf")));
    ASSERT_TRUE(fs::exists(path("lecture.original.pdf")));
    {
        std::ifstream in(path("lecture.original.pdf"), std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), original);
    }
    EXPECT_NE(session.getDocument()->getPdfFilepath(), path("lecture.pdf")) << "the pages come from a copy now";
    // Saved again: still right, the original untouched
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffff0000U), 2,
                  {Point(10, 10), Point(200, 300)});
    }
    ASSERT_TRUE(session.save().ok);
    auto again = DocumentSession::loadFile(path("lecture.pdf"));
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describe(*session.getDocument()));
    std::ifstream in(path("lecture.original.pdf"), std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), original);
}

TEST_F(HybridPdfTest, exportsAPlainXoppForXournalpp) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.notes.pdf");
    ASSERT_TRUE(HybridPdf::write(*doc, out).ok);
    auto loaded = DocumentSession::loadFile(out);
    DocumentSession session(*app, std::move(loaded.document));
    const fs::path xopp = path("lecture.notes.xopp");
    EXPECT_EQ(DocumentSession::exportPdfFor(xopp), path(".lecture.notes.pages.pdf")) << "the hybrid PDF has the name";
    ASSERT_TRUE(session.exportXopp(xopp).ok);
    EXPECT_EQ(session.getFilePath(), out) << "the document stays the hybrid PDF";
    auto exported = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(exported.document) << exported.error;
    EXPECT_FALSE(exported.hybrid);
    EXPECT_EQ(exported.document->getPdfFilepath(), path(".lecture.notes.pages.pdf"));
    EXPECT_EQ(describe(*exported.document), describe(*session.getDocument()));
    QPDF base;
    base.processFile(path(".lecture.notes.pages.pdf").string().c_str());
    EXPECT_FALSE(base.getRoot().hasKey("/XournalQt"));
    for (auto& page: QPDFPageDocumentHelper(base).getAllPages()) {
        EXPECT_TRUE(page.getAnnotations().empty()) << "no ink twice in Xournal++";
    }
    // A notes document without PDF of its own: "name.pdf" next to it
    EXPECT_EQ(DocumentSession::exportPdfFor(path("other.xopp")), path("other.pdf"));
}

// --- measurements ---------------------------------------------------------------------------------------------------

// XQT_BENCH_HYBRID=<pdf>: notes on every 25th page (strokes with pressure, a highlighter, a text); the time and size
// of our PDF export and of the hybrid PDF, opening it (clean copy made, then from the cache) and saving it again.
TEST_F(HybridPdfTest, benchSaveAndOpen) {
    const char* source = std::getenv("XQT_BENCH_HYBRID");
    if (!source) {
        GTEST_SKIP() << "set XQT_BENCH_HYBRID=<pdf>";
    }
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a) {
        return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
    };
    auto size = [](const fs::path& p) { return static_cast<double>(fs::file_size(p)) / 1024; };
    auto t = Clock::now();
    auto loaded = DocumentSession::loadFile(source);
    ASSERT_TRUE(loaded.document);
    Document& doc = *loaded.document;
    const double openPlain = ms(t);
    size_t noted = 0;
    for (size_t i = 0; i < doc.getPageCount(); i += 25, ++noted) {
        Layer* layer = doc.getPage(i)->getSelectedLayer();
        for (int k = 0; k < 20; ++k) {
            std::vector<Point> pts;
            for (int j = 0; j < 60; ++j) {
                pts.emplace_back(60 + j * 6, 100 + k * 20 + 5 * std::sin(j / 3.0), 1 + (j % 10) / 5.0);
            }
            addStroke(layer, StrokeTool::PEN, Color(0xff000080U), 1.41, pts);
        }
        addStroke(layer, StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 12, {Point(60, 80), Point(400, 80)});
        addText(layer, "a note on page " + std::to_string(i + 1), 60, 600);
    }
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            for (const auto& e: l->getElementsView()) {
                e->getBoundingBox();
            }
        }
    }
    const fs::path exported = path("export.pdf"), hybrid = path("hybrid.pdf");
    t = Clock::now();
    ExportHelper::exportPdf(&doc, exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    const double exportMs = ms(t);
    t = Clock::now();
    const auto r = HybridPdf::write(doc, hybrid);
    const double hybridMs = ms(t);
    ASSERT_TRUE(r.ok) << r.error;
    t = Clock::now();
    auto first = DocumentSession::loadFile(hybrid);
    const double openFirst = ms(t);
    ASSERT_TRUE(first.document);
    t = Clock::now();
    auto second = DocumentSession::loadFile(hybrid);
    const double openCached = ms(t);
    t = Clock::now();
    const auto again = HybridPdf::write(*second.document, path("again.pdf"));
    const double againMs = ms(t);
    ASSERT_TRUE(again.ok);
    std::cout << source << ": " << doc.getPageCount() << " pages, notes on " << noted << " (" << r.annotations
              << " annotations)\n"
              << "  source " << size(source) << " KB, opened as a plain PDF in " << openPlain << " ms\n"
              << "  our PDF export: " << exportMs << " ms, " << size(exported) << " KB\n"
              << "  hybrid PDF:     " << hybridMs << " ms, " << size(hybrid) << " KB\n"
              << "  open hybrid:    " << openFirst << " ms (clean copy made), " << openCached << " ms (cached)\n"
              << "  save again from the clean copy: " << againMs << " ms, " << size(path("again.pdf")) << " KB\n";
}

// A PDF page shown twice (a duplicated page) is two pages in the file, each with its own annotations.
TEST_F(HybridPdfTest, aPageShownTwiceHasItsOwnAnnotations) {
    auto doc = annotated(path("lecture.pdf"));
    auto copy = std::make_shared<XojPage>(*doc->getPage(0));  // (the pen stroke and the highlighter too)
    doc->insertPage(copy, 1);
    for (const Layer* l: copy->getLayers()) {
        for (const auto& e: l->getElementsView()) {
            e->getBoundingBox();
        }
    }
    const fs::path out = path("twice.pdf");
    const auto r = HybridPdf::write(*doc, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.pages, 5u);
    int code = -1;
    const std::string report = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << report;
    QPDF q;
    q.processFile(out.string().c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 5u);
    EXPECT_EQ(pages[0].getAnnotations().size(), 2u);
    EXPECT_EQ(pages[1].getAnnotations().size(), 2u);
    EXPECT_NE(pages[0].getObjectHandle().getObjGen(), pages[1].getObjectHandle().getObjGen());
    EXPECT_EQ(pages[1].getAnnotations()[0].getObjectHandle().getKey("/P").getObjGen(),
              pages[1].getObjectHandle().getObjGen());
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_EQ(describe(*loaded.document), describeAsXopp(*doc, path("reference.xopp")));
}
