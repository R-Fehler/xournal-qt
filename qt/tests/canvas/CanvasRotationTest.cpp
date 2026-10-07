/*
 * xournal-qt: the canvas turned (qt/docs/features/canvas-rotation.md): the upright view the layout is seen through, the
 * mapping between it and the screen, turning about an anchor, the steps of 90°, snapping and the turn of two fingers.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <memory>

#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"

#include "DocumentLayout.h"
#include "ViewController.h"

using namespace xqt;

namespace {
/// A document of A4 pages and its layout (one column)
struct Pages {
    explicit Pages(int count): doc(nullptr) {
        for (int i = 0; i < count; ++i) {
            auto p = std::make_shared<XojPage>(595.27559, 841.88976);
            doc.addPage(p);
            refs.push_back(p);
        }
        layout.update(doc, refs, {});
    }
    Document doc;
    std::vector<PageRef> refs;
    DocumentLayout layout;
};

/// Where a point of a page (points) is on the screen
QPointF onScreen(const ViewController& vc, const DocumentLayout& layout, size_t page, QPointF pt) {
    const QPointF content = layout.pageRect(page, vc.zoom()).topLeft() + pt * vc.zoom();
    return vc.viewToScreen(vc.contentToView(content));
}

/// The point of a page under a screen point (points)
QPointF onPage(const ViewController& vc, const DocumentLayout& layout, size_t page, QPointF screen) {
    const QPointF content = vc.viewToContent(vc.screenToView(screen));
    return (content - layout.pageRect(page, vc.zoom()).topLeft()) / vc.zoom();
}

/// Zoomed in on the middle of page 1 (nothing is clamped at the ends of the document)
void zoomIntoPageOne(ViewController& vc, const DocumentLayout& layout) {
    vc.setZoom(vc.zoom() * 3, QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
    const QRectF r = layout.pageRect(1, vc.zoom());
    vc.setScrollPosition(r.center() - QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2));
}

void expectNear(QPointF a, QPointF b, double tolerance, const char* what) {
    EXPECT_NEAR(a.x(), b.x(), tolerance) << what;
    EXPECT_NEAR(a.y(), b.y(), tolerance) << what;
}
}  // namespace

TEST(CanvasRotation, uprightViewIsTheScreenExactly) {
    Pages pages(3);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(800, 600));
    EXPECT_FALSE(vc.rotated());
    EXPECT_EQ(vc.viewSize(), QSizeF(800, 600));
    EXPECT_EQ(vc.screenSize(), QSizeF(800, 600));
    // No rounding at all: an upright canvas maps every point onto itself
    for (const QPointF p: {QPointF(0.1, 0.7), QPointF(799.3, 13.37), QPointF(-5, 1e5)}) {
        EXPECT_EQ(vc.screenToView(p), p);
        EXPECT_EQ(vc.viewToScreen(p), p);
    }
    EXPECT_EQ(vc.viewToScreen(QRectF(1.5, 2.5, 30, 40)), QRectF(1.5, 2.5, 30, 40));
}

TEST(CanvasRotation, theViewIsTheBoundingBoxOfTheTurnedScreen) {
    Pages pages(3);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(800, 600));
    vc.setRotation(90);
    EXPECT_EQ(vc.rotation(), 90);
    EXPECT_TRUE(vc.rightAngled());
    EXPECT_EQ(vc.viewSize(), QSizeF(600, 800)) << "a quarter turn: width and height swap, exactly";
    // Turned clockwise: the screen's top left shows the view's bottom left
    EXPECT_EQ(vc.screenToView(QPointF(0, 0)), QPointF(0, 800));
    EXPECT_EQ(vc.screenToView(QPointF(800, 600)), QPointF(600, 0));
    vc.setRotation(180);
    EXPECT_EQ(vc.viewSize(), QSizeF(800, 600));
    EXPECT_EQ(vc.screenToView(QPointF(0, 0)), QPointF(800, 600));
    vc.setRotation(-90);
    EXPECT_EQ(vc.rotation(), 270) << "normalised to [0, 360)";
    EXPECT_EQ(vc.screenToView(QPointF(0, 0)), QPointF(600, 0));
    vc.setRotation(45);
    EXPECT_FALSE(vc.rightAngled());
    const double side = (800 + 600) / std::sqrt(2.0);
    EXPECT_NEAR(vc.viewSize().width(), side, 1e-9);
    EXPECT_NEAR(vc.viewSize().height(), side, 1e-9);
    // Every corner of the screen lies within the view
    for (const QPointF c: vc.screenInView()) {
        EXPECT_GE(c.x(), -1e-9);
        EXPECT_GE(c.y(), -1e-9);
        EXPECT_LE(c.x(), side + 1e-9);
        EXPECT_LE(c.y(), side + 1e-9);
    }
}

TEST(CanvasRotation, screenAndViewRoundTrip) {
    Pages pages(3);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(1024, 700));
    for (const double angle: {0.0, 37.0, 90.0, 133.5, 180.0, 270.0, 359.0}) {
        vc.setRotation(angle);
        for (const QPointF p: {QPointF(0, 0), QPointF(512, 350), QPointF(1023.5, 3.25), QPointF(-40, 900)}) {
            expectNear(vc.viewToScreen(vc.screenToView(p)), p, 1e-9, "screen -> view -> screen");
            expectNear(vc.screenToView(vc.viewToScreen(p)), p, 1e-9, "view -> screen -> view");
        }
        // Deltas turn only: a delta is the difference of two points
        const QPointF a(100, 200), b(130, 160);
        expectNear(vc.screenDeltaToView(b - a), vc.screenToView(b) - vc.screenToView(a), 1e-9, "delta");
        expectNear(vc.viewDeltaToScreen(vc.screenDeltaToView(b - a)), b - a, 1e-9, "delta back");
        // The middle of the screen is the middle of the view
        expectNear(vc.screenToView(QPointF(512, 350)),
                   QPointF(vc.viewSize().width() / 2, vc.viewSize().height() / 2), 1e-9, "middle");
    }
    vc.setRotation(90);
    EXPECT_EQ(vc.screenDeltaToView(QPointF(0, 10)), QPointF(10, 0)) << "down on the screen is to the right in the view";
}

TEST(CanvasRotation, turningKeepsTheAnchorWhereItIsOnTheScreen) {
    Pages pages(4);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(800, 600));
    zoomIntoPageOne(vc, pages.layout);  // (zoomed in: the content is larger than the view both ways)
    const QPointF middle(400, 300);
    const QPointF underMiddle = onPage(vc, pages.layout, 1, middle);
    vc.setRotation(30);
    expectNear(onScreen(vc, pages.layout, 1, underMiddle), middle, 1e-6, "the middle stays");
    // About another point (a gesture's, a tap's)
    const QPointF at(150, 420);
    const QPointF underAt = onPage(vc, pages.layout, 1, at);
    vc.setRotation(120, at);
    expectNear(onScreen(vc, pages.layout, 1, underAt), at, 1e-6, "the anchor stays");
    vc.setRotation(0, at);
    expectNear(onScreen(vc, pages.layout, 1, underAt), at, 1e-6, "back upright, the anchor stays");
    EXPECT_EQ(vc.viewSize(), QSizeF(800, 600));
}

TEST(CanvasRotation, stepsOfAQuarterTurn) {
    Pages pages(2);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(800, 600));
    for (const double expected: {90.0, 180.0, 270.0, 0.0}) {
        vc.rotateBy(90);
        EXPECT_EQ(vc.rotation(), expected);
    }
    vc.rotateBy(-90);
    EXPECT_EQ(vc.rotation(), 270);
    vc.setRotation(37);
    vc.rotateBy(90);
    EXPECT_EQ(vc.rotation(), 90) << "from a free angle to the next quarter that way";
    vc.setRotation(37);
    vc.rotateBy(-90);
    EXPECT_EQ(vc.rotation(), 0);
}

TEST(CanvasRotation, snapsToRightAnglesWithinSixDegrees) {
    EXPECT_EQ(ViewController::snapAngle(87), 90);
    EXPECT_EQ(ViewController::snapAngle(96), 90);
    EXPECT_EQ(ViewController::snapAngle(83), 83);
    EXPECT_EQ(ViewController::snapAngle(357), 0);
    EXPECT_EQ(ViewController::snapAngle(-3), 0);
    EXPECT_EQ(ViewController::snapAngle(184.5), 180);
    EXPECT_EQ(ViewController::snapAngle(45), 45);
}

TEST(CanvasRotation, twoFingersTurnItOnlyAfterADeliberateTwist) {
    Pages pages(4);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(800, 600));
    zoomIntoPageOne(vc, pages.layout);
    const QPointF c(400, 300);
    const QPointF underFingers = onPage(vc, pages.layout, 1, c);
    vc.pinchBegin(c, 200, 10.0);
    vc.pinchUpdate(c, 200, 18.0);
    EXPECT_EQ(vc.rotation(), 0) << "a little twist while pinching does not turn it";
    EXPECT_FALSE(vc.twisting());
    vc.pinchUpdate(c, 200, 50.0);  // 40° in all: it turns, behind the fingers by the 12° it took to start
    EXPECT_TRUE(vc.twisting());
    EXPECT_NEAR(vc.rotation(), 28, 1e-9);
    vc.pinchUpdate(c, 200, 108.0);  // 98 - 12 = 86: snaps to 90
    EXPECT_EQ(vc.rotation(), 90);
    // The fingers move and spread while turning: the document point under them stays under them
    const QPointF c2(300, 250);
    vc.pinchUpdate(c2, 300, 70.0);  // 60 - 12 = 48
    EXPECT_NEAR(vc.rotation(), 48, 1e-9);
    expectNear(onScreen(vc, pages.layout, 1, underFingers), c2, 1e-6, "anchored under the fingers");
    vc.pinchEnd();
    EXPECT_FALSE(vc.twisting());
    // Across the wrap of the fingers' angle (from 170° to -170° is 20°, not -340°)
    vc.setRotation(0);
    vc.pinchBegin(c, 200, 170.0);
    vc.pinchUpdate(c, 200, -170.0);
    EXPECT_NEAR(vc.rotation(), 8, 1e-9);
    vc.pinchEnd();
    // Without the fingers' angle (rotation gestures off) a pinch never turns it
    vc.setRotation(0);
    vc.pinchBegin(c, 200);
    vc.pinchUpdate(c, 300);
    EXPECT_EQ(vc.rotation(), 0);
    vc.pinchEnd();
}

TEST(CanvasRotation, touchpadTwistAddsUp) {
    Pages pages(2);
    ViewController vc(&pages.layout);
    vc.setViewSize(QSizeF(800, 600));
    const QPointF at(200, 200);
    vc.twistBy(5, at);
    vc.twistBy(5, at);
    EXPECT_EQ(vc.rotation(), 0);
    vc.twistBy(10, at);  // 20 - 12
    EXPECT_NEAR(vc.rotation(), 8, 1e-9);
    vc.twistEnd();
    vc.twistBy(-30, at);  // a new gesture: -30 + 12 = -18 from 8
    EXPECT_NEAR(vc.rotation(), 350, 1e-9);
    vc.twistEnd();
}
