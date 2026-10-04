#include "CurtainLayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QRectF>

#include "model/XojPage.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "ViewController.h"

namespace xqt {

namespace {
/// Turned by `angle` (radians; clockwise on the screen, whose y goes down)
QPointF turned(QPointF p, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return {p.x() * c - p.y() * s, p.x() * s + p.y() * c};
}

/// Beyond the page at its sides and its bottom (points): the edges of the page do not peek out
constexpr double MARGIN = 14;
/// Never bigger than this (points a side)
constexpr double MAX_SIDE = 20000;
}  // namespace

CurtainLayer::CurtainLayer(CanvasView& view): view(view) {}

CurtainLayer::~CurtainLayer() = default;

void CurtainLayer::toggle(Shape wanted) {
    if (shown == wanted) {
        hide();
        return;
    }
    show(wanted);
}

void CurtainLayer::show(Shape wanted) {
    CanvasPage* page = currentPage();
    if (!page) {
        return;
    }
    shown = wanted;
    placeDefault(*page);
    withHandles = true;  // (put out: it shows that it can be moved and sized)
    drag = {};
    inGesture = false;
    put(*page);
    stateChanged();
}

void CurtainLayer::hide() {
    if (!shown) {
        return;
    }
    shown.reset();
    onPage = nullptr;
    onDocumentPage.reset();
    withHandles = false;
    drag = {};
    inGesture = false;
    stateChanged();
}

void CurtainLayer::place(QPointF centre, QSizeF size, double rotation) {
    middle = centre;
    extent = QSizeF(std::clamp(size.width(), MIN_SIDE, MAX_SIDE), std::clamp(size.height(), MIN_SIDE, MAX_SIDE));
    turn = rotation;
    changed();
}

void CurtainLayer::setHandlesShown(bool on) {
    if (on == withHandles || (on && !visible())) {
        return;
    }
    withHandles = on;
    if (!on) {
        drag = {};
    }
    stateChanged();
}

CanvasPage* CurtainLayer::currentPage() const {
    const size_t pageNo = view.currentPageNo();
    return pageNo < view.pageCount() ? view.getPage(pageNo) : nullptr;
}

double CurtainLayer::zoom() const { return view.getViewController().zoom(); }

QPointF CurtainLayer::pageOrigin() const { return onPage ? onPage->viewRect().topLeft() : QPointF(); }

void CurtainLayer::placeDefault(CanvasPage& page) {
    double width = 0;
    double height = 0;
    if (const PageRef p = page.getPage()) {
        width = p->getWidth();
        height = p->getHeight();
    }
    // The part of the page in view, in its coordinates (all of it when none is)
    const QRectF r = page.viewRect();
    const double z = zoom();
    QRectF inView = r.intersected(QRectF(QPointF(0, 0), view.getViewController().viewSize()));
    QRectF part(0, 0, width, height);
    if (!inView.isEmpty() && z > 0) {
        part = QRectF((inView.topLeft() - r.topLeft()) / z, inView.size() / z);
    }
    // Over its lower half, down to the bottom of the page and a little beyond its sides
    const double top = part.center().y();
    const QRectF sheet(QPointF(-MARGIN, top), QPointF(width + MARGIN, std::max(top + MIN_SIDE, height + MARGIN)));
    middle = sheet.center();
    extent = sheet.size();
    turn = 0;
}

void CurtainLayer::put(CanvasPage& page) {
    onPage = &page;
    onDocumentPage = page.getPage();
    changed();
}

void CurtainLayer::changed() { Q_EMIT view.updateRequested(); }

void CurtainLayer::stateChanged() {
    Q_EMIT view.updateRequested();
    Q_EMIT view.curtainChanged();
}

QPointF CurtainLayer::toView(QPointF own) const { return pageOrigin() + (middle + turned(own, turn)) * zoom(); }

QPointF CurtainLayer::fromView(QPointF viewPos) const {
    const double z = zoom();
    if (z <= 0) {
        return {};
    }
    return turned((viewPos - pageOrigin()) / z - middle, -turn);
}

bool CurtainLayer::covers(QPointF viewPos) const {
    if (!visible()) {
        return false;
    }
    const QPointF own = fromView(viewPos);
    return std::abs(own.x()) <= extent.width() / 2 && std::abs(own.y()) <= extent.height() / 2;
}

std::vector<std::pair<CurtainLayer::Handle, QPointF>> CurtainLayer::handles() const {
    std::vector<std::pair<Handle, QPointF>> list;
    if (!handlesShown()) {
        return list;
    }
    const double w = extent.width() / 2, h = extent.height() / 2;
    // The knob above the middle of the top edge, the corners, the edges
    list.emplace_back(Handle::Rotate, toView(QPointF(0, -h)) + turned(QPointF(0, -KNOB_DISTANCE), turn));
    list.emplace_back(Handle::TopLeft, toView(QPointF(-w, -h)));
    list.emplace_back(Handle::TopRight, toView(QPointF(w, -h)));
    list.emplace_back(Handle::BottomLeft, toView(QPointF(-w, h)));
    list.emplace_back(Handle::BottomRight, toView(QPointF(w, h)));
    list.emplace_back(Handle::Top, toView(QPointF(0, -h)));
    list.emplace_back(Handle::Bottom, toView(QPointF(0, h)));
    list.emplace_back(Handle::Left, toView(QPointF(-w, 0)));
    list.emplace_back(Handle::Right, toView(QPointF(w, 0)));
    return list;
}

CurtainLayer::Handle CurtainLayer::handleAt(QPointF viewPos, double reach) const {
    if (!visible()) {
        return Handle::None;
    }
    // The nearest handle within reach (the knob and the corners before the edges: on a small curtain they are close)
    Handle best = Handle::None;
    double nearest = reach;
    for (const auto& [handle, at]: handles()) {
        const double d = std::hypot(viewPos.x() - at.x(), viewPos.y() - at.y());
        if (d <= nearest) {
            nearest = d;
            best = handle;
        }
    }
    if (best != Handle::None) {
        return best;
    }
    return covers(viewPos) ? Handle::Body : Handle::None;
}

void CurtainLayer::beginDrag(Handle handle, QPointF viewPos) {
    if (!visible() || handle == Handle::None) {
        return;
    }
    const double z = zoom();
    drag.handle = handle;
    drag.from = (viewPos - pageOrigin()) / z;
    drag.middle = middle;
    drag.extent = extent;
    drag.turn = turn;
    const QPointF d = drag.from - middle;
    drag.angle = std::atan2(d.y(), d.x());
    inGesture = false;
}

void CurtainLayer::dragTo(QPointF viewPos) {
    if (!visible() || !dragging()) {
        return;
    }
    const double z = zoom();
    const QPointF p = (viewPos - pageOrigin()) / z;
    const Handle h = drag.handle;
    if (h == Handle::Body) {
        middle = drag.middle + (p - drag.from);
        changed();
        return;
    }
    if (h == Handle::Rotate) {
        const QPointF d = p - drag.middle;
        double t = drag.turn + std::remainder(std::atan2(d.y(), d.x()) - drag.angle, 2 * M_PI);
        const double straight = std::round(t / (M_PI / 2)) * (M_PI / 2);
        if (std::abs(t - straight) < STRAIGHT_SNAP) {
            t = straight;
        }
        turn = t;
        changed();
        return;
    }
    // An edge or a corner: the opposite ones stay where they are. Measured in its own coordinates as they were when
    // the drag began, by how far the pointer went (a handle taken a little beside its middle does not jump).
    const double w = drag.extent.width() / 2, hh = drag.extent.height() / 2;
    double left = -w, right = w, top = -hh, bottom = hh;
    const QPointF moved = turned(p - drag.from, -drag.turn);
    const bool l = h == Handle::Left || h == Handle::TopLeft || h == Handle::BottomLeft;
    const bool r = h == Handle::Right || h == Handle::TopRight || h == Handle::BottomRight;
    const bool t = h == Handle::Top || h == Handle::TopLeft || h == Handle::TopRight;
    const bool b = h == Handle::Bottom || h == Handle::BottomLeft || h == Handle::BottomRight;
    if (l) {
        left = std::clamp(-w + moved.x(), right - MAX_SIDE, right - MIN_SIDE);
    }
    if (r) {
        right = std::clamp(w + moved.x(), left + MIN_SIDE, left + MAX_SIDE);
    }
    if (t) {
        top = std::clamp(-hh + moved.y(), bottom - MAX_SIDE, bottom - MIN_SIDE);
    }
    if (b) {
        bottom = std::clamp(hh + moved.y(), top + MIN_SIDE, top + MAX_SIDE);
    }
    middle = drag.middle + turned(QPointF((left + right) / 2, (top + bottom) / 2), drag.turn);
    extent = QSizeF(right - left, bottom - top);
    changed();
}

void CurtainLayer::beginGesture(QPointF centre, double angle, double distance) {
    if (!visible()) {
        return;
    }
    drag = {};
    gesture.centre = (centre - pageOrigin()) / zoom();
    gesture.middle = middle;
    gesture.extent = extent;
    gesture.turn = turn;
    gesture.distance = std::max(1.0, distance);
    gesture.lastAngle = angle;
    gesture.twist = 0;
    inGesture = true;
}

void CurtainLayer::moveGesture(QPointF centre, double angle, double distance) {
    if (!visible() || !inGesture) {
        return;
    }
    // As GeometryToolLayer::moveGesture: the turn added up in small steps, turning and sizing only past the slop
    gesture.twist += std::remainder(angle - gesture.lastAngle, 2 * M_PI);
    gesture.lastAngle = angle;
    const double by =
            std::abs(gesture.twist) <= TURN_SLOP ? 0.0 : gesture.twist - std::copysign(TURN_SLOP, gesture.twist);
    const double spread = std::max(1.0, distance) / gesture.distance;
    double size = 1;
    if (spread > 1 + SIZE_SLOP) {
        size = spread / (1 + SIZE_SLOP);
    } else if (spread < 1 - SIZE_SLOP) {
        size = spread / (1 - SIZE_SLOP);
    }
    // Both sides by the same factor, within the limits of each
    const double smallest = std::min(gesture.extent.width(), gesture.extent.height());
    const double largest = std::max(gesture.extent.width(), gesture.extent.height());
    size = std::clamp(size, MIN_SIDE / smallest, MAX_SIDE / largest);
    const QPointF at = (centre - pageOrigin()) / zoom();
    middle = at + turned((gesture.middle - gesture.centre) * size, by);
    extent = gesture.extent * size;
    turn = gesture.turn + by;
    changed();
}

void CurtainLayer::pageGoing(const CanvasPage* page) {
    if (onPage == page) {
        onPage = nullptr;
        drag = {};
        inGesture = false;
    }
}

void CurtainLayer::allPagesGoing() {
    onPage = nullptr;
    drag = {};
    inGesture = false;
}

void CurtainLayer::pagesChanged() {
    if (!shown || onPage) {
        return;
    }
    if (const PageRef page = onDocumentPage.lock()) {
        if (CanvasPage* canvasPage = view.canvasPageOf(page.get())) {
            put(*canvasPage);  // the same page, shown anew
            return;
        }
    }
    // Its page is gone: onto the page the view is at, where it lay
    if (CanvasPage* page = currentPage()) {
        put(*page);
    } else {
        hide();
    }
}

void CurtainLayer::currentPageChanged() {
    if (!shown || dragging() || inGesture) {
        return;
    }
    CanvasPage* page = currentPage();
    if (page && page != onPage) {
        put(*page);
    }
}

}  // namespace xqt
