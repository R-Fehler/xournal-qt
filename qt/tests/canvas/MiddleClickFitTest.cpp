/*
 * xournal-qt: a click of the mouse's middle button fits the page, as two taps of the finger do
 * (CanvasView::doubleTapAt): zoomed in, the whole page again (presenting: filling the screen again); at the whole page,
 * the text column under it or the width. A middle drag is still the button's tool (the hand: it pans), a middle press
 * held long is no click, and the pen's barrel button (reported as the middle button) never fits. It works in a second
 * view shown for reading only (the reference) and while presenting.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasInput.h"
#include "CanvasView.h"
#include "ViewController.h"
#include "config-test.h"

using namespace xqt;

namespace {
class MiddleClickFit: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        app->getToolHandler()->selectTool(TOOL_PEN);  // (the middle button lends the hand)
        session = std::make_unique<DocumentSession>(*app);
        for (size_t i = 1; i < 3; ++i) {
            session->insertNewPage(i);
        }
        session->setCurrentPageNo(0);
        makeView();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }
    void makeView() {
        input.reset();
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(800, 1000));
        input = std::make_unique<CanvasInput>(*view);
        processEvents();
    }
    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    ViewController& vc(CanvasView* v = nullptr) { return (v ? *v : *view).getViewController(); }
    QPointF at(size_t page, QPointF onPage, CanvasView* v = nullptr) const {
        CanvasView& cv = v ? *v : *view;
        return cv.pageViewRect(page).topLeft() + onPage * cv.getViewController().zoom();
    }
    /// Zoomed in 2.5 times around the middle of the view: the page is much wider than the view
    void zoomIn(CanvasView* v = nullptr) {
        auto& c = vc(v);
        c.setZoom(c.zoom() * 2.5, QPointF(400, 500));
        processEvents();
    }
    bool wholePageShown(size_t page, CanvasView* v = nullptr) const {
        CanvasView& cv = v ? *v : *view;
        return cv.pageViewRect(page).width() <= cv.getViewController().viewSize().width() * 1.05;
    }
    void mouse(QEvent::Type type, QPointF pos, Qt::MouseButtons buttons, const QPointingDevice* device = nullptr,
               CanvasInput* in = nullptr) {
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::MiddleButton;
        QMouseEvent e(type, pos, pos, button, buttons, Qt::NoModifier, device ? device : &mousePointer);
        e.setTimestamp(timestamp);
        timestamp += 8;
        (in ? *in : *input).mouseEvent(&e, pos);
    }
    void middleClick(QPointF pos, CanvasInput* in = nullptr) {
        mouse(QEvent::MouseButtonPress, pos, Qt::MiddleButton, nullptr, in);
        mouse(QEvent::MouseMove, pos + QPointF(2, 1), Qt::MiddleButton, nullptr, in);  // (a hand is never quite still)
        mouse(QEvent::MouseButtonRelease, pos + QPointF(2, 1), Qt::NoButton, nullptr, in);
        processEvents();
    }
    size_t elements() const {
        size_t n = 0;
        Document* doc = session->getDocument();
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            for (const Layer* l: doc->getPage(i)->getLayersView()) {
                n += l->getElementsView().size();
            }
        }
        return n;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice mousePointer{"test mouse", 1105, QInputDevice::DeviceType::Mouse,
                                 QPointingDevice::PointerType::Generic, QInputDevice::Capability::Position, 3, 3};
    QPointingDevice pen{"test pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 3, 3};
    ulong timestamp = 1000;
};
}  // namespace

TEST_F(MiddleClickFit, aMiddleClickOnAZoomedInPageFitsThePageAndDrawsNothing) {
    zoomIn();
    ASSERT_FALSE(wholePageShown(0));
    middleClick(QPointF(400, 500));
    EXPECT_TRUE(wholePageShown(0)) << "the whole page again";
    EXPECT_LE(view->pageViewRect(0).height(), 1000.0);
    EXPECT_EQ(elements(), 0u) << "no dot";
    EXPECT_EQ(app->getToolHandler()->getToolType(), TOOL_PEN) << "the pen is still in hand";

    // The same with the hand, and with the pen in hand the left button still draws afterwards
    zoomIn();
    middleClick(QPointF(400, 500));
    EXPECT_TRUE(wholePageShown(0));
    QMouseEvent press(QEvent::MouseButtonPress, at(0, {200, 200}), at(0, {200, 200}), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier, &mousePointer);
    input->mouseEvent(&press, press.position());
    QMouseEvent release(QEvent::MouseButtonRelease, at(0, {200, 200}), at(0, {200, 200}), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier, &mousePointer);
    input->mouseEvent(&release, release.position());
    processEvents();
    EXPECT_EQ(elements(), 1u) << "a left click still draws its dot";
}

TEST_F(MiddleClickFit, atTheWholePageAMiddleClickFitsTheWidth) {
    vc().fitPage(0, true);
    processEvents();
    const double whole = vc().zoom();
    middleClick(at(0, QPointF(300, 400)));
    EXPECT_GT(vc().zoom(), whole * 1.05) << "zoomed in";
    EXPECT_NEAR(vc().zoom(), vc().fitWidthZoom(0), 1e-6) << "to the width of the page (no text column on it)";
}

TEST_F(MiddleClickFit, atTheWholePageAMiddleClickOnATextColumnZoomsToIt) {
    const std::string pdf = tmp.filePath("columns.pdf").toStdString();
    {
        cairo_surface_t* surface = cairo_pdf_surface_create(pdf.c_str(), 600, 800);
        cairo_t* cr = cairo_create(surface);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);
        for (int line = 0; line < 30; ++line) {
            cairo_move_to(cr, 60, 80 + line * 20);
            cairo_show_text(cr, "the left column of the page");
            cairo_move_to(cr, 330, 80 + line * 20);
            cairo_show_text(cr, "the right column of it");
        }
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    input.reset();
    view.reset();
    auto loaded = DocumentSession::loadFile(fs::path(pdf));
    ASSERT_TRUE(loaded.document) << loaded.error;
    session = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
    makeView();
    vc().fitPage(0, true);
    processEvents();
    const double whole = vc().zoom();
    middleClick(at(0, QPointF(420, 300)));
    EXPECT_GT(vc().zoom(), vc().fitWidthZoom(0) * 1.3) << "the column fills the view: more than the page width";
    EXPECT_GT(vc().zoom(), whole * 1.3);
}

TEST_F(MiddleClickFit, aMiddleDragPansAndDoesNotFit) {
    zoomIn();
    const double zoom = vc().zoom();
    const QPointF scroll = vc().scrollPosition();
    const QPointF from(400, 500);
    mouse(QEvent::MouseButtonPress, from, Qt::MiddleButton);
    for (int i = 1; i <= 10; ++i) {
        mouse(QEvent::MouseMove, from + QPointF(-8 * i, -6 * i), Qt::MiddleButton);
    }
    mouse(QEvent::MouseButtonRelease, from + QPointF(-80, -60), Qt::NoButton);
    processEvents();
    EXPECT_DOUBLE_EQ(vc().zoom(), zoom) << "no fit";
    EXPECT_NEAR(vc().scrollPosition().x() - scroll.x(), 80, 1.0) << "the page followed the mouse all the way";
    EXPECT_NEAR(vc().scrollPosition().y() - scroll.y(), 60, 1.0);
    EXPECT_EQ(elements(), 0u) << "the hand draws nothing";
    EXPECT_EQ(app->getToolHandler()->getToolType(), TOOL_PEN) << "the pen is back in hand";
}

TEST_F(MiddleClickFit, aMiddlePressHeldLongIsNoClick) {
    zoomIn();
    const double zoom = vc().zoom();
    mouse(QEvent::MouseButtonPress, QPointF(400, 500), Qt::MiddleButton);
    timestamp += 700;
    mouse(QEvent::MouseButtonRelease, QPointF(400, 500), Qt::NoButton);
    processEvents();
    EXPECT_DOUBLE_EQ(vc().zoom(), zoom);
    EXPECT_EQ(elements(), 0u);
}

TEST_F(MiddleClickFit, thePensMiddleButtonDoesNotFit) {
    zoomIn();
    const double zoom = vc().zoom();
    const QPointF pos(400, 500);
    // The barrel button pressed and let go while the pen hovers (tablet events)
    input->proximityEvent(true);
    for (auto [type, buttons]: {std::pair{QEvent::TabletPress, Qt::MouseButtons(Qt::MiddleButton)},
                                std::pair{QEvent::TabletRelease, Qt::MouseButtons(Qt::NoButton)}}) {
        QTabletEvent e(type, &pen, pos, pos, 0.0, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, Qt::MiddleButton, buttons);
        e.setTimestamp(timestamp += 8);
        input->tabletEvent(&e, pos);
    }
    processEvents();
    EXPECT_DOUBLE_EQ(vc().zoom(), zoom) << "tablet events";

    // The same as a mouse event of the mouse while the pen is near (a driver that turns the barrel button into the
    // middle button of the pointer)
    middleClick(pos);
    EXPECT_DOUBLE_EQ(vc().zoom(), zoom) << "the pen is near: its barrel button";
    input->proximityEvent(false);

    // A mouse event made from the pen
    mouse(QEvent::MouseButtonPress, pos, Qt::MiddleButton, &pen);
    mouse(QEvent::MouseButtonRelease, pos, Qt::NoButton, &pen);
    processEvents();
    EXPECT_DOUBLE_EQ(vc().zoom(), zoom) << "mouse events made from the pen";
    EXPECT_EQ(elements(), 0u);

    // The mouse, the pen away: it fits
    middleClick(pos);
    EXPECT_TRUE(wholePageShown(0));
}

TEST_F(MiddleClickFit, worksInTheReferenceViewAndWhilePresenting) {
    // A second view of the document shown for reading only (the reference beside it)
    auto second = std::make_unique<CanvasView>(*session);
    second->setReadingOnly(true);
    second->getViewController().setViewSize(QSizeF(600, 800));
    CanvasInput secondInput(*second);
    processEvents();
    second->getViewController().setZoom(second->getViewController().zoom() * 3, QPointF(300, 400));
    processEvents();
    ASSERT_FALSE(wholePageShown(0, second.get()));
    const double primaryZoom = vc().zoom();
    middleClick(QPointF(300, 400), &secondInput);
    EXPECT_TRUE(wholePageShown(0, second.get()) || wholePageShown(1, second.get()));
    EXPECT_DOUBLE_EQ(vc().zoom(), primaryZoom) << "the other view stays as it was";

    // Presenting: zoomed in, a middle click fills the screen with the page again
    view->setPresenting(true);
    processEvents();
    const double presented = vc().zoom();
    zoomIn();
    ASSERT_GT(vc().zoom(), presented * 2);
    middleClick(QPointF(400, 500));
    EXPECT_NEAR(vc().zoom(), presented, 1e-6);
    EXPECT_EQ(vc().keptFit(), ViewController::Fit::Page);
    EXPECT_EQ(elements(), 0u);
}
