/*
 * xournal-qt: new elements get the time they were made (qt/docs/timeline.md, "Creation times"): strokes when the pen
 * touches, shapes, texts when their box opens, images, pasted elements and stickers (new: the time they were pasted),
 * sticky notes; what changes an element (the eraser's pieces, moving, undo and redo) keeps its time.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/ElementTimes.h"
#include "session/StickyNote.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "StickyNotes.h"
#include "TextEditor.h"
#include "TextFlow.h"

using namespace xqt;

namespace {
constexpr int64_t T0 = 1791100800000;  // 2026-10-04 08:00:00 UTC

class ElementTimesCanvasTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        timeline::setClock([this] { return clock; });
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        input = std::make_unique<CanvasInput>(*view);
        tools()->selectTool(TOOL_PEN);
        tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
        timeline::setClock({});
    }
    ToolHandler* tools() const { return app->getToolHandler(); }
    void processEvents(int ms = 30) {
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
    /// A line with the pen; the clock goes on by `takes` ms while it is drawn (the time is the press's)
    void drawLine(QPointF from, QPointF to, int64_t takes = 400) {
        tablet(QEvent::TabletPress, viewPos(from), 0.6, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 20; ++i) {
            clock += takes / 20;
            tablet(QEvent::TabletMove, viewPos(from + (to - from) * (i / 20.0)), 0.6, Qt::NoButton, Qt::LeftButton);
        }
        tablet(QEvent::TabletRelease, viewPos(to), 0.0, Qt::LeftButton, Qt::NoButton);
        processEvents();
    }
    std::vector<const Element*> elements(size_t page = 0) const {
        std::vector<const Element*> out;
        for (const Element* e: session->getDocument()->getPage(page)->getSelectedLayer()->getElementsView()) {
            out.push_back(e);
        }
        return out;
    }
    std::vector<int64_t> times(size_t page = 0) const {
        std::vector<int64_t> out;
        for (const Element* e: elements(page)) {
            out.push_back(e->getCreated());
        }
        return out;
    }

    QTemporaryDir tmp;
    int64_t clock = T0;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"pen", 1, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    quint64 timestamp = 1000;
};

QByteArray png(int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0.8, 0.1, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    QByteArray bytes;
    cairo_surface_write_to_png_stream(
            s,
            [](void* closure, const unsigned char* data, unsigned int length) {
                static_cast<QByteArray*>(closure)->append(reinterpret_cast<const char*>(data), length);
                return CAIRO_STATUS_SUCCESS;
            },
            &bytes);
    cairo_surface_destroy(s);
    return bytes;
}
}  // namespace

// A stroke gets the time the pen touched (pen, highlighter, a shape); undo and redo keep it
TEST_F(ElementTimesCanvasTest, strokesGetTheTimeThePenTouched) {
    drawLine({100, 100}, {300, 100});
    clock = T0 + 5000;
    tools()->selectTool(TOOL_HIGHLIGHTER);
    drawLine({100, 200}, {300, 200});
    tools()->selectTool(TOOL_PEN);
    clock = T0 + 9000;
    tools()->setDrawingType(DRAWING_TYPE_RECTANGLE);
    drawLine({100, 300}, {300, 400});
    tools()->setDrawingType(DRAWING_TYPE_DEFAULT);
    EXPECT_EQ(times(), (std::vector<int64_t>{T0, T0 + 5000, T0 + 9000}));

    session->getUndoRedoHandler()->undo();
    session->getUndoRedoHandler()->undo();
    clock = T0 + 20000;
    session->getUndoRedoHandler()->redo();
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(times(), (std::vector<int64_t>{T0, T0 + 5000, T0 + 9000})) << "redo puts the same strokes back";
}

// The pieces the eraser leaves of a stroke keep its time
TEST_F(ElementTimesCanvasTest, erasedPiecesKeepTheTime) {
    drawLine({100, 100}, {400, 100});
    clock = T0 + 60000;
    tools()->selectTool(TOOL_ERASER);
    tools()->setEraserType(ERASER_TYPE_DEFAULT);
    drawLine({250, 60}, {250, 140});
    tools()->selectTool(TOOL_PEN);
    const auto t = times();
    ASSERT_EQ(t.size(), 2u) << "cut in two";
    EXPECT_EQ(t[0], T0);
    EXPECT_EQ(t[1], T0);
}

// Pasted elements are new: the time they were pasted; the copied ones keep theirs. Moving keeps it.
TEST_F(ElementTimesCanvasTest, pastedElementsAreNew) {
    drawLine({100, 100}, {300, 100});
    clock = T0 + 1000;
    drawLine({100, 140}, {300, 140});
    view->selectAllOnPage();
    ASSERT_TRUE(view->copySelection());
    view->clearSelection();
    clock = T0 + 70000;
    ASSERT_TRUE(view->pasteElements());
    view->clearSelection();
    EXPECT_EQ(times(), (std::vector<int64_t>{T0, T0 + 1000, T0 + 70000, T0 + 70000}));
}

// A text gets the time its box opened; an image the time it was inserted; a sticky note (its paper) when it was put
// on the page
TEST_F(ElementTimesCanvasTest, textsImagesAndNotes) {
    clock = T0 + 100;
    view->setMarkdownText(false, 10, false);
    view->startText(*view->getPage(0), 100, 100);
    ASSERT_NE(view->getTextEditor(), nullptr);
    clock = T0 + 4000;
    QKeyEvent k(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    bool finish = false;
    view->getTextEditor()->keyPressed(&k, finish);
    view->endTextEditing();
    tools()->selectTool(TOOL_PEN);
    clock = T0 + 8000;
    ASSERT_TRUE(view->insertImage(png(40, 20), std::nullopt, std::nullopt, nullptr));
    view->clearSelection();
    const auto t = times();
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[0], T0 + 100) << "when the box opened, not when it was written in";
    EXPECT_EQ(t[1], T0 + 8000);

    clock = T0 + 12000;
    ASSERT_TRUE(view->notes().insert(std::nullopt));
    Document* doc = session->getDocument();
    const PageRef page = doc->getPage(0);
    const Layer* note = nullptr;
    for (const Layer* l: page->getLayersView()) {
        if (sticky::lookOf(*l)) {
            note = l;
        }
    }
    ASSERT_TRUE(note);
    EXPECT_EQ(note->getElementsView().front()->getCreated(), T0 + 12000);
}

// The page's own text is laid out anew on every change: it keeps the time it was begun, also when written in again
// later
TEST_F(ElementTimesCanvasTest, thePagesTextKeepsTheTimeItWasBegun) {
    TextFlow::Style style;
    TextBlock b;
    b.kind = TextBlock::Kind::Paragraph;
    b.text = QStringLiteral("one");
    TextFlowSession flow(*session);
    flow.begin(0, style);
    clock = T0 + 300;
    flow.update({b});
    clock = T0 + 900;
    b.text = QStringLiteral("one two");
    flow.update({b});
    flow.finish();
    clock = T0 + 50000;
    flow.begin(0, style);
    b.text = QStringLiteral("one two three");
    flow.update({b});
    flow.finish();
    const Layer* layer = TextFlow::textLayer(session->getDocument()->getPage(0));
    ASSERT_TRUE(layer);
    ASSERT_GT(layer->getElementsView().size(), 0u);
    for (const Element* e: layer->getElementsView()) {
        EXPECT_EQ(e->getCreated(), T0 + 300);
    }
}
