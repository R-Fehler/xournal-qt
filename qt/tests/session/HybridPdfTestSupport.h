/*
 * xournal-qt: what the tests of the PDF with notes share (HybridPdfTest.cpp, ArchivePdfTest.cpp, IncrementalSaveTest.cpp,
 * HybridPdfBench.cpp): an annotated lecture, qpdf --check, poppler's picture of a page and comparing pictures, a
 * document described as text, and the fixtures.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

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

#include <QSizeF>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/DLL.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "control/ExportHelper.h"
#include "control/xojfile/SaveHandler.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "model/BackgroundImage.h"
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
#include "session/PageBookmarks.h"
#include "session/PdfBookmarks.h"
#include "undo/UndoRedoHandler.h"

#include "config.h"
#include "support/TestSupport.h"

namespace xqt::test::hybrid {

inline Stroke* addStroke(Layer* layer, StrokeTool tool, Color color, double width, std::vector<Point> points) {
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

inline Text* addText(Layer* layer, const std::string& text, double x, double y) {
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
inline std::unique_ptr<Document> annotated(const fs::path& pdf) {
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
inline std::string qpdfCheck(const fs::path& pdf, int& code) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    code = job.getExitCode();
    return out.str() + err.str();
}

/// A page as poppler draws it (with annotations), white behind, 1 px per point.
inline cairo_surface_t* render(const fs::path& pdf, size_t page) {
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
inline Diff compare(cairo_surface_t* a, cairo_surface_t* b) {
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
inline bool inked(cairo_surface_t* s, int x0, int y0, int x1, int y1) {
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
inline std::string describe(Document& doc) {
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
inline std::string describeAsXopp(Document& doc, const fs::path& xopp) {
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
    const std::string wFile = tmp.string();  // (QPDFWriter keeps the pointer)
    QPDFWriter w(q, wFile.c_str());
    w.write();
    fs::rename(tmp, file);
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
}

inline QPDFObjectHandle ourAnnot(QPDF& q, size_t page, const std::string& name) {
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

/// The archive PDF tests (and the incremental saves of an archive PDF).
class ArchivePdfTest: public HybridPdfTest {
public:
    /// XQT_ARCHIVE_SAMPLES=<folder>: the archive PDFs that should be PDF/A go there (veraPDF checks them in CI).
    static void keepSample(const fs::path& pdf, const char* name) {
        if (const char* dir = std::getenv("XQT_ARCHIVE_SAMPLES")) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            fs::copy_file(pdf, fs::path(dir) / name, fs::copy_options::overwrite_existing, ec);
        }
    }
};

/// A stream's data, decoded.
inline std::string streamText(QPDFObjectHandle stream) {
    auto buffer = stream.getStreamData(qpdf_dl_all);
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

// --- saving again: incremental updates (qt/docs/features/hybrid-pdf.md, "Saving: incremental updates") ---------------

inline size_t countOf(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

/// A stroke on a page (as the stroke tool adds it, through the undo machinery).
inline void drawOn(DocumentSession& s, size_t pageNo, double y, size_t layer = 0) {
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
inline void expectSamePages(const fs::path& a, const fs::path& b, size_t pages, const std::string& when) {
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

inline std::vector<QPDFObjGen> annotIds(const fs::path& pdf, size_t page) {
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

}  // namespace xqt::test::hybrid
