/*
 * xournal-qt: annotations of other apps made editable (qt/docs/features/adopt-annotations.md).
 *
 * No real exports of GoodNotes, Drawboard or Preview were at hand: the fixtures are PDFs made here (cairo) with
 * annotations added by qpdf the way the PDF standard defines them, plus the keys the apps are known or assumed to
 * write (the doc says which is which). Never written into test/files.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <poppler.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/Image.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AdoptAnnotations.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/MergedPdf.h"
#include "session/PageNoteSpace.h"
#include "session/StickyNote.h"
#include "undo/UndoRedoHandler.h"

using namespace xqt;
using QH = QPDFObjectHandle;

namespace {

/// Pages of this size, each with a black square of 6 pt centred at `dot` (PDF space, y up) when given
void makePdf(const fs::path& p, int pages, double w = 595, double h = 842, std::pair<double, double> dot = {-1, -1}) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), w, h);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20);
    for (int i = 0; i < pages; ++i) {
        if (dot.first < 0) {
            cairo_move_to(cr, 72, 100);
            cairo_show_text(cr, ("page" + std::to_string(i + 1)).c_str());
        } else {
            cairo_rectangle(cr, dot.first - 3, h - dot.second - 3, 6, 6);
            cairo_fill(cr);
        }
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// Change the file with qpdf (as another app would)
void editPdf(const fs::path& file, const std::function<void(QPDF&)>& change) {
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

void setProducer(QPDF& q, const std::string& producer) {
    QH info = q.getTrailer().getKey("/Info");
    if (!info.isDictionary()) {
        info = q.makeIndirectObject(QH::newDictionary());
        q.getTrailer().replaceKey("/Info", info);
    }
    info.replaceKey("/Producer", QH::newUnicodeString(producer));
}

QPDFPageObjectHelper pageOf(QPDF& q, size_t i) { return QPDFPageDocumentHelper(q).getAllPages().at(i); }

/// Add an annotation (PDF syntax of its dictionary) to page `i`; returns it (indirect)
QH annotate(QPDF& q, size_t i, const std::string& dict) {
    QH page = pageOf(q, i).getObjectHandle();
    QH a = q.makeIndirectObject(QH::parse(dict));
    a.replaceKey("/P", page);
    QH annots = page.getKey("/Annots");
    if (!annots.isArray()) {
        annots = QH::newArray();
        page.replaceKey("/Annots", annots);
    }
    annots.appendItem(a);
    return a;
}

/// An appearance stream for an annotation
void giveAppearance(QPDF& q, QH a, const std::string& content, const std::string& bbox,
                    const std::string& resources = "<< >>") {
    QH ap = QH::newStream(&q, content);
    ap.getDict().replaceKey("/Type", QH::newName("/XObject"));
    ap.getDict().replaceKey("/Subtype", QH::newName("/Form"));
    ap.getDict().replaceKey("/BBox", QH::parse(bbox));
    ap.getDict().replaceKey("/Resources", QH::parse(resources));
    QH d = QH::newDictionary();
    d.replaceKey("/N", ap);
    a.replaceKey("/AP", d);
}

std::vector<std::string> annotationTypes(const fs::path& pdf, size_t page) {
    QPDF q;
    q.processFile(pdf.string().c_str());
    std::vector<std::string> out;
    QH annots = pageOf(q, page).getObjectHandle().getKey("/Annots");
    for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
        QH a = annots.getArrayItem(i);
        std::string t = a.getKey("/Subtype").isName() ? a.getKey("/Subtype").getName() : "?";
        if (a.getKey("/NM").isString() && a.getKey("/NM").getUTF8Value().rfind("xopp:", 0) == 0) {
            t += "(ours)";
        }
        out.push_back(t);
    }
    return out;
}

const Layer* layerNamed(const XojPage& page, const std::string& name) {
    for (const Layer* l: page.getLayersView()) {
        if (l->getName() == name) {
            return l;
        }
    }
    return nullptr;
}

std::vector<const Element*> elementsOf(const Layer* l) {
    std::vector<const Element*> out;
    if (l) {
        for (const Element* e: l->getElementsView()) {
            out.push_back(e);
        }
    }
    return out;
}

std::vector<const Layer*> notesOf(const XojPage& page) {
    std::vector<const Layer*> out;
    for (const Layer* l: page.getLayersView()) {
        if (sticky::isNote(*l)) {
            out.push_back(l);
        }
    }
    return out;
}

std::string qpdfCheck(const fs::path& pdf, int& code) {
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

/// Where poppler draws the dark square of a page of `pdf` (its centre, in points of the shown page)
std::pair<double, double> darkSquareOf(const fs::path& pdf, int page) {
    GError* e = nullptr;
    const std::string uri = "file://" + pdf.string();
    PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), nullptr, &e);
    if (!doc) {
        return {-1, -1};
    }
    PopplerPage* p = poppler_document_get_page(doc, page);
    double w = 0, h = 0;
    poppler_page_get_size(p, &w, &h);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, int(w), int(h));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    poppler_page_render(p, cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    double sx = 0, sy = 0, n = 0;
    for (int y = 0; y < int(h); ++y) {
        for (int x = 0; x < int(w); ++x) {
            const uint32_t px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
            if ((px & 0xff) < 60 && ((px >> 8) & 0xff) < 60 && ((px >> 16) & 0xff) < 60) {
                sx += x + 0.5, sy += y + 0.5, n += 1;
            }
        }
    }
    cairo_surface_destroy(s);
    g_object_unref(p);
    g_object_unref(doc);
    return n > 0 ? std::make_pair(sx / n, sy / n) : std::make_pair(-1.0, -1.0);
}

class AdoptAnnotationsTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    std::unique_ptr<DocumentSession> open(const fs::path& file) {
        auto loaded = DocumentSession::loadFile(file);
        EXPECT_TRUE(loaded.document) << loaded.error;
        return loaded.document ? std::make_unique<DocumentSession>(*app, std::move(loaded.document)) : nullptr;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};

/// Every standard kind on page 1, as a Drawboard-like app writes them (Producer "Drawboard PDF", assumed)
void standardKinds(QPDF& q) {
    setProducer(q, "Drawboard PDF");
    // Ink: two strokes, 3 pt, red
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Ink /Rect [90 690 210 760] /C [1 0 0] /BS << /W 3 >> "
             "/InkList [[100 700 150 750 200 700] [100 720 200 720]] >>");
    // A highlighter pen: translucent ink
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Ink /Rect [90 590 310 610] /C [1 1 0] /CA 0.4 /BS << /W 12 >> "
             "/InkList [[100 600 300 600]] >>");
    // A highlight over a line of text (Acrobat's order of the points), with a comment
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Highlight /Rect [70 730 300 750] /C [0 1 0] "
             "/QuadPoints [70 750 300 750 70 730 300 730] /Contents (Look at this) >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Underline /Rect [70 500 200 515] /C [0 0 1] "
             "/QuadPoints [70 515 200 515 70 500 200 500] >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /StrikeOut /Rect [70 480 200 495] /C [1 0 0] "
             "/QuadPoints [70 495 200 495 70 480 200 480] >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Squiggly /Rect [70 460 200 475] /C [1 0 1] "
             "/QuadPoints [70 475 200 475 70 460 200 460] >>");
    // A text box: 14 pt, blue
    annotate(q, 0,
             "<< /Type /Annot /Subtype /FreeText /Rect [300 300 500 360] /DA (/Helv 14 Tf 0 0 1 rg) "
             "/Contents (Typed in another app) /BS << /W 0 >> >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Square /Rect [100 100 200 200] /C [0 0 0] /IC [1 0.5 0] "
             "/BS << /W 2 >> >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Circle /Rect [250 100 350 160] /C [0 0.5 0] /BS << /W 1 /S /D /D [3 2] >> >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Line /Rect [0 0 1 1] /L [400 100 500 200] /C [0 0 0] /LE [/None /OpenArrow] "
             "/BS << /W 1.5 >> >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /Polygon /Rect [380 380 480 480] /C [0 0 1] "
             "/Vertices [400 400 460 400 430 460] >>");
    annotate(q, 0,
             "<< /Type /Annot /Subtype /PolyLine /Rect [380 380 480 480] /C [0 0 1] "
             "/Vertices [400 250 440 280 480 250] >>");
    QH note = annotate(q, 0,
                       "<< /Type /Annot /Subtype /Text /Rect [520 780 540 800] /Contents (A note *with* marks) "
                       "/Name /Comment >>");
    QH popup = annotate(q, 0, "<< /Type /Annot /Subtype /Popup /Rect [540 600 595 800] >>");
    popup.replaceKey("/Parent", note);
    note.replaceKey("/Popup", popup);
    // A stamp (a signature drawn as a path)
    QH stamp = annotate(q, 0, "<< /Type /Annot /Subtype /Stamp /Rect [300 600 400 640] /Name /Signature >>");
    giveAppearance(q, stamp, "0 0 1 RG 4 w 10 10 m 50 35 l 90 5 l S", "[0 0 100 40]");
    // Not converted: a link, a form field, a hidden ink, a caret (kept)
    annotate(q, 0, "<< /Type /Annot /Subtype /Link /Rect [10 10 50 50] /A << /S /URI /URI (https://example.org) >> >>");
    annotate(q, 0, "<< /Type /Annot /Subtype /Widget /FT /Tx /Rect [10 60 50 80] /T (field) >>");
    annotate(q, 0, "<< /Type /Annot /Subtype /Ink /F 2 /Rect [0 0 10 10] /InkList [[1 1 5 5]] >>");
    annotate(q, 0, "<< /Type /Annot /Subtype /Caret /Rect [20 20 30 30] >>");
}

}  // namespace

TEST_F(AdoptAnnotationsTest, scanCountsWhatCanBeMadeEditableAndNamesTheApp) {
    makePdf(path("a.pdf"), 2);
    editPdf(path("a.pdf"), [](QPDF& q) {
        standardKinds(q);
        annotate(q, 0, "<< /Type /Annot /Subtype /Ink /NM (xopp:p1-l1) /Rect [0 0 10 10] /InkList [[1 1 5 5]] >>");
    });
    const auto s = adopt::scan(path("a.pdf"));
    ASSERT_TRUE(s.ok) << s.error;
    EXPECT_EQ(s.count, 14u) << "links, fields, popups, ours, hidden ones and carets are not counted";
    EXPECT_EQ(s.app, "Drawboard PDF");
    EXPECT_EQ(s.kinds.at("Ink"), 2u);
    EXPECT_EQ(adopt::scan(path("a.pdf"), {1}).count, 0u) << "only the pages asked for";

    EXPECT_EQ(adopt::appOfProducer("GoodNotes 5\n"), "GoodNotes");
    EXPECT_EQ(adopt::appOfProducer("macOS Version 14.5 (Build 23F79) Quartz PDFContext\n"), "Preview");
    EXPECT_EQ(adopt::appOfProducer("pdfTeX-1.40.25\nLaTeX with hyperref"), "");
    EXPECT_EQ(adopt::layerName(""), "From another app");
    EXPECT_EQ(adopt::layerName("GoodNotes"), "From GoodNotes");
}

TEST_F(AdoptAnnotationsTest, eachStandardKindBecomesItsElement) {
    makePdf(path("a.pdf"), 2);
    editPdf(path("a.pdf"), standardKinds);
    auto session = open(path("a.pdf"));
    ASSERT_TRUE(session);
    std::string error;
    size_t converted = 0;
    ASSERT_TRUE(session->adoptAnnotations(error, &converted)) << error;
    EXPECT_EQ(converted, 14u);
    Document& d = *session->getDocument();
    const XojPage& page = *d.getPage(0);
    const auto els = elementsOf(layerNamed(page, "From Drawboard PDF"));
    ASSERT_GE(els.size(), 13u);
    size_t i = 0;
    auto stroke = [&](size_t k) { return dynamic_cast<const Stroke*>(els.at(k)); };
    // Ink: one stroke per path, its width and colour, placed with y down (842 - y)
    ASSERT_TRUE(stroke(0));
    EXPECT_EQ(stroke(0)->getToolType(), StrokeTool::PEN);
    EXPECT_DOUBLE_EQ(stroke(0)->getWidth(), 3);
    EXPECT_EQ(uint32_t(stroke(0)->getColor()) & 0xffffff, 0xff0000u);
    ASSERT_EQ(stroke(0)->getPointCount(), 3u);
    EXPECT_DOUBLE_EQ(stroke(0)->getPointVector()[0].x, 100);
    EXPECT_DOUBLE_EQ(stroke(0)->getPointVector()[0].y, 142);
    EXPECT_DOUBLE_EQ(stroke(0)->getPointVector()[1].y, 92);
    EXPECT_FALSE(stroke(0)->hasPressure()) << "the standard has no pressure";
    ASSERT_TRUE(stroke(1));
    i = 2;
    EXPECT_EQ(stroke(i)->getToolType(), StrokeTool::HIGHLIGHTER) << "translucent ink is a highlighter";
    EXPECT_DOUBLE_EQ(stroke(i)->getWidth(), 12);
    ++i;
    // Highlight: across the piece, as wide as it is high
    EXPECT_EQ(stroke(i)->getToolType(), StrokeTool::HIGHLIGHTER);
    EXPECT_DOUBLE_EQ(stroke(i)->getWidth(), 20);
    EXPECT_DOUBLE_EQ(stroke(i)->getPointVector()[0].x, 70);
    EXPECT_DOUBLE_EQ(stroke(i)->getPointVector()[0].y, 102);
    EXPECT_DOUBLE_EQ(stroke(i)->getPointVector()[1].x, 300);
    ++i;
    // Underline at the bottom, strike out through the middle, squiggly a zigzag
    EXPECT_GT(stroke(i)->getPointVector()[0].y, 335);  // (the piece is 327..342)
    EXPECT_EQ(uint32_t(stroke(i)->getColor()) & 0xffffff, 0x0000ffu);
    ++i;
    EXPECT_NEAR(stroke(i)->getPointVector()[0].y, 842 - 487.5, 0.01);
    ++i;
    EXPECT_GT(stroke(i)->getPointCount(), 10u);
    ++i;
    // The text box
    const auto* text = dynamic_cast<const Text*>(els.at(i));
    ASSERT_TRUE(text);
    EXPECT_EQ(text->getText(), "Typed in another app");
    EXPECT_DOUBLE_EQ(text->getFontSize(), 14);
    EXPECT_EQ(uint32_t(text->getColor()) & 0xffffff, 0x0000ffu);
    EXPECT_NEAR(text->getOrigin().x, 302, 0.01);
    EXPECT_NEAR(text->getOrigin().y, 842 - 360 + 2, 0.01);
    ++i;
    // Square: closed, filled
    EXPECT_EQ(stroke(i)->getPointCount(), 5u);
    EXPECT_EQ(stroke(i)->getFill(), 255);
    EXPECT_EQ(stroke(i)->getFillColor().value_or(Color(0, 0, 0)), Color(255, 128, 0));
    ++i;
    // Circle: dashed
    EXPECT_TRUE(stroke(i)->getLineStyle().hasDashes());
    ++i;
    // Line with an arrow head at its end
    ASSERT_EQ(stroke(i)->getPointCount(), 2u);
    EXPECT_DOUBLE_EQ(stroke(i)->getPointVector()[1].x, 500);
    EXPECT_DOUBLE_EQ(stroke(i)->getPointVector()[1].y, 642);
    ++i;
    EXPECT_EQ(stroke(i)->getPointCount(), 3u) << "the arrow head";
    ++i;
    EXPECT_EQ(stroke(i)->getPointCount(), 4u) << "polygon: closed";
    ++i;
    EXPECT_EQ(stroke(i)->getPointCount(), 3u) << "polyline";
    ++i;
    // The stamp as a picture of its rectangle
    const auto* image = dynamic_cast<const Image*>(els.at(i));
    ASSERT_TRUE(image);
    EXPECT_NEAR(image->getBoundingBox().width, 100, 0.5);
    EXPECT_NEAR(image->getBoundingBox().height, 40, 0.5);
    EXPECT_NEAR(image->getOrigin().x, 300, 0.5);
    EXPECT_NEAR(image->getOrigin().y, 842 - 640, 0.5);
    EXPECT_EQ(els.size(), i + 1);

    // Notes: the note itself, and the highlight's comment beside it
    const auto notes = notesOf(page);
    ASSERT_EQ(notes.size(), 2u);
    std::vector<std::string> texts;
    for (const Layer* n: notes) {
        const Text* t = sticky::textOf(*n);
        ASSERT_TRUE(t);
        texts.push_back(t->getText());
    }
    EXPECT_EQ(texts, (std::vector<std::string>{"Look at this", "A note \\*with\\* marks"}));
    EXPECT_EQ(page.getLayersView().back(), notes.back()) << "notes on top";

    // The background no longer has them; what was not converted stays
    EXPECT_EQ(annotationTypes(d.getPdfFilepath(), 0), (std::vector<std::string>{"/Link", "/Widget", "/Ink", "/Caret"}));
    EXPECT_TRUE(MergedPdf::inCache(d.getPdfFilepath()));
    EXPECT_EQ(MergedPdf::kindOf(d.getPdfFilepath()), MergedPdf::Kind::WithSource);
    EXPECT_EQ(session->annotatedPdf(), path("a.pdf")) << "still the user's PDF for the user";
    EXPECT_EQ(annotationTypes(path("a.pdf"), 0).size(), 19u) << "the user's PDF is not touched";
    EXPECT_TRUE(session->isModified());
}

TEST_F(AdoptAnnotationsTest, undoBringsTheOriginalsBackAndRedoTakesThemAgain) {
    makePdf(path("a.pdf"), 2);
    editPdf(path("a.pdf"), [](QPDF& q) {
        annotate(q, 1, "<< /Type /Annot /Subtype /Ink /Rect [0 0 300 300] /InkList [[100 100 200 200]] >>");
    });
    auto session = open(path("a.pdf"));
    ASSERT_TRUE(session);
    Document& d = *session->getDocument();
    const size_t layers = d.getPage(1)->getLayerCount();
    std::string error;
    ASSERT_TRUE(session->adoptAnnotations(error)) << error;
    const fs::path copy = d.getPdfFilepath();
    EXPECT_EQ(d.getPage(1)->getLayerCount(), layers + 1);
    EXPECT_EQ(d.getPage(0)->getLayerCount(), 1u) << "pages without them get no layer";
    EXPECT_TRUE(annotationTypes(copy, 1).empty());

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(d.getPage(1)->getLayerCount(), layers);
    EXPECT_EQ(d.getPdfFilepath(), path("a.pdf"));
    EXPECT_EQ(annotationTypes(d.getPdfFilepath(), 1), std::vector<std::string>{"/Ink"});
    EXPECT_EQ(session->annotatedPdf(), path("a.pdf"));

    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(d.getPage(1)->getLayerCount(), layers + 1);
    EXPECT_EQ(d.getPdfFilepath(), copy);
    EXPECT_EQ(adopt::scan(d.getPdfFilepath()).count, 0u);
}

TEST_F(AdoptAnnotationsTest, placementFollowsRotationAndCropBox) {
    // A page whose crop box is not at the origin, turned a quarter: the dark square of the page and an ink dot drawn
    // over it by "another app" must land at the same place
    const std::pair<double, double> dot{150, 300};
    makePdf(path("r.pdf"), 1, 600, 800, dot);
    for (int rotate: {0, 90, 180, 270}) {
        const fs::path file = path(("r" + std::to_string(rotate) + ".pdf").c_str());
        fs::copy_file(path("r.pdf"), file, fs::copy_options::overwrite_existing);
        editPdf(file, [&](QPDF& q) {
            QH page = pageOf(q, 0).getObjectHandle();
            page.replaceKey("/CropBox", QH::parse("[50 100 550 700]"));
            page.replaceKey("/Rotate", QH::newInteger(rotate));
            annotate(q, 0,
                     "<< /Type /Annot /Subtype /Ink /Rect [140 290 160 310] /BS << /W 2 >> "
                     "/InkList [[150 300 150.5 300]] >>");
        });
        auto session = open(file);
        ASSERT_TRUE(session);
        Document& d = *session->getDocument();
        const auto [px, py] = darkSquareOf(file, 0);
        ASSERT_GE(px, 0) << rotate;
        std::string error;
        ASSERT_TRUE(session->adoptAnnotations(error)) << error;
        const auto els = elementsOf(layerNamed(*d.getPage(0), "From another app"));
        ASSERT_EQ(els.size(), 1u) << rotate;
        const Point p = static_cast<const Stroke*>(els[0])->getPointVector()[0];
        EXPECT_NEAR(p.x, px, 1.5) << "rotate " << rotate;
        EXPECT_NEAR(p.y, py, 1.5) << "rotate " << rotate;
        EXPECT_DOUBLE_EQ(d.getPage(0)->getWidth(), rotate % 180 ? 600 : 500);
    }
}

TEST_F(AdoptAnnotationsTest, roundTripAsPdfWithNotesAndAsXopp) {
    makePdf(path("lecture.pdf"), 2);
    editPdf(path("lecture.pdf"), standardKinds);
    {
        auto session = open(path("lecture.pdf"));
        std::string error;
        ASSERT_TRUE(session->adoptAnnotations(error)) << error;
        ASSERT_TRUE(session->saveAsHybrid(path("lecture.notes.pdf")).ok);
    }
    // Other apps see our annotations in place of theirs
    const auto types = annotationTypes(path("lecture.notes.pdf"), 0);
    EXPECT_EQ(std::count(types.begin(), types.end(), "/Highlight"), 0);
    EXPECT_EQ(std::count(types.begin(), types.end(), "/FreeText"), 0);
    EXPECT_EQ(std::count(types.begin(), types.end(), "/Link"), 1) << "kept";
    EXPECT_GE(std::count_if(types.begin(), types.end(),
                            [](const std::string& t) { return t.find("(ours)") != std::string::npos; }),
              3)
            << "the layer and the two notes";
    int code = -1;
    const std::string report = qpdfCheck(path("lecture.notes.pdf"), code);
    EXPECT_EQ(code, 0) << report;
    {
        // Reopened: our ink, editable; nothing left to adopt
        auto again = DocumentSession::loadFile(path("lecture.notes.pdf"));
        ASSERT_TRUE(again.document);
        EXPECT_TRUE(again.hybridChanged.empty());
        EXPECT_GE(elementsOf(layerNamed(*again.document->getPage(0), "From Drawboard PDF")).size(), 13u);
        EXPECT_EQ(notesOf(*again.document->getPage(0)).size(), 2u);
        EXPECT_EQ(adopt::scan(again.document->getPdfFilepath()).count, 0u);
    }
    {
        // As a .xopp: the PDF without them goes beside it; the user's PDF stays as it was
        auto session = open(path("lecture.pdf"));
        std::string error;
        ASSERT_TRUE(session->adoptAnnotations(error)) << error;
        ASSERT_TRUE(session->saveAs(path("lecture.xopp")).ok);
        EXPECT_EQ(session->getDocument()->getPdfFilepath(), MergedPdf::sidecarOf(path("lecture.xopp")));
    }
    auto xopp = DocumentSession::loadFile(path("lecture.xopp"));
    ASSERT_TRUE(xopp.document);
    EXPECT_EQ(adopt::scan(xopp.document->getPdfFilepath()).count, 0u);
    EXPECT_GE(elementsOf(layerNamed(*xopp.document->getPage(0), "From Drawboard PDF")).size(), 13u);
    EXPECT_EQ(adopt::scan(path("lecture.pdf")).count, 14u) << "the user's PDF is not touched";
}

TEST_F(AdoptAnnotationsTest, aPdfWithNotesMarkedUpElsewhereAdoptsAndSavesItsMarks) {
    // Our PDF with notes, then another app added a highlight and a note to it
    makePdf(path("lecture.pdf"), 2);
    {
        auto session = open(path("lecture.pdf"));
        Layer* l = session->getDocument()->getPage(0)->getSelectedLayer();
        auto s = std::make_unique<Stroke>();
        s->setWidth(2);
        s->addPoint(Point(10, 10));
        s->addPoint(Point(50, 50));
        l->addElement(std::move(s));
        ASSERT_TRUE(session->saveAsHybrid(path("lecture.notes.pdf")).ok);
    }
    editPdf(path("lecture.notes.pdf"), [](QPDF& q) {
        setProducer(q, "Preview");
        QH h = annotate(q, 1,
                        "<< /Type /Annot /Subtype /Highlight /Rect [70 730 300 750] /C [1 1 0] "
                        "/QuadPoints [70 750 300 750 70 730 300 730] >>");
        h.replaceKey("/AAPL:AKExtras", QH::parse("<< /AAPL:AKAnnotationObject (data) >>"));
    });
    auto loaded = DocumentSession::loadFile(path("lecture.notes.pdf"));
    ASSERT_TRUE(loaded.document);
    EXPECT_TRUE(loaded.hybridChanged.empty()) << "ours are as we wrote them";
    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_EQ(adopt::scan(session.getDocument()->getPdfFilepath()).app, "Preview") << "AnnotationKit's keys";
    std::string error;
    size_t converted = 0;
    ASSERT_TRUE(session.adoptAnnotations(error, &converted)) << error;
    EXPECT_EQ(converted, 1u);
    EXPECT_EQ(elementsOf(layerNamed(*session.getDocument()->getPage(1), "From Preview")).size(), 1u);
    ASSERT_TRUE(session.save().ok);
    EXPECT_EQ(annotationTypes(path("lecture.notes.pdf"), 1), std::vector<std::string>{"/Ink(ours)"})
            << "written in full: the other app's highlight is ours now";
    auto again = DocumentSession::loadFile(path("lecture.notes.pdf"));
    ASSERT_TRUE(again.document);
    EXPECT_TRUE(again.hybridChanged.empty());
    EXPECT_EQ(adopt::scan(again.document->getPdfFilepath()).count, 0u);
    EXPECT_EQ(elementsOf(layerNamed(*again.document->getPage(1), "From Preview")).size(), 1u);
}

TEST_F(AdoptAnnotationsTest, undoAfterTheNotesWentIntoThePdfItselfBringsTheOriginalsBack) {
    makePdf(path("lecture.pdf"), 1);
    editPdf(path("lecture.pdf"), [](QPDF& q) {
        annotate(q, 0, "<< /Type /Annot /Subtype /Ink /Rect [0 0 300 300] /InkList [[100 100 200 200]] >>");
    });
    auto session = open(path("lecture.pdf"));
    std::string error;
    ASSERT_TRUE(session->adoptAnnotations(error)) << error;
    ASSERT_TRUE(session->saveAsHybrid(path("lecture.pdf")).ok);  // (into the PDF itself: it changes)
    EXPECT_EQ(annotationTypes(path("lecture.pdf"), 0), std::vector<std::string>{"/Ink(ours)"});
    session->getUndoRedoHandler()->undo();
    Document& d = *session->getDocument();
    EXPECT_EQ(annotationTypes(d.getPdfFilepath(), 0), std::vector<std::string>{"/Ink"})
            << "the original bytes, kept when they were adopted";
    EXPECT_EQ(elementsOf(layerNamed(*d.getPage(0), "From another app")).size(), 0u);
}

TEST_F(AdoptAnnotationsTest, appStyles) {
    // As GoodNotes is assumed to write an Editable export (qt/docs/features/adopt-annotations.md): ink per stroke, the
    // highlighter as ink with an opacity in its appearance only
    makePdf(path("gn.pdf"), 1);
    editPdf(path("gn.pdf"), [](QPDF& q) {
        setProducer(q, "GoodNotes 6");
        annotate(q, 0,
                 "<< /Type /Annot /Subtype /Ink /Rect [0 0 300 300] /C [0.1 0.1 0.6] /BS << /W 1.2 >> "
                 "/InkList [[100 100 110 105 120 103 130 110]] >>");
        QH hl = annotate(q, 0,
                         "<< /Type /Annot /Subtype /Ink /Rect [0 0 300 300] /C [1 0.9 0] /BS << /W 14 >> "
                         "/InkList [[100 200 250 200]] >>");
        giveAppearance(q, hl, "/G0 gs 1 0.9 0 RG 14 w 0 0 m 150 0 l S", "[0 0 300 300]",
                       "<< /ExtGState << /G0 << /CA 0.35 >> >> >>");
    });
    auto gn = open(path("gn.pdf"));
    std::string error;
    ASSERT_TRUE(gn->adoptAnnotations(error)) << error;
    const auto els = elementsOf(layerNamed(*gn->getDocument()->getPage(0), "From GoodNotes"));
    ASSERT_EQ(els.size(), 2u);
    EXPECT_EQ(static_cast<const Stroke*>(els[0])->getToolType(), StrokeTool::PEN);
    EXPECT_EQ(static_cast<const Stroke*>(els[1])->getToolType(), StrokeTool::HIGHLIGHTER);

    // As Preview writes a signature and an arrow whose end lies outside its rectangle (verified: KrankyBearReader)
    makePdf(path("pv.pdf"), 1);
    editPdf(path("pv.pdf"), [](QPDF& q) {
        setProducer(q, "macOS Version 14.5 (Build 23F79) Quartz PDFContext");
        QH sig = annotate(q, 0, "<< /Type /Annot /Subtype /Stamp /Rect [300 100 420 150] >>");
        giveAppearance(q, sig, "0 0 0 RG 2 w 5 5 m 40 45 l 80 5 l 115 40 l S", "[0 0 120 50]");
        annotate(q, 0,
                 "<< /Type /Annot /Subtype /Line /Rect [100 100 110 110] /L [100 100 300 250] /C [1 0 0] "
                 "/LE [/None /ClosedArrow] >>");
    });
    auto pv = open(path("pv.pdf"));
    ASSERT_TRUE(pv->adoptAnnotations(error)) << error;
    const auto pels = elementsOf(layerNamed(*pv->getDocument()->getPage(0), "From Preview"));
    ASSERT_EQ(pels.size(), 3u);
    EXPECT_TRUE(dynamic_cast<const Image*>(pels[0]));
    EXPECT_DOUBLE_EQ(static_cast<const Stroke*>(pels[1])->getPointVector()[1].x, 300) << "placed by /L";
    EXPECT_EQ(static_cast<const Stroke*>(pels[2])->getFill(), 255) << "a closed arrow head";
}

TEST_F(AdoptAnnotationsTest, aPageWithSpaceForNotesGetsThemAtTheSlide) {
    makePdf(path("s.pdf"), 1);
    editPdf(path("s.pdf"), [](QPDF& q) {
        annotate(q, 0, "<< /Type /Annot /Subtype /Ink /Rect [0 0 300 300] /InkList [[100 700 200 700]] >>");
    });
    auto session = open(path("s.pdf"));
    notespace::Amounts space;
    space.left = 100;
    space.top = 50;
    ASSERT_EQ(notespace::apply(*session, {0}, space), 1u);
    std::string error;
    ASSERT_TRUE(session->adoptAnnotations(error)) << error;
    const auto els = elementsOf(layerNamed(*session->getDocument()->getPage(0), "From another app"));
    ASSERT_EQ(els.size(), 1u);
    const Point p = static_cast<const Stroke*>(els[0])->getPointVector()[0];
    EXPECT_DOUBLE_EQ(p.x, 200);
    EXPECT_DOUBLE_EQ(p.y, 142 + 50);
}
