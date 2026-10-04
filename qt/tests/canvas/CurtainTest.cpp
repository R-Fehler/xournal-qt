/*
 * xournal-qt: the curtain (CurtainLayer, qt/docs/curtain.md): a black sheet over part of the page, for teaching and
 * presenting. Put out over the lower half of the page in view; nothing is written, erased or followed on it (a tap
 * shows its handles); its handles size and turn it, a drag on it moves it, two fingers carry, turn and size it; it goes
 * along to the page the view is at. It is never part of the document.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTouchEvent>
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
#include "CurtainLayer.h"
#include "PenHover.h"
#include "ViewController.h"
#include "config-test.h"

using namespace xqt;

namespace {
class CurtainTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        app->getToolHandler()->selectTool(TOOL_PEN);
        PenHover::instance().reset();
        session = std::make_unique<DocumentSession>(*app);
        session->insertNewPage(1);  // two pages
        session->setCurrentPageNo(0);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(900, 1400));
        input = std::make_unique<CanvasInput>(*view);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
        session.reset();
        app.reset();
    }
    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    CurtainLayer& curtain() { return view->curtain(); }
    QPointF at(size_t page, QPointF onPage) const {
        return view->pageViewRect(page).topLeft() + onPage * view->getViewController().zoom();
    }
    void tablet(QEvent::Type type, QPointF pos, double pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->tabletEvent(&e, pos);
    }
    /// A pen stroke from one place of the view to another
    void penDrag(QPointF from, QPointF to, int steps = 20) {
        tablet(QEvent::TabletPress, from, 0.5, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= steps; ++i) {
            tablet(QEvent::TabletMove, from + (to - from) * (static_cast<double>(i) / steps), 0.5, Qt::NoButton,
                   Qt::LeftButton);
        }
        tablet(QEvent::TabletRelease, to, 0.0, Qt::LeftButton, Qt::NoButton);
        tablet(QEvent::TabletLeaveProximity, to, 0.0, Qt::NoButton, Qt::NoButton);
        input->proximityEvent(false);
    }
    void penTap(QPointF pos) {
        tablet(QEvent::TabletPress, pos, 0.5, Qt::LeftButton, Qt::LeftButton);
        tablet(QEvent::TabletRelease, pos, 0.0, Qt::LeftButton, Qt::NoButton);
        input->proximityEvent(false);
    }
    void touchN(QEvent::Type type, const std::vector<std::tuple<int, QEventPoint::State, QPointF>>& fingers) {
        QList<QEventPoint> points;
        for (const auto& [id, state, pos]: fingers) {
            points.append(QEventPoint(id, state, pos, pos));
        }
        QTouchEvent e(type, &touchscreen, Qt::NoModifier, points);
        input->touchEvent(&e, [](QPointF scene) { return scene; });
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
    double pageHeight(size_t page) const { return session->getDocument()->getPage(page)->getHeight(); }
    double pageWidth(size_t page) const { return session->getDocument()->getPage(page)->getWidth(); }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
    QPointingDevice pen{"test pen", 1201, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    QPointingDevice touchscreen{"test touchscreen", 1204, QInputDevice::DeviceType::TouchScreen,
                                QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0};
    ulong timestamp = 1000;
};
}  // namespace

// Put out, it covers the lower half of the part of the page in view (down to the bottom of the page and a little
// beyond its sides), upright, with its handles shown; the same again takes it away. It is not part of the document.
TEST_F(CurtainTest, coversTheLowerHalfOfThePageInView) {
    ASSERT_FALSE(curtain().active());
    curtain().toggle(CurtainLayer::Shape::Curtain);
    ASSERT_TRUE(curtain().visible());
    EXPECT_EQ(curtain().page(), view->getPage(0));
    EXPECT_TRUE(curtain().handlesShown()) << "put out, it shows that it can be moved and sized";
    EXPECT_DOUBLE_EQ(curtain().rotation(), 0);
    // The part of page 0 in view (the view is 1400 high, the page fits the width)
    const QRectF r = view->pageViewRect(0);
    const double zoom = view->getViewController().zoom();
    const double visibleBottom = std::min(r.bottom(), 1400.0);
    const double top = ((std::max(r.top(), 0.0) + visibleBottom) / 2 - r.top()) / zoom;
    const QPointF c = curtain().centre();
    const QSizeF s = curtain().size();
    EXPECT_NEAR(c.y() - s.height() / 2, top, 0.5) << "its top edge in the middle of the part in view";
    EXPECT_GT(c.y() + s.height() / 2, pageHeight(0)) << "down to (beyond) the bottom of the page";
    EXPECT_LT(c.x() - s.width() / 2, 0);
    EXPECT_GT(c.x() + s.width() / 2, pageWidth(0)) << "a little beyond its sides";

    EXPECT_TRUE(curtain().covers(at(0, QPointF(pageWidth(0) / 2, pageHeight(0) - 20))));
    EXPECT_FALSE(curtain().covers(at(0, QPointF(pageWidth(0) / 2, 20))));
    EXPECT_EQ(elements(), 0u) << "nothing in the document";

    curtain().toggle(CurtainLayer::Shape::Curtain);
    EXPECT_FALSE(curtain().active()) << "the same again takes it away";
    EXPECT_FALSE(curtain().covers(at(0, QPointF(pageWidth(0) / 2, pageHeight(0) - 20))));
}

// The pen on the black writes nothing (also with the eraser or a select tool in hand nothing happens there); a tap on
// it shows the handles. Beside it the pen writes as usual, and a press there hides the handles.
TEST_F(CurtainTest, thePenWritesNothingOnTheBlack) {
    curtain().show(CurtainLayer::Shape::Curtain);
    curtain().setHandlesShown(false);
    const QPointF centre = curtain().centre();
    const QPointF onBlack = at(0, centre);
    ASSERT_TRUE(curtain().covers(onBlack));
    penDrag(onBlack, onBlack + QPointF(120, 40));
    processEvents();
    EXPECT_EQ(elements(), 0u) << "no ink under the curtain";
    EXPECT_EQ(curtain().centre(), centre) << "with the handles hidden the pen does not move it";
    EXPECT_FALSE(curtain().handlesShown()) << "a stroke is no tap";

    penTap(onBlack);
    processEvents();
    EXPECT_TRUE(curtain().handlesShown()) << "a tap on the black shows the handles";
    EXPECT_EQ(elements(), 0u) << "and leaves no dot";

    // Beside it: written as usual, the handles go
    const QPointF above = at(0, QPointF(100, 100));
    ASSERT_FALSE(curtain().covers(above));
    penDrag(above, above + QPointF(150, 30));
    processEvents();
    EXPECT_EQ(elements(), 1u);
    EXPECT_FALSE(curtain().handlesShown());
}

// While the handles are shown, a drag on the black moves it; a corner or an edge sizes it (the opposite side stays);
// the knob above it turns it (straight within a few degrees of a right angle).
TEST_F(CurtainTest, itsHandlesMoveSizeAndTurnIt) {
    curtain().show(CurtainLayer::Shape::Curtain);
    const double zoom = view->getViewController().zoom();
    curtain().place(QPointF(300, 500), QSizeF(200, 100), 0);
    ASSERT_TRUE(curtain().handlesShown());

    // Moved
    penDrag(at(0, QPointF(300, 500)), at(0, QPointF(340, 560)));
    EXPECT_NEAR(curtain().centre().x(), 340, 0.5);
    EXPECT_NEAR(curtain().centre().y(), 560, 0.5);
    EXPECT_EQ(elements(), 0u);
    ASSERT_TRUE(curtain().handlesShown()) << "moving it keeps its handles";

    // The top edge down by 30 points: the bottom stays
    const double bottom = curtain().centre().y() + curtain().size().height() / 2;
    const QPointF topEdge = at(0, QPointF(340, 510));
    ASSERT_EQ(curtain().handleAt(topEdge, CurtainLayer::REACH), CurtainLayer::Handle::Top);
    penDrag(topEdge, topEdge + QPointF(0, 30 * zoom));
    EXPECT_NEAR(curtain().size().height(), 70, 0.5);
    EXPECT_NEAR(curtain().centre().y() + curtain().size().height() / 2, bottom, 0.5);
    EXPECT_NEAR(curtain().size().width(), 200, 0.5) << "an edge sizes one way only";

    // The bottom right corner: both ways, the top left corner stays
    const QPointF topLeft = curtain().centre() - QPointF(100, 35);
    const QPointF corner = at(0, curtain().centre() + QPointF(100, 35));
    ASSERT_EQ(curtain().handleAt(corner, CurtainLayer::REACH), CurtainLayer::Handle::BottomRight);
    penDrag(corner, corner + QPointF(50, 20) * zoom);
    EXPECT_NEAR(curtain().size().width(), 250, 0.5);
    EXPECT_NEAR(curtain().size().height(), 90, 0.5);
    EXPECT_NEAR((curtain().centre() - QPointF(125, 45) - topLeft).manhattanLength(), 0, 0.5);

    // Never smaller than its least size
    const QPointF right = at(0, curtain().centre() + QPointF(125, 0));
    penDrag(right, right - QPointF(1000, 0));
    EXPECT_NEAR(curtain().size().width(), CurtainLayer::MIN_SIDE, 0.5);

    // The knob, a quarter turn around its middle (88 degrees: straight at 90)
    const QPointF middle = at(0, curtain().centre());
    const auto handles = curtain().handles();
    QPointF knob;
    for (const auto& [h, pos]: handles) {
        if (h == CurtainLayer::Handle::Rotate) {
            knob = pos;
        }
    }
    ASSERT_EQ(curtain().handleAt(knob, CurtainLayer::REACH), CurtainLayer::Handle::Rotate);
    const double radius = std::hypot(knob.x() - middle.x(), knob.y() - middle.y());
    const double a = -M_PI / 2 + 88 * M_PI / 180;  // (the knob is straight above the middle)
    penDrag(knob, middle + QPointF(std::cos(a), std::sin(a)) * radius);
    EXPECT_NEAR(curtain().rotation(), M_PI / 2, 1e-9) << "a quarter turn, straight";
    EXPECT_EQ(elements(), 0u);
}

// One finger on the black with the handles hidden scrolls the page as usual; a tap shows them. Then two fingers on it
// carry it, turn it and size it, and the page neither scrolls nor zooms.
TEST_F(CurtainTest, fingersScrollOverItOrCarryItWithItsHandles) {
    curtain().show(CurtainLayer::Shape::Curtain);
    curtain().place(QPointF(300, 400), QSizeF(300, 200), 0);
    curtain().setHandlesShown(false);
    ViewController& vc = view->getViewController();
    const QPointF start = at(0, QPointF(300, 400));
    using S = QEventPoint::State;
    const double before = vc.visibleContentRect().top();
    touchN(QEvent::TouchBegin, {{1, S::Pressed, start}});
    for (int i = 1; i <= 10; ++i) {
        touchN(QEvent::TouchUpdate, {{1, S::Updated, start - QPointF(0, 10 * i)}});
    }
    touchN(QEvent::TouchEnd, {{1, S::Released, start - QPointF(0, 100)}});
    vc.stopMomentum();
    EXPECT_NEAR(vc.visibleContentRect().top() - before, 100, 1) << "the page scrolled";
    EXPECT_EQ(curtain().centre(), QPointF(300, 400)) << "the curtain lies on it, and went along";
    EXPECT_FALSE(curtain().handlesShown());

    const QPointF onBlack = at(0, QPointF(300, 400));
    touchN(QEvent::TouchBegin, {{1, S::Pressed, onBlack}});
    touchN(QEvent::TouchEnd, {{1, S::Released, onBlack}});
    EXPECT_TRUE(curtain().handlesShown()) << "a tap on the black: its handles";

    // Two fingers: carried by (40, 20), turned by 30 degrees and spread a quarter wider
    const double zoomBefore = vc.zoom();
    const QPointF topBefore = vc.visibleContentRect().topLeft();
    const QPointF a0 = onBlack - QPointF(60, 0), b0 = onBlack + QPointF(60, 0);
    touchN(QEvent::TouchBegin, {{1, S::Pressed, a0}});
    touchN(QEvent::TouchUpdate, {{1, S::Stationary, a0}, {2, S::Pressed, b0}});
    const double turn = M_PI / 6;
    for (int i = 1; i <= 12; ++i) {
        const double t = i / 12.0;
        const QPointF c = onBlack + QPointF(40, 20) * t;
        const double angle = turn * t, half = 60 * (1 + 0.25 * t);
        const QPointF d(std::cos(angle) * half, std::sin(angle) * half);
        touchN(QEvent::TouchUpdate, {{1, S::Updated, c - d}, {2, S::Updated, c + d}});
    }
    const QPointF c1 = onBlack + QPointF(40, 20);
    const QPointF d1(std::cos(turn) * 75, std::sin(turn) * 75);
    touchN(QEvent::TouchUpdate, {{1, S::Stationary, c1 - d1}, {2, S::Released, c1 + d1}});
    touchN(QEvent::TouchEnd, {{1, S::Released, c1 - d1}});
    EXPECT_DOUBLE_EQ(vc.zoom(), zoomBefore) << "the page did not zoom";
    EXPECT_EQ(vc.visibleContentRect().topLeft(), topBefore) << "nor scroll";
    const double zoom = vc.zoom();
    EXPECT_NEAR(curtain().centre().x(), 300 + 40 / zoom, 1);
    EXPECT_NEAR(curtain().centre().y(), 400 + 20 / zoom, 1);
    EXPECT_NEAR(curtain().rotation(), turn - CurtainLayer::TURN_SLOP, 0.01);
    EXPECT_NEAR(curtain().size().width(), 300 * 1.25 / (1 + CurtainLayer::SIZE_SLOP), 2);
    EXPECT_EQ(elements(), 0u);
}

// It lies on its page, and goes along to the page the view is at (the same place on it): flipping through a
// presentation keeps each page covered as far. When its page is deleted it goes onto the current one.
TEST_F(CurtainTest, itGoesAlongToTheCurrentPage) {
    curtain().show(CurtainLayer::Shape::Curtain);
    curtain().place(QPointF(200, 600), QSizeF(300, 200), 0.1);
    // (the view's current page is the page it shows most of: scrolled there)
    view->getViewController().scrollToPage(1);
    processEvents();
    ASSERT_EQ(view->currentPageNo(), 1u);
    EXPECT_EQ(curtain().page(), view->getPage(1));
    EXPECT_EQ(curtain().centre(), QPointF(200, 600));
    EXPECT_DOUBLE_EQ(curtain().rotation(), 0.1);
    EXPECT_TRUE(curtain().covers(at(1, QPointF(200, 600))));
    EXPECT_FALSE(curtain().covers(at(0, QPointF(200, 600))));

    view->getViewController().scrollToPage(0);
    processEvents();
    ASSERT_EQ(curtain().page(), view->getPage(0));
    ASSERT_TRUE(session->deletePages({0}));
    processEvents();
    ASSERT_TRUE(curtain().visible()) << "its page went: onto the one the view is at";
    EXPECT_EQ(curtain().page(), view->getPage(0));
    EXPECT_EQ(curtain().centre(), QPointF(200, 600));
}

// The spotlight: everything is black but its hole, in the middle of the view, with rounded corners; beyond the page
// as well (the space around the pages, the other pages). The pen writes in the hole and nowhere else; its handles are
// on the hole's edges. Put out, it takes the place of the curtain.
TEST_F(CurtainTest, theSpotlightShowsOnlyItsHole) {
    curtain().show(CurtainLayer::Shape::Curtain);
    curtain().toggle(CurtainLayer::Shape::Spotlight);
    ASSERT_EQ(curtain().shape(), CurtainLayer::Shape::Spotlight) << "instead of the curtain";
    ASSERT_TRUE(curtain().visible());
    const QPointF middle = at(0, curtain().centre());
    const QRectF r = view->pageViewRect(0);
    const QRectF inView = r.intersected(QRectF(0, 0, 900, 1400));
    EXPECT_NEAR(middle.x(), inView.center().x(), 1) << "in the middle of the part of the page in view";
    EXPECT_NEAR(middle.y(), inView.center().y(), 1);
    EXPECT_FALSE(curtain().covers(middle)) << "its hole shows the page";
    EXPECT_TRUE(curtain().covers(at(0, QPointF(10, 10)))) << "the rest of the page is black";
    EXPECT_TRUE(curtain().covers(QPointF(2, 2))) << "and the space around it";
    EXPECT_TRUE(curtain().covers(at(1, QPointF(100, 100)))) << "and the other pages";

    // The hole's corners are rounded: just inside its corner is black, just inside its edges is not
    curtain().place(QPointF(300, 400), QSizeF(200, 100), 0);
    const double r0 = curtain().cornerRadius();
    ASSERT_DOUBLE_EQ(r0, CurtainLayer::RADIUS);
    EXPECT_TRUE(curtain().covers(at(0, QPointF(201, 351)))) << "the corner is cut off";
    EXPECT_FALSE(curtain().covers(at(0, QPointF(201, 400)))) << "inside the left edge";
    EXPECT_FALSE(curtain().covers(at(0, QPointF(300, 351)))) << "inside the top edge";

    // The pen: in the hole it writes; on the black nothing; a tap on the black shows the handles
    curtain().setHandlesShown(false);
    penDrag(at(0, QPointF(250, 380)), at(0, QPointF(350, 420)));
    processEvents();
    EXPECT_EQ(elements(), 1u) << "written in the hole";
    penDrag(at(0, QPointF(100, 100)), at(0, QPointF(150, 150)));
    processEvents();
    EXPECT_EQ(elements(), 1u) << "nothing on the black";
    penTap(at(0, QPointF(100, 100)));
    EXPECT_TRUE(curtain().handlesShown());

    // Its handles are the hole's: the right edge wider, the left one stays
    const QPointF right = at(0, QPointF(400, 400));
    ASSERT_EQ(curtain().handleAt(right, CurtainLayer::REACH), CurtainLayer::Handle::Right);
    penDrag(right, right + QPointF(50, 0) * view->getViewController().zoom());
    EXPECT_NEAR(curtain().size().width(), 250, 0.5);
    EXPECT_NEAR(curtain().centre().x() - curtain().size().width() / 2, 200, 0.5);
    // A drag on the black moves it, the hole along
    penDrag(at(0, QPointF(100, 100)), at(0, QPointF(100, 160)));
    EXPECT_NEAR(curtain().centre().y(), 460, 0.5);
    EXPECT_EQ(elements(), 1u);
}
