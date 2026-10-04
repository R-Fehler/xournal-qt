/*
 * xournal-qt: the pen's styles (qt/pen-styles): upstream's line styles and fillings drawn by the canvas, kept in
 * .xopp and shown the same in an export; the laser pointer, whose ink fades and is never in the document.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>

#include <cairo.h>
#include <gtest/gtest.h>

#include "control/ExportHelper.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/LineStyle.h"
#include "model/Stroke.h"
#include "model/StrokeStyle.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/PenFill.h"
#include "undo/UndoRedoHandler.h"
#include "util/GzUtil.h"
#include "view/DocumentView.h"
#include "control/settings/Settings.h"
#include "util/serializing/BinObjectEncoding.h"
#include "util/serializing/ObjectInputStream.h"
#include "util/serializing/ObjectOutputStream.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "PenHover.h"

using namespace xqt;

namespace {
class PenStylesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        input = std::make_unique<CanvasInput>(*view);
        tools()->selectTool(TOOL_PEN);
        tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
        tools()->setSize(TOOL_SIZE_THICK);  // (a dot is a few pixels at the canvas' zoom)
        PenHover::instance().reset();
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }

    ToolHandler* tools() const { return app->getToolHandler(); }

    void processEvents(int ms = 50) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }

    QPointF viewPos(QPointF pagePoint) const {
        return view->pageViewRect(0).topLeft() + pagePoint * view->getViewController().zoom();
    }

    void tablet(QEvent::Type type, QPointF pos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->tabletEvent(&e, pos);
    }

    /// A stroke along a line (page points), at an even pressure
    void drawLine(QPointF from, QPointF to, int steps = 40) {
        tablet(QEvent::TabletPress, viewPos(from), 0.6, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= steps; ++i) {
            tablet(QEvent::TabletMove, viewPos(from + (to - from) * (static_cast<double>(i) / steps)), 0.6,
                   Qt::NoButton, Qt::LeftButton);
        }
        tablet(QEvent::TabletRelease, viewPos(to), 0.0, Qt::LeftButton, Qt::NoButton);
        processEvents();
    }

    std::vector<const Stroke*> strokes() const {
        std::vector<const Stroke*> out;
        for (const Element* e: session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView()) {
            if (auto* s = dynamic_cast<const Stroke*>(e)) {
                out.push_back(s);
            }
        }
        return out;
    }

    /// The canvas' picture of a row of the page (y in points, from x0 to x1 points): dark or not, pixel by pixel
    std::vector<bool> canvasRow(double y, double x0, double x1) {
        processEvents(100);
        auto* page = view->getPage(0);
        const auto info = page->bufferInfo();
        EXPECT_TRUE(info.valid);
        const double s = info.zoom * info.dpiScale;
        const QImage tile = page->composeTile(QRect(static_cast<int>(x0 * s), static_cast<int>(y * s),
                                                    static_cast<int>((x1 - x0) * s), 1));
        std::vector<bool> row;
        for (int x = 0; x < tile.width(); ++x) {
            row.push_back(qGray(tile.pixel(x, 0)) < 170);
        }
        return row;
    }

    /// The canvas' color at a point of the page (points)
    QColor canvasAt(QPointF p) {
        processEvents(100);
        auto* page = view->getPage(0);
        const auto info = page->bufferInfo();
        EXPECT_TRUE(info.valid);
        const double s = info.zoom * info.dpiScale;
        const QImage tile =
                page->composeTile(QRect(static_cast<int>(p.x() * s), static_cast<int>(p.y() * s), 1, 1));
        return QColor(tile.pixel(0, 0));
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"test pen", 1101, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    ulong timestamp = 1000;
};

/// A .xopp file's XML
std::string unzipped(const fs::path& file) {
    std::string xml;
    gzFile f = GzUtil::openPath(file, "r");
    EXPECT_NE(f, nullptr);
    if (!f) {
        return xml;
    }
    char buffer[4096];
    for (int n; (n = gzread(f, buffer, sizeof(buffer))) > 0;) {
        xml.append(buffer, static_cast<size_t>(n));
    }
    gzclose(f);
    return xml;
}

/// A PDF page's color at a point (points), as poppler draws it
QColor pdfAt(const fs::path& pdf, QPointF p, double scale = 2) {
    XojPdfDocument doc;
    EXPECT_TRUE(doc.load(pdf, "", nullptr)) << pdf;
    XojPdfPageSPtr page = doc.getPage(0);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, static_cast<int>(page->getWidth() * scale),
                                                    static_cast<int>(page->getHeight() * scale));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    page->render(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    const unsigned char* px = cairo_image_surface_get_data(s) +
                              static_cast<int>(p.y() * scale) * cairo_image_surface_get_stride(s) +
                              4 * static_cast<int>(p.x() * scale);
    QColor c(px[2], px[1], px[0]);
    cairo_surface_destroy(s);
    return c;
}
/// Reddish (a red filling at half opacity on white: about 255, 128, 128)
bool reddish(const QColor& c) { return c.red() > 200 && c.green() < 200 && c.blue() < 200 && c.red() - c.green() > 50; }
/// Grayish (the black line's color at half opacity)
bool grayish(const QColor& c) {
    return c.red() < 220 && c.red() > 40 && std::abs(c.red() - c.green()) < 20 && std::abs(c.red() - c.blue()) < 20;
}

/// How often a row of pixels changes between ink and paper
int changes(const std::vector<bool>& row) {
    int n = 0;
    for (size_t i = 1; i < row.size(); ++i) {
        n += row[i] != row[i - 1];
    }
    return n;
}
int inked(const std::vector<bool>& row) {
    int n = 0;
    for (bool b: row) {
        n += b;
    }
    return n;
}

/// A page of a PDF file as poppler draws it (white behind), `scale` pixels per point: a row of it, dark or not
std::vector<bool> pdfRow(const fs::path& pdf, double y, double x0, double x1, double scale = 4) {
    XojPdfDocument doc;
    EXPECT_TRUE(doc.load(pdf, "", nullptr)) << pdf;
    XojPdfPageSPtr p = doc.getPage(0);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(p->getWidth() * scale),
                                                    static_cast<int>(p->getHeight() * scale));
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    p->render(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    std::vector<bool> row;
    const int py = static_cast<int>(y * scale);
    const unsigned char* line = cairo_image_surface_get_data(s) + py * cairo_image_surface_get_stride(s);
    for (int x = static_cast<int>(x0 * scale); x < static_cast<int>(x1 * scale); ++x) {
        const unsigned char* px = line + 4 * x;
        row.push_back((px[0] + px[1] + px[2]) / 3 < 170);
    }
    cairo_surface_destroy(s);
    return row;
}
}  // namespace

TEST_F(PenStylesTest, aDashedStrokeIsDrawnWithGapsOnTheCanvas) {
    drawLine(QPointF(100, 200), QPointF(400, 200));  // solid, for comparison
    tools()->setLineStyle(StrokeStyle::parseStyle("dash"));
    drawLine(QPointF(100, 300), QPointF(400, 300));
    tools()->setLineStyle(StrokeStyle::parseStyle("dot"));
    drawLine(QPointF(100, 400), QPointF(400, 400));

    const auto s = strokes();
    ASSERT_EQ(s.size(), 3u);
    EXPECT_FALSE(s[0]->getLineStyle().hasDashes());
    EXPECT_EQ(s[1]->getLineStyle().getDashes(), (std::vector<double>{6, 3}));
    EXPECT_EQ(s[2]->getLineStyle().getDashes(), (std::vector<double>{0.5, 3}));

    // The canvas draws them so: a solid line without gaps, the dashes about every 9 points (300 points: ~33 dashes)
    const auto solid = canvasRow(200, 120, 380);
    EXPECT_GT(inked(solid), static_cast<int>(solid.size() * 0.95));
    EXPECT_LE(changes(solid), 2);
    const auto dashed = canvasRow(300, 120, 380);
    EXPECT_GE(changes(dashed), 40) << "no gaps in the dashed stroke";
    EXPECT_GT(inked(dashed), static_cast<int>(dashed.size() * 0.5));  // (dashes longer than the gaps)
    const auto dotted = canvasRow(400, 120, 380);
    EXPECT_GE(changes(dotted), 40) << "no gaps between the dots";
    EXPECT_LT(inked(dotted), inked(dashed));  // (dots shorter than dashes)

    // The pen keeps its style; undo takes the stroke away with it
    EXPECT_EQ(tools()->getLineStyle().getDashes(), (std::vector<double>{0.5, 3}));
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(strokes().size(), 2u);
}

TEST_F(PenStylesTest, lineStylesAreKeptInXoppAndShownTheSameInAPdfExport) {
    const std::vector<std::string> names{"plain", "dash", "dashdot", "dot"};
    for (size_t i = 0; i < names.size(); ++i) {
        tools()->setLineStyle(StrokeStyle::parseStyle(names[i]));
        drawLine(QPointF(100, 150 + 100 * i), QPointF(400, 150 + 100 * i));
    }
    // A shape drawn with the pen takes its style too
    tools()->setLineStyle(StrokeStyle::parseStyle("dash"));
    tools()->setDrawingType(DRAWING_TYPE_RECTANGLE);
    drawLine(QPointF(100, 600), QPointF(400, 700));
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    ASSERT_EQ(strokes().size(), 5u);
    EXPECT_EQ(strokes()[4]->getLineStyle().getDashes(), (std::vector<double>{6, 3}));

    const fs::path xopp = fs::path(tmp.filePath("styles.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(xopp).ok);
    // Upstream's attribute: style="dash" (Xournal++ opens it the same)
    const std::string xml = unzipped(xopp);
    EXPECT_NE(xml.find("style=\"dash\""), std::string::npos);
    EXPECT_NE(xml.find("style=\"dashdot\""), std::string::npos);
    EXPECT_NE(xml.find("style=\"dot\""), std::string::npos);

    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document) << loaded.error;
    std::vector<std::vector<double>> dashes;
    for (const Element* e: loaded.document->getPage(0)->getSelectedLayer()->getElementsView()) {
        dashes.push_back(dynamic_cast<const Stroke*>(e)->getLineStyle().getDashes());
    }
    EXPECT_EQ(dashes, (std::vector<std::vector<double>>{
                              {}, {6, 3}, {6, 3, 0.5, 3}, {0.5, 3}, {6, 3}}));

    // The PDF export draws the dashes as the canvas does
    const fs::path pdf = fs::path(tmp.filePath("styles.pdf").toStdString());
    ExportHelper::exportPdf(session->getDocument(), pdf, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    const auto solid = pdfRow(pdf, 150, 120, 380);
    EXPECT_LE(changes(solid), 2);
    EXPECT_GE(changes(pdfRow(pdf, 250, 120, 380)), 40) << "the export lost the dashes";
    EXPECT_GE(changes(pdfRow(pdf, 450, 120, 380)), 40) << "the export lost the dots";
}

TEST_F(PenStylesTest, shapesAndStrokesAreFilledWithTheLinesColorOrAnother) {
    tools()->setColor(Colors::black, false);
    tools()->setFillEnabled(true);
    tools()->setPenFill(128);  // (upstream's default opacity of the filling)
    ASSERT_EQ(tools()->getFill(), 128);

    // The line's color (upstream's filling)
    tools()->setDrawingType(DRAWING_TYPE_RECTANGLE);
    drawLine(QPointF(100, 100), QPointF(250, 200));
    ASSERT_EQ(strokes().size(), 1u);
    EXPECT_EQ(strokes()[0]->getFill(), 128);
    EXPECT_FALSE(strokes()[0]->getFillColor());
    EXPECT_TRUE(grayish(canvasAt(QPointF(175, 150)))) << "filled with the line's color";

    // Another color: also while it is drawn (the overlay of the shape), and when it is done
    penfill::setColor(*app->getSettings(), TOOL_PEN, Colors::red);
    tools()->setDrawingType(DRAWING_TYPE_ELLIPSE);
    const QPointF from(300, 100), to(450, 200);
    tablet(QEvent::TabletPress, viewPos(from), 0.6, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 20; ++i) {
        tablet(QEvent::TabletMove, viewPos(from + (to - from) * (i / 20.0)), 0.6, Qt::NoButton, Qt::LeftButton);
    }
    EXPECT_TRUE(reddish(canvasAt(QPointF(375, 150)))) << "the shape being drawn is filled with the other color";
    tablet(QEvent::TabletRelease, viewPos(to), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_EQ(strokes().size(), 2u);
    ASSERT_TRUE(strokes()[1]->getFillColor());
    EXPECT_EQ(*strokes()[1]->getFillColor(), Colors::red);
    EXPECT_EQ(strokes()[1]->getColor(), Colors::black) << "the line keeps its color";
    EXPECT_TRUE(reddish(canvasAt(QPointF(375, 150))));

    // A freehand stroke (a triangle drawn by hand): filled as well, while drawn and when done
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    const std::vector<QPointF> corners{{100, 300}, {300, 300}, {200, 450}, {100, 300}};
    tablet(QEvent::TabletPress, viewPos(corners[0]), 0.6, Qt::LeftButton, Qt::LeftButton);
    for (size_t k = 1; k < corners.size(); ++k) {
        for (int i = 1; i <= 20; ++i) {
            tablet(QEvent::TabletMove, viewPos(corners[k - 1] + (corners[k] - corners[k - 1]) * (i / 20.0)), 0.6,
                   Qt::NoButton, Qt::LeftButton);
        }
    }
    EXPECT_TRUE(reddish(canvasAt(QPointF(200, 350)))) << "the stroke being drawn is filled";
    tablet(QEvent::TabletRelease, viewPos(corners.back()), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();
    ASSERT_EQ(strokes().size(), 3u);
    EXPECT_EQ(strokes()[2]->getFill(), 128);
    EXPECT_TRUE(reddish(canvasAt(QPointF(200, 350))));

    // Without the filling: nothing inside
    tools()->setFillEnabled(false);
    tools()->setDrawingType(DRAWING_TYPE_RECTANGLE);
    drawLine(QPointF(300, 300), QPointF(450, 400));
    ASSERT_EQ(strokes().size(), 4u);
    EXPECT_EQ(strokes()[3]->getFill(), -1);
    EXPECT_FALSE(strokes()[3]->getFillColor());
    EXPECT_EQ(canvasAt(QPointF(375, 350)), QColor(Qt::white));

    // The highlighter fills with its own color (upstream draws it through a mask in the line's color)
    tools()->selectTool(TOOL_HIGHLIGHTER);
    tools()->setFillEnabled(true);
    drawLine(QPointF(100, 600), QPointF(250, 700));
    ASSERT_EQ(strokes().size(), 5u);
    EXPECT_NE(strokes()[4]->getFill(), -1);
    EXPECT_FALSE(strokes()[4]->getFillColor());
    tools()->setFillEnabled(false);
    tools()->selectTool(TOOL_PEN);
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    penfill::setColor(*app->getSettings(), TOOL_PEN, std::nullopt);
}

TEST_F(PenStylesTest, theFillingIsSavedCopiedAndExported) {
    tools()->setColor(Colors::black, false);
    tools()->setFillEnabled(true);
    tools()->setPenFill(128);
    tools()->setDrawingType(DRAWING_TYPE_RECTANGLE);
    drawLine(QPointF(100, 100), QPointF(250, 200));  // the line's color
    penfill::setColor(*app->getSettings(), TOOL_PEN, Colors::red);
    drawLine(QPointF(300, 100), QPointF(450, 200));  // red
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    tools()->setFillEnabled(false);
    ASSERT_EQ(strokes().size(), 2u);

    // .xopp: upstream's fill="128"; the other color in an attribute of ours (upstream fills with the line's color)
    const fs::path xopp = fs::path(tmp.filePath("fill.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(xopp).ok);
    const std::string xml = unzipped(xopp);
    EXPECT_NE(xml.find("fill=\"128\""), std::string::npos);
    EXPECT_NE(xml.find("xqt-fill-color=\"#ff0000ff\""), std::string::npos);
    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document) << loaded.error;
    std::vector<const Stroke*> back;
    for (const Element* e: loaded.document->getPage(0)->getSelectedLayer()->getElementsView()) {
        back.push_back(dynamic_cast<const Stroke*>(e));
    }
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[0]->getFill(), 128);
    EXPECT_FALSE(back[0]->getFillColor());
    EXPECT_EQ(back[1]->getFill(), 128);
    ASSERT_TRUE(back[1]->getFillColor());
    EXPECT_EQ(*back[1]->getFillColor(), Colors::red);

    // The clipboard (serialized strokes): the color comes along; a stroke without one is written as upstream writes it
    for (const Stroke* s: strokes()) {
        ObjectOutputStream out(new BinObjectEncoding());
        s->serialize(out);
        ObjectInputStream in;
        GString* data = out.stealData();
        ASSERT_TRUE(in.read(data->str, data->len));
        g_string_free(data, true);
        Stroke copy;
        copy.readSerialized(in);
        EXPECT_EQ(copy.getFill(), 128);
        EXPECT_EQ(copy.getFillColor(), s->getFillColor());
        EXPECT_EQ(copy.getPointCount(), s->getPointCount());
    }

    // The PDF export draws the fillings
    const fs::path pdf = fs::path(tmp.filePath("fill.pdf").toStdString());
    ExportHelper::exportPdf(session->getDocument(), pdf, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    EXPECT_TRUE(grayish(pdfAt(pdf, QPointF(175, 150)))) << pdfAt(pdf, QPointF(175, 150)).name().toStdString();
    EXPECT_TRUE(reddish(pdfAt(pdf, QPointF(375, 150)))) << pdfAt(pdf, QPointF(375, 150)).name().toStdString();
    penfill::setColor(*app->getSettings(), TOOL_PEN, std::nullopt);
}

// The laser pointer (upstream's laser pen and highlighter): ink over the page that fades out a while after the pen is
// lifted (upstream's setting, laserPointerFadeOutTime); never in the document, the undo stack, the file or the
// pictures of the page (thumbnails, previews, exports draw the document).
TEST_F(PenStylesTest, theLaserPointersInkFadesAndIsNeverInTheDocument) {
    app->getSettings()->setLaserPointerFadeOutTime(400);
    drawLine(QPointF(100, 200), QPointF(400, 200));  // a stroke of the pen, to compare
    ASSERT_EQ(strokes().size(), 1u);
    session->getUndoRedoHandler()->clearContents();
    const fs::path xopp = fs::path(tmp.filePath("laser.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(xopp).ok);
    ASSERT_FALSE(session->isModified());

    tools()->selectTool(TOOL_LASER_POINTER_PEN);
    drawLine(QPointF(100, 300), QPointF(400, 300));
    CanvasPage* page = view->getPage(0);
    EXPECT_TRUE(page->hasLaserInk());
    EXPECT_TRUE(reddish(canvasAt(QPointF(250, 300)))) << "the laser's red ink on the canvas";
    EXPECT_EQ(strokes().size(), 1u) << "not in the document";
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo()) << "not on the undo stack";
    EXPECT_FALSE(session->isModified()) << "nothing to save";

    // The picture of the page as thumbnails, previews and exports draw it: without the laser's ink
    {
        const double scale = 2;
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, static_cast<int>(600 * scale),
                                                              static_cast<int>(850 * scale));
        cairo_t* cr = cairo_create(surface);
        cairo_scale(cr, scale, scale);
        DocumentView().drawPage(session->getDocument()->getPage(0), cr, false, xoj::view::BACKGROUND_SHOW_ALL);
        cairo_destroy(cr);
        cairo_surface_flush(surface);
        const auto pixel = [&](double x, double y) {
            const unsigned char* px = cairo_image_surface_get_data(surface) +
                                      static_cast<int>(y * scale) * cairo_image_surface_get_stride(surface) +
                                      4 * static_cast<int>(x * scale);
            return QColor(px[2], px[1], px[0]);
        };
        EXPECT_FALSE(reddish(pixel(250, 300))) << "the laser's ink in the page's picture";
        EXPECT_LT(pixel(250, 200).lightness(), 200) << "(the pen's stroke is there)";
        cairo_surface_destroy(surface);
    }

    // Another stroke while it is there: one handler, both fade together
    drawLine(QPointF(100, 350), QPointF(400, 350));
    EXPECT_TRUE(reddish(canvasAt(QPointF(250, 350))));
    // It fades: gone a while after the delay (upstream's steps: 50 ms each, about half a second)
    QElapsedTimer t;
    t.start();
    while (page->hasLaserInk() && t.elapsed() < 5000) {
        processEvents(50);
    }
    EXPECT_FALSE(page->hasLaserInk()) << "the ink did not fade";
    EXPECT_GE(t.elapsed(), 300) << "it faded before the delay set";
    EXPECT_FALSE(page->hasOverlays());
    EXPECT_FALSE(reddish(canvasAt(QPointF(250, 300))));
    EXPECT_FALSE(reddish(canvasAt(QPointF(250, 350))));

    // The laser highlighter as well
    tools()->selectTool(TOOL_LASER_POINTER_HIGHLIGHTER);
    drawLine(QPointF(100, 450), QPointF(400, 450));
    EXPECT_TRUE(page->hasLaserInk());
    EXPECT_EQ(strokes().size(), 1u);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
    tools()->selectTool(TOOL_PEN);  // (the ink still fades)
    t.restart();
    while (page->hasLaserInk() && t.elapsed() < 5000) {
        processEvents(50);
    }
    EXPECT_FALSE(page->hasLaserInk());
    EXPECT_FALSE(session->isModified());

    // The file has the pen's stroke only
    ASSERT_TRUE(session->saveAs(xopp).ok);
    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_EQ(loaded.document->getPage(0)->getSelectedLayer()->getElementsView().size(), 1u);
}
