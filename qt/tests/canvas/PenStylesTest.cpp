/*
 * xournal-qt: the pen's styles (qt/pen-styles): upstream's line styles drawn by the canvas, kept in .xopp and shown
 * the same in an export.
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
#include "undo/UndoRedoHandler.h"
#include "util/GzUtil.h"

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
