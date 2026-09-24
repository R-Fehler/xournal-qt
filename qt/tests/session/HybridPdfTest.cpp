/*
 * xournal-qt: the hybrid PDF (qt/docs/hybrid-pdf.md): the file is valid, other apps see our drawing as we draw it,
 * and it opens again as the same document.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <cstdlib>
#include <fstream>
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
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"

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
    t->setColor(Color(0x000080U));
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
    addStroke(p1->getSelectedLayer(), StrokeTool::PEN, Color(0xcc0000U), 2.26,
              {Point(100, 200, 1.0), Point(150, 230, 3.0), Point(200, 210, 5.0), Point(260, 260, 2.0)});
    auto* highlights = new Layer();
    highlights->setName("Highlights");
    p1->getLayers().push_back(highlights);  // (addLayer is for the LayerController)
    addStroke(highlights, StrokeTool::HIGHLIGHTER, Color(0xffff00U), 12, {Point(60, 95), Point(220, 95)});
    addText(doc->getPage(1)->getSelectedLayer(), "a note in the margin", 80, 400);
    auto ruled = std::make_shared<XojPage>(595, 842);
    ruled->setBackgroundType(PageType(PageTypeFormat::Ruled));
    addStroke(ruled->getSelectedLayer(), StrokeTool::PEN, Color(0x0000ffU), 1.41,
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

class HybridPdfTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
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
