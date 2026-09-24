/*
 * xournal-qt: the hybrid PDF (qt/docs/hybrid-pdf.md): the file is valid, other apps see our drawing as we draw it,
 * and it opens again as the same document.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
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
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/ArchivePdf.h"
#include "session/HybridPdf.h"
#include "session/IncrementalPdf.h"
#include "undo/UndoRedoHandler.h"

#include "config.h"

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
    editWithQpdf(out, [](QPDF& q) {  // comments on page 4 and on the ruled page 2, as another app adds them
        for (int n: {3, 1}) {
            QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(n).getObjectHandle();
            QPDFObjectHandle note = q.makeIndirectObject(QPDFObjectHandle::parse(
                    "<< /Type /Annot /Subtype /Text /Rect [100 100 120 120] /Contents (from another app) /NM (other-" +
                    std::to_string(n) + ") >>"));
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            if (page.getKey("/Annots").isArray()) {
                for (int i = 0; i < page.getKey("/Annots").getArrayNItems(); ++i) {
                    annots.appendItem(page.getKey("/Annots").getArrayItem(i));
                }
            }
            annots.appendItem(note);
            page.replaceKey("/Annots", annots);
        }
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty()) << "ours are unchanged";
    {
        QPDF clean;
        clean.processFile(loaded.document->getPdfFilepath().string().c_str());
        auto annots = QPDFPageDocumentHelper(clean).getAllPages().at(3).getAnnotations();
        ASSERT_EQ(annots.size(), 1u) << "the clean copy keeps it (shown by the background)";
        EXPECT_EQ(annots[0].getObjectHandle().getKey("/NM").getUTF8Value(), "other-3");
    }
    DocumentSession session(*app, std::move(loaded.document));
    {  // the ruled page moves to the end: its comment goes with it
        std::unique_lock lock(*session.getDocument());
        PageRef ruled = session.getDocument()->getPage(1);
        session.getDocument()->deletePage(1);
        session.getDocument()->insertPage(ruled, 3);
    }
    ASSERT_TRUE(session.save().ok);
    QPDF saved;
    saved.processFile(out.string().c_str());
    auto pages = QPDFPageDocumentHelper(saved).getAllPages();
    ASSERT_EQ(pages.at(2).getAnnotations().size(), 1u) << "the last PDF page, now third";
    EXPECT_EQ(pages.at(2).getAnnotations()[0].getObjectHandle().getKey("/Contents").getUTF8Value(),
              "from another app");
    EXPECT_EQ(pages.at(0).getAnnotations().size(), 2u) << "ours, written once";
    std::vector<std::string> onRuled;
    for (auto& a: pages.at(3).getAnnotations()) {
        onRuled.push_back(a.getObjectHandle().getKey("/NM").getUTF8Value());
    }
    EXPECT_EQ(onRuled, (std::vector<std::string>{"other-1", HybridPdf::nameOf(3, 0)}))
            << "a page with a generated background keeps the other app's comment too";
    // (a copy made by a full write points at its page; appended, the other app's annotation stays as it was, on the
    // same page object)
    QPDFObjectHandle onPage = pages.at(3).getAnnotations()[0].getObjectHandle().getKey("/P");
    if (!onPage.isNull()) {
        EXPECT_EQ(onPage.getObjGen(), pages.at(3).getObjectHandle().getObjGen());
    }
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);

    // "Save as" .xopp: its pages go next to it, not a reference into the cache
    ASSERT_TRUE(session.saveAs(path("notes.xopp")).ok);
    EXPECT_FALSE(session.isHybrid());
    EXPECT_EQ(session.getDocument()->getPdfFilepath(), path("notes.pdf"));
    auto xopp = DocumentSession::loadFile(path("notes.xopp"));
    ASSERT_TRUE(xopp.document);
    EXPECT_EQ(describe(*xopp.document), describe(*session.getDocument()));
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

// "Keep it updated for Xournal++": the hybrid PDF records the .xopp it keeps (relative to it), a session reads that
// from the file, and a save without it drops it.
TEST_F(HybridPdfTest, recordsTheXoppItKeepsForXournalpp) {
    auto doc = annotated(path("lecture.pdf"));
    DocumentSession session(*app, std::move(doc));
    const fs::path out = path("lecture.notes.pdf"), xopp = path("lecture.xopp");
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::Hybrid;
    request.target = out;
    request.exportXopp = xopp;
    request.recordExport = xopp;
    ASSERT_TRUE(session.saveNow(request).ok);
    EXPECT_TRUE(fs::exists(xopp));
    EXPECT_EQ(HybridPdf::xoppExportOf(out), xopp);
    EXPECT_EQ(session.xoppExport(), xopp);
    QPDF q;
    q.processFile(out.string().c_str());
    EXPECT_EQ(q.getRoot().getKey("/XournalQt").getKey("/XoppExport").getUTF8Value(), "lecture.xopp")
            << "relative: it follows the PDF";

    auto reopened = DocumentSession::loadFile(out);
    ASSERT_TRUE(reopened.document);
    DocumentSession again(*app, std::move(reopened.document));
    EXPECT_EQ(again.xoppExport(), xopp) << "read from the file";
    ASSERT_TRUE(again.save().ok);
    EXPECT_EQ(again.xoppExport(), fs::path()) << "a save that does not record it drops it";
    EXPECT_EQ(HybridPdf::xoppExportOf(out), fs::path());
}

// The .xopp a document was goes to the trash (or is written over): the document takes its pages from a copy in the
// cache first, the same pages under the same numbers.
TEST_F(HybridPdfTest, aDocumentLetsGoOfTheFileItShowsPagesFrom) {
    makeTextPdf(path("pages.pdf"), {"pageone", "pagetwo"});
    auto loaded = DocumentSession::loadFile(path("pages.pdf"));
    DocumentSession session(*app, std::move(loaded.document));
    std::string error;
    ASSERT_TRUE(session.detachBackground({path("other.pdf")}, error));
    EXPECT_EQ(session.getDocument()->getPdfFilepath(), path("pages.pdf")) << "not one of them: stays";
    ASSERT_TRUE(session.detachBackground({path("pages.pdf")}, error)) << error;
    const fs::path copy = session.getDocument()->getPdfFilepath();
    EXPECT_TRUE(HybridPdf::inCache(copy)) << copy;
    fs::remove(path("pages.pdf"));
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(1)->getSelectedLayer(), StrokeTool::PEN, Color(0xffff0000U), 2,
                  {Point(10, 10), Point(200, 300)});
    }
    ASSERT_TRUE(session.saveAsHybrid(path("pages.notes.pdf")).ok) << "the pages are still there";
    auto again = DocumentSession::loadFile(path("pages.notes.pdf"));
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describe(*session.getDocument()));
}

// Share → For Xournal++: "name.xopp" with upstream's attached PDF "name.xopp.bg.pdf" (domain "attach"), which upstream's
// LoadHandler opens with the pages right, also after the two were moved elsewhere together.
TEST_F(HybridPdfTest, aCopyForXournalppTakesItsPdfAlongAsAttachment) {
    auto doc = annotated(path("lecture.pdf"));
    const std::string expected = describeAsXopp(*doc, path("roundtrip.xopp"));  // (as a .xopp gives it back)
    DocumentSession session(*app, std::move(doc));
    fs::create_directories(path("out"));
    const fs::path xopp = path("out") / "lecture.xopp";
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::ExportXopp;
    request.target = xopp;
    request.attachedPdf = true;
    ASSERT_TRUE(session.saveNow(request).ok);
    EXPECT_TRUE(fs::exists(path("out") / "lecture.xopp.bg.pdf"));
    EXPECT_FALSE(fs::exists(path("out") / "lecture.pdf"));
    EXPECT_FALSE(fs::exists(path("out") / ".lecture.pages.pdf"));
    auto check = [&](const fs::path& file) {
        auto loaded = DocumentSession::loadFile(file);  // (upstream's LoadHandler)
        ASSERT_TRUE(loaded.document) << loaded.error;
        Document& d = *loaded.document;
        EXPECT_TRUE(d.isAttachPdf());
        fs::path bg = file;
        bg += ".bg.pdf";
        EXPECT_EQ(d.getPdfFilepath(), bg);
        EXPECT_EQ(describe(d), expected);
        for (size_t i = 0; i < d.getPageCount(); ++i) {
            if (d.getPage(i)->getBackgroundType().isPdfPage()) {
                EXPECT_EQ(d.getPage(i)->getPdfPageNr(), i) << "base page i";
            }
        }
    };
    check(xopp);
    fs::rename(path("out"), path("moved"));
    check(path("moved") / "lecture.xopp");

    // Notes without a PDF: the .xopp alone
    DocumentSession notes(*app);
    request.target = path("moved") / "notes.xopp";
    ASSERT_TRUE(notes.saveNow(request).ok);
    EXPECT_TRUE(fs::exists(path("moved") / "notes.xopp"));
    EXPECT_FALSE(fs::exists(path("moved") / "notes.xopp.bg.pdf"));
}

// Share → a PDF copy of a .xopp: the document keeps its file, format and unsaved changes. Never over its own PDF.
TEST_F(HybridPdfTest, aPdfCopyLeavesTheDocumentAsItIs) {
    auto doc = annotated(path("lecture.pdf"));
    DocumentSession session(*app, std::move(doc));
    ASSERT_TRUE(session.saveAs(path("lecture.xopp")).ok);
    {
        std::unique_lock lock(*session.getDocument());
        addStroke(session.getDocument()->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xff00ff00U), 2,
                  {Point(10, 10), Point(300, 300)});
    }
    const bool modified = session.isModified();
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::ExportHybrid;
    request.target = path("copy");
    ASSERT_TRUE(session.saveNow(request).ok);
    EXPECT_EQ(session.getFilePath(), path("lecture.xopp"));
    EXPECT_FALSE(session.isHybrid());
    EXPECT_EQ(session.isModified(), modified);
    ASSERT_TRUE(HybridPdf::isHybrid(path("copy.pdf")));
    auto copy = DocumentSession::loadFile(path("copy.pdf"));
    ASSERT_TRUE(copy.document);
    EXPECT_EQ(describe(*copy.document), describeAsXopp(*session.getDocument(), path("roundtrip.xopp")));
    request.target = path("lecture.pdf");
    EXPECT_FALSE(session.saveNow(request).ok) << "not over the PDF it shows";
    EXPECT_FALSE(HybridPdf::isHybrid(path("lecture.pdf")));
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

TEST_F(HybridPdfTest, linksOfMarkdownBoxesBecomeLinksOtherViewersFollow) {
    // A lecture PDF with its notes, and a document of notes that links to it (a Markdown box on page 1)
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
    {
        auto loaded = DocumentSession::loadFile(path("lecture.pdf"));
        ASSERT_TRUE(loaded.document);
        ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, path("lecture.xopp")).ok);
    }
    Document doc(nullptr);
    auto page = std::make_shared<XojPage>(595, 842);
    auto* markdown = new Layer();
    markdown->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().insert(page->getLayers().begin(), markdown);
    auto box = std::make_unique<Text>();
    box->setText("See [the lecture](lecture.xopp#page=3&pdfpage=2), [the PDF](lecture.pdf#page=3), "
                 "[the web](https://example.org/x) and [notes](other.md#heading=a).");
    box->setFont(XojFont("Sans", 10));
    box->setWrap(400);
    box->setTransformation(xoj::util::Matrix::TRANSLATION(56, 56));
    markdown->addElement(std::move(box));
    doc.addPage(page);
    const fs::path out = path("notes.pdf");
    const auto r = HybridPdf::write(doc, out);
    ASSERT_TRUE(r.ok) << r.error;

    QPDF pdf;
    pdf.processFile(out.string().c_str());
    std::vector<std::string> actions;
    for (auto& p: QPDFPageDocumentHelper(pdf).getAllPages()) {
        QPDFObjectHandle annots = p.getObjectHandle().getKey("/Annots");
        for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
            QPDFObjectHandle a = annots.getArrayItem(i);
            if (a.getKey("/Subtype").getName() != "/Link") {
                continue;
            }
            QPDFObjectHandle act = a.getKey("/A");
            const QPDFObjectHandle::Rectangle rect = a.getKey("/Rect").getArrayAsRectangle();
            EXPECT_GT(rect.urx, rect.llx);
            EXPECT_GT(rect.lly, 700) << "near the top of the page (PDF space: y up)";
            if (act.getKey("/S").getName() == "/URI") {
                actions.push_back("URI " + act.getKey("/URI").getStringValue());
            } else {
                actions.push_back(act.getKey("/S").getName() + " " + act.getKey("/F").getUTF8Value() + " " +
                                  std::to_string(act.getKey("/D").getArrayItem(0).getIntValue()));
            }
        }
    }
    EXPECT_EQ(actions, (std::vector<std::string>{"/GoToR lecture.pdf 1", "/GoToR lecture.pdf 2",
                                                 "URI https://example.org/x"}))
            << "the .xopp's PDF at its PDF page; the PDF at its page; not the .md";

    // They are ours: not reported as changed, and written again (not twice) on the next save
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_TRUE(opened.changed.empty());
    ASSERT_TRUE(HybridPdf::write(*opened.document, out).ok);
    QPDF again;
    again.processFile(out.string().c_str());
    int count = 0;
    for (auto& p: QPDFPageDocumentHelper(again).getAllPages()) {
        QPDFObjectHandle annots = p.getObjectHandle().getKey("/Annots");
        for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
            count += annots.getArrayItem(i).getKey("/Subtype").getName() == "/Link";
        }
    }
    EXPECT_EQ(count, 3);
}

// --- archive PDF (PDF/A-3b) ----------------------------------------------------------------------------------------

namespace {
class ArchivePdfTest: public HybridPdfTest {
protected:
    /// XQT_ARCHIVE_SAMPLES=<folder>: the archive PDFs that should be PDF/A go there (veraPDF checks them in CI).
    static void keepSample(const fs::path& pdf, const char* name) {
        if (const char* dir = std::getenv("XQT_ARCHIVE_SAMPLES")) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            fs::copy_file(pdf, fs::path(dir) / name, fs::copy_options::overwrite_existing, ec);
        }
    }
};

/// A PDF made by hand with qpdf: one page per (content, resources); `extra` changes it before it is written.
void makeRawPdf(const fs::path& p, const std::vector<std::pair<std::string, std::string>>& pages,
                const std::function<void(QPDF&)>& extra = {}) {
    QPDF q;
    q.emptyPDF();
    for (const auto& [content, resources]: pages) {
        QPDFObjectHandle page = q.makeIndirectObject(
                QPDFObjectHandle::parse("<< /Type /Page /MediaBox [0 0 595 842] /Resources " + resources + " >>"));
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(&q, content));
        QPDFPageDocumentHelper(q).addPage(page, false);
    }
    if (extra) {
        extra(q);
    }
    QPDFWriter w(q, p.string().c_str());
    w.write();
}

std::string streamText(QPDFObjectHandle stream) {
    auto buffer = stream.getStreamData(qpdf_dl_all);
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

/// The colour of a pixel (r, g, b).
std::array<int, 3> pixel(cairo_surface_t* s, int x, int y) {
    const unsigned char* p = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s) + 4 * x;
    return {p[2], p[1], p[0]};
}

int elementsOf(Document& doc, size_t page, size_t layer) {
    const auto& v = doc.getPage(page)->getLayers()[layer]->getElementsView();
    return static_cast<int>(std::distance(v.begin(), v.end()));
}
}  // namespace

TEST_F(ArchivePdfTest, writesAPdfA3bWithTheInkInThePagesAndTheDataAsItsSource) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.archive.pdf");
    const auto r = HybridPdf::writeArchive(*doc, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa);
    EXPECT_TRUE(r.notPdfA.empty()) << r.notPdfA.front();
    EXPECT_EQ(r.pages, 4u);
    EXPECT_EQ(r.flattened, 4u) << "pen, highlighter, text, ruled page";
    EXPECT_EQ(r.annotations, 0u);
    keepSample(out, "archive-lecture.pdf");
    EXPECT_TRUE(HybridPdf::isHybrid(out));
    EXPECT_TRUE(HybridPdf::isArchive(out));
    EXPECT_FALSE(HybridPdf::isArchive(path("lecture.pdf")));

    int code = -1;
    const std::string check = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << check;
    EXPECT_NE(check.find("No syntax or stream encoding errors"), std::string::npos) << check;

    QPDF q;
    q.processFile(out.string().c_str());
    EXPECT_GE(q.getPDFVersion(), std::string("1.7"));
    EXPECT_FALSE(q.isEncrypted());
    EXPECT_TRUE(q.getTrailer().getKey("/ID").isArray());
    QPDFObjectHandle root = q.getRoot();
    // The ink is page content: no annotations, the layers drawn by a marked stream after the page's own
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    ASSERT_EQ(pages.size(), 4u);
    for (size_t i = 0; i < 3; ++i) {  // (page 4 has no notes)
        EXPECT_TRUE(pages[i].getAnnotations().empty());
        QPDFObjectHandle contents = pages[i].getObjectHandle().getKey("/Contents");
        ASSERT_TRUE(contents.isArray());
        ASSERT_GE(contents.getArrayNItems(), 3);
        EXPECT_TRUE(contents.getArrayItem(0).getDict().getKey("/XournalQt").isDictionary());
        QPDFObjectHandle last = contents.getArrayItem(contents.getArrayNItems() - 1);
        ASSERT_TRUE(last.getDict().getKey("/XournalQt").isDictionary());
        EXPECT_NE(streamText(last).find("/XqtInk1 Do"), std::string::npos);
    }
    EXPECT_FALSE(pages[3].getObjectHandle().getKey("/Contents").isArray()) << "a page without notes is as it was";
    // The marker, and the data as the PDF's source (an associated file)
    QPDFObjectHandle marker = root.getKey("/XournalQt");
    ASSERT_TRUE(marker.isDictionary());
    EXPECT_EQ(marker.getKey("/Version").getIntValue(), HybridPdf::ARCHIVE_FORMAT_VERSION);
    EXPECT_TRUE(marker.getKey("/Archive").getBoolValue());
    EXPECT_EQ(marker.getKey("/Flattened").getArrayNItems(), 4);
    QPDFObjectHandle af = root.getKey("/AF");
    ASSERT_TRUE(af.isArray());
    ASSERT_EQ(af.getArrayNItems(), 1);
    QPDFObjectHandle spec = af.getArrayItem(0);
    EXPECT_EQ(spec.getKey("/UF").getUTF8Value(), HybridPdf::DATA_NAME);
    EXPECT_EQ(spec.getKey("/F").getUTF8Value(), HybridPdf::DATA_NAME);
    EXPECT_EQ(spec.getKey("/AFRelationship").getName(), "/Source");
    QPDFObjectHandle ef = spec.getKey("/EF").getKey("/F");
    EXPECT_EQ(ef.getDict().getKey("/Subtype").getName(), "/application/x-xopp");
    EXPECT_TRUE(ef.getDict().getKey("/Params").getKey("/ModDate").isString());
    EXPECT_TRUE(QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(HybridPdf::DATA_NAME));
    // The output intent: sRGB with its profile
    QPDFObjectHandle intents = root.getKey("/OutputIntents");
    ASSERT_TRUE(intents.isArray());
    ASSERT_EQ(intents.getArrayNItems(), 1);
    QPDFObjectHandle intent = intents.getArrayItem(0);
    EXPECT_EQ(intent.getKey("/S").getName(), "/GTS_PDFA1");
    QPDFObjectHandle icc = intent.getKey("/DestOutputProfile");
    EXPECT_EQ(icc.getDict().getKey("/N").getIntValue(), 3);
    EXPECT_EQ(streamText(icc), ArchivePdf::srgbProfile());
    EXPECT_EQ(ArchivePdf::srgbProfile().size(), 3268u);
    EXPECT_EQ(ArchivePdf::srgbProfile().substr(36, 4), "acsp");
    // The metadata: unfiltered XMP that says PDF/A-3b, with the title of the document information
    QPDFObjectHandle meta = root.getKey("/Metadata");
    ASSERT_TRUE(meta.isStream());
    EXPECT_FALSE(meta.getDict().hasKey("/Filter")) << "PDF/A: the metadata stream is not compressed";
    const std::string xmp = streamText(meta);
    EXPECT_NE(xmp.find("<pdfaid:part>3</pdfaid:part>"), std::string::npos) << xmp;
    EXPECT_NE(xmp.find("<pdfaid:conformance>B</pdfaid:conformance>"), std::string::npos);
    QPDFObjectHandle info = q.getTrailer().getKey("/Info");
    EXPECT_EQ(info.getKey("/Title").getUTF8Value(), "lecture");
    EXPECT_NE(xmp.find("<rdf:li xml:lang=\"x-default\">lecture</rdf:li>"), std::string::npos);
    const std::string date = info.getKey("/ModDate").getUTF8Value();  // D:YYYYMMDDHHmmSS+00'00'
    ASSERT_GE(date.size(), 16u);
    EXPECT_NE(xmp.find("<xmp:ModifyDate>" + date.substr(2, 4) + "-" + date.substr(6, 2) + "-" + date.substr(8, 2) +
                       "T" + date.substr(10, 2) + ":" + date.substr(12, 2) + ":" + date.substr(14, 2) + "+00:00"),
              std::string::npos);
    EXPECT_EQ(info.getKey("/Creator").getUTF8Value(), std::string(PROJECT_STRING));
    EXPECT_NE(xmp.find("<xmp:CreatorTool>" + std::string(PROJECT_STRING) + "</xmp:CreatorTool>"), std::string::npos);
    // The text of the PDF is still there
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_FALSE(pdf.getPage(0)->findText("lectureone").empty());
    EXPECT_FALSE(pdf.getPage(2)->findText("lecturetwo").empty());
}

TEST_F(ArchivePdfTest, popplerShowsItLikeOurPdfExport) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path archive = path("lecture.archive.pdf"), exported = path("export.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, archive).ok);
    ExportHelper::exportPdf(doc.get(), exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    for (size_t i = 0; i < 4; ++i) {
        cairo_surface_t* a = render(archive, i);
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

TEST_F(ArchivePdfTest, reopensEditableWithoutTheInkInItsBackground) {
    auto doc = annotated(path("lecture.pdf"));
    const std::string before = describeAsXopp(*doc, path("reference.xopp"));
    const fs::path out = path("lecture.archive.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, out).ok);

    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.hybrid);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(describe(*loaded.document), before) << "the embedded .xopp round-trips";
    // The background: the original pages, pixel for pixel (no trace of the ink)
    const fs::path base = loaded.document->getPdfFilepath();
    for (auto [basePage, original]: {std::pair<size_t, size_t>{0, 0}, {2, 1}, {3, 2}}) {
        cairo_surface_t* a = render(base, basePage);
        cairo_surface_t* b = render(path("lecture.pdf"), original);
        const Diff d = compare(a, b);
        EXPECT_EQ(d.mean, 0) << "page " << basePage + 1;
        cairo_surface_destroy(a);
        cairo_surface_destroy(b);
    }
    {  // the ruled page: its lines, not the blue stroke
        cairo_surface_t* ruled = render(base, 1);
        const auto c = pixel(ruled, 350, 325);
        EXPECT_FALSE(c[2] > 150 && c[0] < 100) << "no stroke at 350,325";
        cairo_surface_destroy(ruled);
    }
    {  // nothing of ours left in the clean copy
        QPDF clean;
        clean.processFile(base.string().c_str());
        EXPECT_FALSE(clean.getRoot().hasKey("/XournalQt"));
        EXPECT_FALSE(clean.getRoot().hasKey("/AF"));
        EXPECT_FALSE(clean.getRoot().hasKey("/OutputIntents"));
        EXPECT_FALSE(clean.getRoot().hasKey("/Metadata")) << "(it would still say PDF/A)";
        for (auto& page: QPDFPageDocumentHelper(clean).getAllPages()) {
            QPDFObjectHandle c = page.getObjectHandle().getKey("/Contents");
            for (int i = 0; c.isArray() && i < c.getArrayNItems(); ++i) {
                EXPECT_FALSE(c.getArrayItem(i).getDict().hasKey("/XournalQt"));
            }
            QPDFObjectHandle x = page.getObjectHandle().getKey("/Resources").getKey("/XObject");
            EXPECT_FALSE(x.isDictionary() && x.hasKey("/XqtInk1"));
        }
    }

    // Erase the pen stroke and save (Ctrl+S): still an archive PDF, without the stroke in its pages and its data
    DocumentSession session(*app, std::move(loaded.document));
    {
        std::unique_lock lock(*session.getDocument());
        Layer* layer = session.getDocument()->getPage(0)->getLayers()[0];
        ASSERT_EQ(layer->getElementsView().size(), 1u);
        layer->removeElement(*layer->getElementsView().begin());
    }
    const auto saved = session.save();
    ASSERT_TRUE(saved.ok) << saved.error;
    EXPECT_TRUE(HybridPdf::isArchive(out));
    cairo_surface_t* page = render(out, 0);
    EXPECT_FALSE(inked(page, 140, 220, 160, 240)) << "the stroke is gone from the page";
    EXPECT_TRUE(inked(page, 60, 842 - 100, 220, 842 - 90) || inked(page, 60, 90, 220, 100)) << "the highlighter stays";
    cairo_surface_destroy(page);
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document) << again.error;
    EXPECT_EQ(elementsOf(*again.document, 0, 0), 0) << "and from the embedded .xopp";
    EXPECT_EQ(elementsOf(*again.document, 0, 1), 1);
    EXPECT_TRUE(again.hybridChanged.empty());
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
}

TEST_F(ArchivePdfTest, contentAnotherAppAddedStaysInTheBackground) {
    auto doc = annotated(path("lecture.pdf"));
    const fs::path out = path("lecture.archive.pdf");
    ASSERT_TRUE(HybridPdf::writeArchive(*doc, out).ok);
    editWithQpdf(out, [](QPDF& q) {  // a black square appended to page 1's content, as another app would
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(0).getObjectHandle();
        QPDFObjectHandle contents = QPDFObjectHandle::newArray();
        QPDFObjectHandle old = page.getKey("/Contents");
        for (int i = 0; i < old.getArrayNItems(); ++i) {
            contents.appendItem(old.getArrayItem(i));
        }
        contents.appendItem(QPDFObjectHandle::newStream(&q, "q 0 0 0 rg 500 700 40 40 re f Q\n"));
        page.replaceKey("/Contents", contents);
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.hybridChanged.empty());
    cairo_surface_t* bg = render(loaded.document->getPdfFilepath(), 0);
    EXPECT_TRUE(inked(bg, 510, 842 - 730, 530, 842 - 710)) << "the other app's square stays";
    EXPECT_FALSE(inked(bg, 140, 220, 160, 240)) << "our stroke does not";
    cairo_surface_destroy(bg);

    // A page whose content another app rewrote as one stream: our marked streams are gone, that is reported
    editWithQpdf(out, [](QPDF& q) {
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(1).getObjectHandle();
        page.replaceKey("/Contents", QPDFObjectHandle::newStream(&q, "0 0 1 RG 300 542 m 400 492 l S\n"));
    });
    auto changed = DocumentSession::loadFile(out);
    ASSERT_TRUE(changed.document);
    EXPECT_EQ(changed.hybridChanged, (std::vector<std::string>{HybridPdf::nameOf(1, 0)}));
}

TEST_F(ArchivePdfTest, notesWithoutAPdfTextAndLinksArePdfA) {
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
    Document doc(nullptr);
    auto page = std::make_shared<XojPage>(595, 842);
    page->setBackgroundType(PageType(PageTypeFormat::Graph));
    addStroke(page->getSelectedLayer(), StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 12, {Point(60, 300), Point(220, 300)});
    addText(page->getSelectedLayer(), "Grüße, «archive»", 80, 400);
    auto* markdown = new Layer();
    markdown->setName(std::string(xoj::markdown::LAYER_NAME));
    page->getLayers().insert(page->getLayers().begin(), markdown);
    auto box = std::make_unique<Text>();
    box->setText("See [the PDF](lecture.pdf#page=3) and [the web](https://example.org/x).");
    box->setFont(XojFont("Sans", 10));
    box->setWrap(400);
    box->setTransformation(xoj::util::Matrix::TRANSLATION(56, 56));
    markdown->addElement(std::move(box));
    doc.addPage(page);
    for (const Layer* l: page->getLayers()) {
        for (const auto& e: l->getElementsView()) {
            e->getBoundingBox();
        }
    }
    fs::create_directories(path("Archive"));
    const fs::path out = path("Archive/notes.pdf");
    // Linked from its own folder, into the archive: the PDF is archived as "Archive/lecture.pdf"
    HybridPdf::LinkMap links;
    links.from = path("");
    links.archived = [&](const fs::path& f) {
        return f.filename() == "lecture.pdf" ? path("Archive/lecture.pdf") : fs::path();
    };
    const auto r = HybridPdf::writeArchive(doc, out, {}, npos, links);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa) << (r.notPdfA.empty() ? "" : r.notPdfA.front());
    keepSample(out, "archive-notes.pdf");
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);

    QPDF q;
    q.processFile(out.string().c_str());
    std::vector<std::string> actions;
    for (auto& a: QPDFPageDocumentHelper(q).getAllPages().at(0).getAnnotations()) {
        QPDFObjectHandle o = a.getObjectHandle();
        ASSERT_EQ(o.getKey("/Subtype").getName(), "/Link") << "the only annotations are links";
        EXPECT_EQ(o.getKey("/F").getIntValue() & 4, 4) << "printed (PDF/A)";
        QPDFObjectHandle act = o.getKey("/A");
        actions.push_back(act.getKey("/S").getName() == "/URI"
                                  ? "URI " + act.getKey("/URI").getStringValue()
                                  : "GoToR " + act.getKey("/F").getUTF8Value() + " " +
                                            std::to_string(act.getKey("/D").getArrayItem(0).getIntValue()));
    }
    EXPECT_EQ(actions, (std::vector<std::string>{"GoToR lecture.pdf 2", "URI https://example.org/x"}));
    auto opened = DocumentSession::loadFile(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(elementsOf(*opened.document, 0, 1), 2);
}

TEST_F(ArchivePdfTest, aSourcePdfThatCannotConformIsWrittenButNotCalledPdfA) {
    // Page 1: Helvetica, not embedded; page 2: CMYK colours
    makeRawPdf(path("old.pdf"),
               {{"BT /F1 24 Tf 72 700 Td (Hello) Tj ET\n",
                 "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"},
                {"0 1 1 0 k 100 100 200 200 re f\n", "<< >>"}});
    auto loaded = DocumentSession::loadFile(path("old.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    addStroke(loaded.document->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2,
              {Point(100, 200), Point(200, 250)});
    loaded.document->getPage(0)->getSelectedLayer()->getElementsView().front()->getBoundingBox();
    const fs::path out = path("old.archive.pdf");
    const auto r = HybridPdf::writeArchive(*loaded.document, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.pdfa);
    ASSERT_EQ(r.notPdfA.size(), 2u);
    EXPECT_NE(r.notPdfA[0].find("not embedded: Helvetica"), std::string::npos) << r.notPdfA[0];
    EXPECT_NE(r.notPdfA[1].find("CMYK"), std::string::npos) << r.notPdfA[1];
    EXPECT_NE(r.notPdfA[1].find("(page 2)"), std::string::npos) << r.notPdfA[1];
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    QPDF q;
    q.processFile(out.string().c_str());
    const std::string xmp = streamText(q.getRoot().getKey("/Metadata"));
    EXPECT_EQ(xmp.find("pdfaid:part"), std::string::npos) << "no claim of PDF/A";
    EXPECT_TRUE(q.getRoot().getKey("/OutputIntents").isArray());
    EXPECT_TRUE(HybridPdf::isArchive(out)) << "still an archive PDF the app opens for editing";
    auto opened = DocumentSession::loadFile(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(elementsOf(*opened.document, 0, 0), 1);
}

TEST_F(ArchivePdfTest, whatCanBeRepairedIsRepaired) {
    makeRawPdf(path("scripted.pdf"),
               {{"q 0 0 1 rg 100 100 50 50 re f Q q 20 0 0 20 300 300 cm /Im1 Do Q\n",
                 "<< /XObject << /Im1 << /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB "
                 "/BitsPerComponent 8 /Interpolate true /Length 3 >> >> >>"}},
               [](QPDF& q) {
                   QPDFObjectHandle root = q.getRoot();
                   root.replaceKey("/OpenAction", QPDFObjectHandle::parse("<< /S /JavaScript /JS (app.alert(1)) >>"));
                   QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(0).getObjectHandle();
                   page.replaceKey("/Annots",
                                   QPDFObjectHandle::parse("[ << /Type /Annot /Subtype /Link /Rect [100 100 150 150] "
                                                           "/A << /S /URI /URI (https://example.org) >> >> ]"));
                   // the image's pixel
                   QPDFObjectHandle im = page.getKey("/Resources").getKey("/XObject").getKey("/Im1");
                   QPDFObjectHandle image = QPDFObjectHandle::newStream(&q, std::string("\xff\x00\x00", 3));
                   image.replaceDict(im);
                   image.getDict().removeKey("/Length");
                   page.getKey("/Resources").getKey("/XObject").replaceKey("/Im1", image);
               });
    auto loaded = DocumentSession::loadFile(path("scripted.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    const fs::path out = path("scripted.archive.pdf");
    const auto r = HybridPdf::writeArchive(*loaded.document, out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa) << (r.notPdfA.empty() ? "" : r.notPdfA.front());
    ASSERT_EQ(r.adjusted.size(), 1u);
    EXPECT_NE(r.adjusted[0].find("JavaScript"), std::string::npos);
    keepSample(out, "archive-repaired.pdf");
    QPDF q;
    q.processFile(out.string().c_str());
    EXPECT_FALSE(q.getRoot().hasKey("/OpenAction"));
    QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(0).getObjectHandle();
    EXPECT_FALSE(page.getKey("/Resources").getKey("/XObject").getKey("/Im1").getDict().getKey("/Interpolate").getBoolValue());
    QPDFObjectHandle link = page.getKey("/Annots").getArrayItem(0);
    EXPECT_EQ(link.getKey("/F").getIntValue(), 4) << "printed";
    EXPECT_EQ(link.getKey("/A").getKey("/URI").getStringValue(), "https://example.org") << "a web link stays";
}

/// XQT_ARCHIVE_SOURCE=<pdf> [XQT_ARCHIVE_SAMPLES=<folder>]: that PDF with a stroke on its first page as an archive PDF,
/// with the report on stdout (to check the checks against veraPDF on real PDFs).
TEST_F(ArchivePdfTest, archiveOfAGivenPdf) {
    const char* source = std::getenv("XQT_ARCHIVE_SOURCE");
    if (!source) {
        GTEST_SKIP() << "XQT_ARCHIVE_SOURCE not set";
    }
    auto loaded = DocumentSession::loadFile(source);
    ASSERT_TRUE(loaded.document) << loaded.error;
    addStroke(loaded.document->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2,
              {Point(100, 200), Point(200, 250)});
    loaded.document->getPage(0)->getSelectedLayer()->getElementsView().front()->getBoundingBox();
    const fs::path out = path("given.archive.pdf");
    const auto start = std::chrono::steady_clock::now();
    const auto r = HybridPdf::writeArchive(*loaded.document, out);
    ASSERT_TRUE(r.ok) << r.error;
    std::cout << "archive: " << (r.pdfa ? "PDF/A-3b" : "not PDF/A") << ", "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    for (const auto& p: r.notPdfA) {
        std::cout << "  not PDF/A: " << p << "\n";
    }
    for (const auto& a: r.adjusted) {
        std::cout << "  adjusted: " << a << "\n";
    }
    keepSample(out, (fs::path(source).stem().string() + ".archive.pdf").c_str());
}

// A hybrid PDF is never PDF/A (its annotations and attachment break it): a PDF/A source's identification is dropped
// from the metadata it keeps; an archive of the same source is PDF/A again.
TEST_F(ArchivePdfTest, aHybridPdfOfAPdfASourceDoesNotClaimPdfA) {
    makeTextPdf(path("source.pdf"), {"lectureone"});
    {  // the source says it is PDF/A-2b (in both forms XMP allows), with an output intent
        auto loaded = DocumentSession::loadFile(path("source.pdf"));
        ASSERT_TRUE(loaded.document);
        ASSERT_TRUE(HybridPdf::writeArchive(*loaded.document, path("pdfa.pdf")).ok);
        editWithQpdf(path("pdfa.pdf"), [](QPDF& q) {
            QPDFObjectHandle root = q.getRoot();
            for (const char* key: {"/XournalQt", "/AF", "/Names"}) {
                root.removeKey(key);  // (a plain PDF/A, not ours)
            }
            QPDFObjectHandle meta = root.getKey("/Metadata");
            std::string xmp = streamText(meta);
            const auto at = xmp.find("<pdfaid:part>3</pdfaid:part>");
            ASSERT_NE(at, std::string::npos);
            xmp.replace(at, std::string("<pdfaid:part>3</pdfaid:part>").size(),
                        "<pdfaid:part>2</pdfaid:part><dc:rights>kept</dc:rights>");
            const auto d = xmp.find("<rdf:Description rdf:about=\"\"");
            xmp.insert(d + std::string("<rdf:Description").size(), " pdfaid:amd=\"2005\"");
            meta.replaceStreamData(xmp, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
        });
    }
    auto loaded = DocumentSession::loadFile(path("pdfa.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_FALSE(loaded.hybrid);
    addStroke(loaded.document->getPage(0)->getSelectedLayer(), StrokeTool::PEN, Color(0xffcc0000U), 2,
              {Point(100, 200), Point(200, 250)});
    loaded.document->getPage(0)->getSelectedLayer()->getElementsView().front()->getBoundingBox();

    ASSERT_TRUE(HybridPdf::write(*loaded.document, path("hybrid.pdf")).ok);
    QPDF hybrid;
    hybrid.processFile(path("hybrid.pdf").string().c_str());
    const std::string xmp = streamText(hybrid.getRoot().getKey("/Metadata"));
    EXPECT_EQ(xmp.find("pdfaid:part"), std::string::npos) << xmp;
    EXPECT_EQ(xmp.find("pdfaid:conformance"), std::string::npos);
    EXPECT_EQ(xmp.find("pdfaid:amd"), std::string::npos);
    EXPECT_NE(xmp.find("<dc:rights>kept</dc:rights>"), std::string::npos) << "the rest of the metadata stays";

    const auto archive = HybridPdf::writeArchive(*loaded.document, path("again.archive.pdf"));
    ASSERT_TRUE(archive.ok) << archive.error;
    EXPECT_TRUE(archive.pdfa);
    QPDF a;
    a.processFile(path("again.archive.pdf").string().c_str());
    EXPECT_NE(streamText(a.getRoot().getKey("/Metadata")).find("<pdfaid:part>3</pdfaid:part>"), std::string::npos);
}

// --- saving again: incremental updates (qt/docs/hybrid-pdf.md, "Saving: incremental updates") ------------------------

namespace {
std::string fileBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

size_t countOf(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

/// A stroke on a page (as the stroke tool adds it, through the undo machinery).
void drawOn(DocumentSession& s, size_t pageNo, double y, size_t layer = 0) {
    PageRef page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->setColor(Color(0xff008000U));
    for (int j = 0; j < 12; ++j) {
        stroke->addPoint(Point(80 + j * 20, y + 6 * std::sin(j / 2.0), 1 + (j % 4) / 4.0));
    }
    stroke->getBoundingBox();
    s.getDocument()->lock();
    (layer == 0 ? page->getSelectedLayer() : page->getLayers().at(layer))->addElement(std::move(stroke));
    s.getDocument()->unlock();
}

/// Each page of `a` looks like the same page of `b` (poppler, with annotations).
void expectSamePages(const fs::path& a, const fs::path& b, size_t pages, const std::string& when) {
    for (size_t i = 0; i < pages; ++i) {
        cairo_surface_t* x = render(a, i);
        cairo_surface_t* y = render(b, i);
        const Diff d = compare(x, y);
        EXPECT_LT(d.mean, 0.5) << when << ", page " << i + 1;
        EXPECT_LT(d.differ, 0.002) << when << ", page " << i + 1;
        cairo_surface_destroy(x);
        cairo_surface_destroy(y);
    }
}

std::vector<QPDFObjGen> annotIds(const fs::path& pdf, size_t page) {
    QPDF q;
    q.processFile(pdf.string().c_str());
    std::vector<QPDFObjGen> ids;
    for (auto& a: QPDFPageDocumentHelper(q).getAllPages().at(page).getAnnotations()) {
        ids.push_back(a.getObjectHandle().getObjGen());
    }
    return ids;
}

class IncrementalSaveTest: public HybridPdfTest {
protected:
    void SetUp() override {
        HybridPdfTest::SetUp();
        HybridPdf::compactAbove = 1000;  // (the small test files: never compacted, unless a test says so)
    }
    void TearDown() override {
        HybridPdf::compactAbove = 0.25;
        IncrementalPdf::failWriteAt = nullptr;
    }
    /// A session of the annotated lecture saved as a PDF with notes (in full).
    std::unique_ptr<DocumentSession> savedLecture(const fs::path& out) {
        auto s = std::make_unique<DocumentSession>(*app, annotated(path("lecture.pdf")));
        const auto r = s->saveAsHybrid(out);
        EXPECT_TRUE(r.ok) << r.error;
        EXPECT_FALSE(r.incremental);
        return s;
    }
    /// The document's state as a full write gives it, to compare with.
    fs::path writtenInFull(DocumentSession& s, const char* name) {
        const fs::path full = path(name);
        EXPECT_TRUE(HybridPdf::write(*s.getDocument(), full).ok);
        return full;
    }
};
}  // namespace

// A crash or a full disk while the update is written leaves the file as it was, byte for byte
TEST_F(IncrementalSaveTest, aFailedAppendLeavesTheFileAsItWas) {
    auto s = savedLecture(path("notes.pdf"));
    const std::string before = fileBytes(path("notes.pdf"));
    drawOn(*s, 0, 500);
    // Before anything of the update is written, and when all of it is written but not yet in place
    for (const uint64_t at: {uint64_t(0), uint64_t(1)}) {
        IncrementalPdf::failWriteAt = [at](uint64_t written) { return written >= at; };
        const auto r = s->save();
        EXPECT_FALSE(r.ok) << "fails at " << at;
        EXPECT_EQ(fileBytes(path("notes.pdf")), before);
        for (auto& e: fs::directory_iterator(tmp.path().toStdString())) {
            EXPECT_NE(e.path().extension(), ".part") << "no temporary file left: " << e.path();
        }
    }
    IncrementalPdf::failWriteAt = nullptr;
    // A temporary file a crash left behind (the process died while it wrote it) goes with the next save
    const fs::path stale = path(".notes.pdf.12345-1.part");
    std::ofstream(stale) << before.substr(0, 1000);
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(1));
    const auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(fileBytes(path("notes.pdf")).substr(0, before.size()), before);
    EXPECT_FALSE(fs::exists(stale));
}

// Ctrl+S appends the changed layer (its annotation and drawing), the embedded document, the marker: nothing else
TEST_F(IncrementalSaveTest, ctrlSAppendsOnlyWhatChanged) {
    const fs::path out = path("lecture.notes.pdf");
    auto s = savedLecture(out);
    const std::string before = fileBytes(out);
    const auto page1 = annotIds(out, 0);
    const auto page3 = annotIds(out, 2);
    drawOn(*s, 2, 600);  // (page 3, the layer of its text: the ruled page is page 2)
    const auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    const std::string after = fileBytes(out);
    EXPECT_EQ(after.substr(0, before.size()), before);
    EXPECT_EQ(r.appended, after.size() - before.size());
    EXPECT_LT(r.appended, 12000u) << "one layer, the .xopp, the marker";
    EXPECT_EQ(annotIds(out, 0), page1) << "the annotations of an unchanged page are the same objects";
    ASSERT_EQ(annotIds(out, 2).size(), 1u) << "the layer of the text and the new stroke";
    EXPECT_NE(annotIds(out, 2), page3) << "written again";
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    EXPECT_TRUE(HybridPdf::hasEarlierRevisions(out));
    EXPECT_TRUE(s->hasEarlierRevisions());
    expectSamePages(out, writtenInFull(*s, "full.pdf"), 4, "after one incremental save");
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(describe(*loaded.document), describeAsXopp(*s->getDocument(), path("same.xopp")));
    // Saved again without a change: little more than the .xopp and the marker
    const auto again = s->save();
    ASSERT_TRUE(again.ok);
    EXPECT_TRUE(again.incremental);
    EXPECT_LT(again.appended, 8000u);
}

// Many saves in a row, with every kind of change: each file is valid, looks like a full write, and opens as the
// document; the earlier revisions stay in it
TEST_F(IncrementalSaveTest, manySavesLookAndOpenLikeFullWrites) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    Document& doc = *s->getDocument();
    const std::vector<std::pair<std::string, std::function<void()>>> edits = {
            {"a stroke", [&] { drawOn(*s, 0, 700); }},
            {"a stroke on the highlighter layer, erased",
             [&] {
                 std::unique_lock lock(doc);
                 Layer* l = doc.getPage(0)->getLayers().at(1);
                 l->removeElement(l->getElementsView().front());
             }},
            {"pages moved",
             [&] {
                 std::unique_lock lock(doc);
                 PageRef p = doc.getPage(3);
                 doc.deletePage(3);
                 doc.insertPage(p, 0);
             }},
            {"a ruled page added",
             [&] {
                 auto ruled = std::make_shared<XojPage>(595, 842);
                 ruled->setBackgroundType(PageType(PageTypeFormat::Ruled));
                 std::unique_lock lock(doc);
                 doc.insertPage(ruled, 2);
                 lock.unlock();
                 drawOn(*s, 2, 300);
             }},
            {"a background changed",
             [&] {
                 std::unique_lock lock(doc);
                 doc.getPage(2)->setBackgroundType(PageType(PageTypeFormat::Graph));
             }},
            {"a page deleted",
             [&] {
                 std::unique_lock lock(doc);
                 doc.deletePage(4);
             }},
            {"a text", [&] {
                 std::unique_lock lock(doc);
                 addText(doc.getPage(1)->getSelectedLayer(), "added later", 100, 300);
             }},
            {"the PDF page shown twice", [&] {
                 std::unique_lock lock(doc);
                 PageRef first = doc.getPage(1);  // (a PDF page)
                 auto twice = std::make_shared<XojPage>(first->getWidth(), first->getHeight());
                 twice->setBackgroundType(first->getBackgroundType());
                 twice->setBackgroundPdfPageNr(first->getPdfPageNr());
                 doc.insertPage(twice, 4);
             }},
    };
    size_t revisions = 1;
    for (const auto& [what, edit]: edits) {
        edit();
        const auto r = s->save();
        ASSERT_TRUE(r.ok) << what << ": " << r.error;
        EXPECT_TRUE(r.incremental) << what;
        revisions += r.incremental ? 1 : 0;
        int code = -1;
        const std::string check = qpdfCheck(out, code);
        EXPECT_EQ(code, 0) << what << "\n" << check;
        const fs::path full = writtenInFull(*s, "full.pdf");
        expectSamePages(out, full, doc.getPageCount(), what);
        auto loaded = DocumentSession::loadFile(out);
        ASSERT_TRUE(loaded.document) << what;
        EXPECT_TRUE(loaded.hybridChanged.empty()) << what;
        EXPECT_EQ(describe(*loaded.document), describeAsXopp(doc, path("same.xopp"))) << what;
    }
    EXPECT_EQ(countOf(fileBytes(out), "startxref"), revisions);
}

// The file is written anew in full when it grew too much, and when asked to (Save as, Share)
TEST_F(IncrementalSaveTest, compactsWhenItGrewTooMuchOrWhenAsked) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    drawOn(*s, 0, 500);
    HybridPdf::compactAbove = 0.0001;
    auto r = s->save();
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental) << "grew by more than the limit: written in full";
    EXPECT_FALSE(HybridPdf::hasEarlierRevisions(out));
    HybridPdf::compactAbove = 1000;
    drawOn(*s, 0, 550);
    r = s->save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "after a full write, appended again";
    EXPECT_TRUE(s->hasEarlierRevisions());
    DocumentSession::SaveRequest share;
    share.compact = true;
    r = s->saveNow(share);
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental);
    EXPECT_FALSE(s->hasEarlierRevisions()) << "shared: no earlier revision goes along";
    r = s->saveAsHybrid(path("other.pdf"));
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental) << "Save as";
    drawOn(*s, 1, 400);
    r = s->save();
    EXPECT_TRUE(r.incremental) << "then Ctrl+S appends to the new file";
}

// Share, Export and the archive export write fresh files: nothing of the earlier revisions (deleted ink) goes along
TEST_F(IncrementalSaveTest, whatIsSharedOrExportedHasNoEarlierRevisions) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    {  // the highlighter goes: its annotation stays in the earlier revision
        std::unique_lock lock(*s->getDocument());
        s->getDocument()->getPage(0)->getLayers().at(1)->clearNoFree();
    }
    ASSERT_TRUE(s->save().incremental);
    auto hasHighlighter = [](const fs::path& pdf) {
        QPDF q;
        q.processFile(pdf.string().c_str());
        for (QPDFObjectHandle o: q.getAllObjects()) {
            if (o.isDictionary() && o.getKey("/NM").isString() && o.getKey("/NM").getUTF8Value() == "xopp:p1-l2") {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(hasHighlighter(out)) << "the earlier revision holds it";
    ASSERT_TRUE(s->saveNow({DocumentSession::SaveKind::ExportHybrid, path("copy.pdf"), {}, {}}).ok);
    ASSERT_TRUE(s->exportArchive(path("archive.pdf")).ok);
    ASSERT_TRUE(s->exportXopp(path("export.xopp")).ok);
    for (const char* f: {"copy.pdf", "archive.pdf", "export.pdf"}) {
        EXPECT_FALSE(HybridPdf::hasEarlierRevisions(path(f))) << f;
        EXPECT_FALSE(hasHighlighter(path(f))) << f;
    }
    // A PDF of the library shared as it is: written anew in one piece first
    fs::copy_file(out, path("card.pdf"));
    std::string error;
    ASSERT_TRUE(HybridPdf::compact(path("card.pdf"), error)) << error;
    EXPECT_FALSE(HybridPdf::hasEarlierRevisions(path("card.pdf")));
    EXPECT_FALSE(hasHighlighter(path("card.pdf")));
    int code = -1;
    EXPECT_EQ((qpdfCheck(path("card.pdf"), code), code), 0);
    auto loaded = DocumentSession::loadFile(path("card.pdf"));
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty());
    EXPECT_EQ(describe(*loaded.document), describeAsXopp(*s->getDocument(), path("same.xopp")));
}

// Opened again, the first Ctrl+S appends too; the clean copy is kept across incremental saves (not made again)
TEST_F(IncrementalSaveTest, theCleanCopyIsKeptAcrossIncrementalSaves) {
    const fs::path out = path("notes.pdf");
    savedLecture(out).reset();
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    const fs::path base = loaded.document->getPdfFilepath();
    ASSERT_TRUE(HybridPdf::inCache(base));
    DocumentSession s(*app, std::move(loaded.document));
    drawOn(s, 0, 650);
    const auto r = s.save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "the first save after opening the file";
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    const fs::path next = again.document->getPdfFilepath();
    EXPECT_NE(next.parent_path(), base.parent_path()) << "the entry of the new version";
    EXPECT_TRUE(fs::equivalent(next, base)) << "the same clean copy (a hard link), not made again";
    EXPECT_EQ(describe(*again.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    // Pages moved: the next open makes a clean copy of that version
    {
        std::unique_lock lock(*s.getDocument());
        PageRef p = s.getDocument()->getPage(3);
        s.getDocument()->deletePage(3);
        s.getDocument()->insertPage(p, 0);
    }
    ASSERT_TRUE(s.save().incremental);
    auto moved = DocumentSession::loadFile(out);
    ASSERT_TRUE(moved.document);
    EXPECT_FALSE(fs::equivalent(moved.document->getPdfFilepath(), base));
    EXPECT_EQ(describe(*moved.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    // Opened from that version, the next save appends again
    DocumentSession reopened(*app, std::move(moved.document));
    drawOn(reopened, 1, 200);
    EXPECT_TRUE(reopened.save().incremental);
}

// Another app appends its own revision: a moved annotation of ours is noticed, and the next save writes the file anew;
// a comment it added is kept, and the next save appends again
TEST_F(IncrementalSaveTest, anotherAppsRevisionIsNoticedAndKept) {
    const fs::path out = path("notes.pdf");
    auto s = savedLecture(out);
    drawOn(*s, 0, 450);
    ASSERT_TRUE(s->save().incremental);
    s.reset();
    auto appendAsAnotherApp = [&](const std::function<void(QPDF&, IncrementalPdf::Update&)>& change) {
        IncrementalPdf::Tail tail;
        std::string error;
        ASSERT_TRUE(IncrementalPdf::readTail(out, tail, error));
        QPDF q;
        q.processFile(out.string().c_str());
        IncrementalPdf::Update u(q);
        change(q, u);
        ASSERT_TRUE(IncrementalPdf::append(out, tail, u.serialize(tail)).ok);
        fs::last_write_time(out, fs::last_write_time(out) + std::chrono::seconds(5));
    };
    appendAsAnotherApp([](QPDF& q, IncrementalPdf::Update& u) {  // a comment on page 3
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages().at(2).getObjectHandle();
        u.touch(page);
        QPDFObjectHandle note = q.makeIndirectObject(QPDFObjectHandle::parse(
                "<< /Type /Annot /Subtype /Text /Rect [300 300 320 320] /Contents (other app) /NM (other) >>"));
        std::vector<QPDFObjectHandle> annots = page.getKey("/Annots").getArrayAsVector();
        annots.push_back(note);
        page.replaceKey("/Annots", QPDFObjectHandle::newArray(annots));
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty()) << "ours are as they were";
    DocumentSession kept(*app, std::move(loaded.document));
    drawOn(kept, 0, 380);
    auto r = kept.save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "appended on top of the other app's revision";
    std::vector<std::string> names;
    {
        QPDF q;
        q.processFile(out.string().c_str());
        for (auto& a: QPDFPageDocumentHelper(q).getAllPages().at(2).getAnnotations()) {
            names.push_back(a.getObjectHandle().getKey("/NM").getUTF8Value());
        }
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "other"), names.end()) << "its comment stays";
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    // Now it moves our pen stroke
    appendAsAnotherApp([](QPDF& q, IncrementalPdf::Update& u) {
        QPDFObjectHandle a = ourAnnot(q, 0, HybridPdf::nameOf(0, 0));
        u.touch(a);
        a.replaceKey("/Rect", QPDFObjectHandle::parse("[10 10 50 50]"));
    });
    auto changed = DocumentSession::loadFile(out);
    ASSERT_TRUE(changed.document);
    EXPECT_EQ(changed.hybridChanged, std::vector<std::string>{HybridPdf::nameOf(0, 0)}) << "edited in another app";
    DocumentSession after(*app, std::move(changed.document));
    after.setHybridChanges({});  // "Keep the Xournal data"
    r = after.save();
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(r.incremental) << "written anew from the Xournal data";
    auto clean = DocumentSession::loadFile(out);
    ASSERT_TRUE(clean.document);
    EXPECT_TRUE(clean.hybridChanged.empty());
}

// Pages pasted from another PDF are appended to the file (the pages it has stay as they are)
TEST_F(IncrementalSaveTest, pastedPdfPagesAreAppended) {
    makeTextPdf(path("other.pdf"), {"pastedone", "pastedtwo"});
    const std::string other = fileBytes(path("other.pdf"));
    const fs::path out = path("notes.pdf");
    auto paste = [&](DocumentSession& s, size_t at) {
        std::string error;
        const size_t first = s.addPdfPages(other, error);
        ASSERT_NE(first, npos) << error;
        s.waitForMerges();
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Pdf));
        page->setBackgroundPdfPageNr(first + 1);
        std::unique_lock lock(*s.getDocument());
        s.getDocument()->insertPage(page, at);
    };
    auto s = savedLecture(out);  // (its background: the user's PDF)
    paste(*s, 1);
    auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    expectSamePages(out, writtenInFull(*s, "full.pdf"), 5, "a page pasted");
    s.reset();
    auto loaded = DocumentSession::loadFile(out);  // (its background: the clean copy)
    ASSERT_TRUE(loaded.document);
    DocumentSession reopened(*app, std::move(loaded.document));
    paste(reopened, 0);
    drawOn(reopened, 0, 300);
    r = reopened.save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    expectSamePages(out, writtenInFull(reopened, "full.pdf"), 6, "pasted into the reopened file");
    auto again = DocumentSession::loadFile(out);
    ASSERT_TRUE(again.document);
    EXPECT_EQ(describe(*again.document), describeAsXopp(*reopened.getDocument(), path("same.xopp")));
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(out, "", nullptr));
    EXPECT_FALSE(pdf.getPage(0)->findText("pastedtwo").empty());
}

// A file written by an earlier version (no record of our layers in its marker, no drawing keys): the first Ctrl+S
// reads the pages with our notes and appends; the next ones use the record it wrote
TEST_F(IncrementalSaveTest, aFileOfAnEarlierVersionIsAppendedTo) {
    const fs::path out = path("notes.pdf");
    savedLecture(out).reset();
    editWithQpdf(out, [](QPDF& q) {
        QPDFObjectHandle marker = q.getRoot().getKey("/XournalQt");
        marker.removeKey("/Layers");
        marker.removeKey("/Drawn");
        for (auto& page: QPDFPageDocumentHelper(q).getAllPages()) {
            if (page.getObjectHandle().hasKey("/XournalQt")) {
                page.getObjectHandle().removeKey("/XournalQt");
            }
            for (auto& a: page.getAnnotations()) {
                QPDFObjectHandle mark = a.getObjectHandle().getKey("/XournalQt");
                if (mark.isDictionary() && mark.hasKey("/Sig")) {
                    mark.removeKey("/Sig");
                }
            }
        }
    });
    auto loaded = DocumentSession::loadFile(out);
    ASSERT_TRUE(loaded.document);
    DocumentSession s(*app, std::move(loaded.document));
    for (int n = 0; n < 2; ++n) {
        drawOn(s, 2, 500 + 40 * n);
        const auto r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_TRUE(r.incremental) << "save " << n + 1;
        int code = -1;
        EXPECT_EQ((qpdfCheck(out, code), code), 0);
        expectSamePages(out, writtenInFull(s, "full.pdf"), 4, "a file of an earlier version, save " + std::to_string(n + 1));
        auto again = DocumentSession::loadFile(out);
        ASSERT_TRUE(again.document);
        EXPECT_TRUE(again.hybridChanged.empty());
        EXPECT_EQ(describe(*again.document), describeAsXopp(*s.getDocument(), path("same.xopp")));
    }
}
