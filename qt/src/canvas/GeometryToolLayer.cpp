#include "GeometryToolLayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "model/Compass.h"
#include "model/Setsquare.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "view/CompassView.h"
#include "view/SetsquareView.h"

#include "CanvasPage.h"
#include "CanvasView.h"

namespace xqt {

namespace {
/// The point on the line through a and b that is nearest to p, but not beyond its ends.
QPointF onSegment(QPointF p, QPointF a, QPointF b) {
    const QPointF along = b - a;
    const double length = QPointF::dotProduct(along, along);
    if (length < 1e-9) {
        return a;
    }
    const double t = std::clamp(QPointF::dotProduct(p - a, along) / length, 0.0, 1.0);
    return a + along * t;
}

/// The point in the coordinates of the tool (centimetres, the middle of the tool is the origin).
QPointF toTool(const GeometryTool& tool, QPointF pagePoint) {
    cairo_matrix_t matrix = tool.getMatrix();
    if (cairo_matrix_invert(&matrix) != CAIRO_STATUS_SUCCESS) {
        return pagePoint;
    }
    double x = pagePoint.x();
    double y = pagePoint.y();
    cairo_matrix_transform_point(&matrix, &x, &y);
    return {x, y};
}

QPointF toPage(const GeometryTool& tool, QPointF toolPoint) {
    cairo_matrix_t matrix = tool.getMatrix();
    double x = toolPoint.x();
    double y = toolPoint.y();
    cairo_matrix_transform_point(&matrix, &x, &y);
    return {x, y};
}
}  // namespace

GeometryToolLayer::GeometryToolLayer(CanvasView& view): view(view) {}

GeometryToolLayer::~GeometryToolLayer() { remove(); }

std::optional<GeometryToolType> GeometryToolLayer::type() const {
    if (!tool) {
        return std::nullopt;
    }
    return dynamic_cast<Setsquare*>(tool.get()) ? GeometryToolType::SETSQUARE : GeometryToolType::COMPASS;
}

void GeometryToolLayer::toggle(GeometryToolType wanted) {
    if (type() == wanted) {
        hide();
        return;
    }
    remove();
    const size_t pageNo = view.getSession().getCurrentPageNo();
    CanvasPage* page = view.pageCount() > pageNo ? view.getPage(pageNo) : nullptr;
    if (!page) {
        return;
    }
    // In the middle of the page, upright
    double width = 0;
    double height = 0;
    if (const PageRef p = page->getPage()) {
        width = p->getWidth();
        height = p->getHeight();
    }
    if (wanted == GeometryToolType::SETSQUARE) {
        tool = std::make_unique<Setsquare>(Setsquare::INITIAL_HEIGHT, 0, width / 2, height / 2);
    } else {
        tool = std::make_unique<Compass>(Compass::INITIAL_HEIGHT, 0, width / 2, height / 2);
    }
    isMinimized = false;
    freeRotation = 0;
    place(*page);
}

void GeometryToolLayer::place(CanvasPage& page) {
    onPage = &page;
    if (auto* setsquare = dynamic_cast<Setsquare*>(tool.get())) {
        page.addOverlayView(std::make_unique<xoj::view::SetsquareView>(setsquare, &page,
                                                                      view.getSession().getZoomControl()));
    } else if (auto* compass = dynamic_cast<Compass*>(tool.get())) {
        page.addOverlayView(std::make_unique<xoj::view::CompassView>(compass, &page,
                                                                    view.getSession().getZoomControl()));
    }
    tool->notify(true);
    Q_EMIT view.updateRequested();
}

void GeometryToolLayer::remove() {
    if (onPage && tool) {
        onPage->removeOverlayViewsOf(tool.get());
    }
    onPage = nullptr;
    tool.reset();
    isMinimized = false;
}

void GeometryToolLayer::setMinimized(bool minimized) {
    if (!tool || minimized == isMinimized) {
        return;
    }
    if (minimized) {
        if (onPage) {
            onPage->removeOverlayViewsOf(tool.get());
        }
        onPage = nullptr;
        isMinimized = true;
        Q_EMIT view.updateRequested();
        return;
    }
    // Back on the page one is at now (the one it lay on may be far away), where it lay if that is on this page
    const size_t pageNo = view.getSession().getCurrentPageNo();
    CanvasPage* page = view.pageCount() > pageNo ? view.getPage(pageNo) : nullptr;
    if (!page) {
        return;
    }
    if (const PageRef p = page->getPage()) {
        const cairo_matrix_t m = tool->getMatrix();
        if (m.x0 < 0 || m.y0 < 0 || m.x0 > p->getWidth() || m.y0 > p->getHeight()) {
            tool->setOrigin({p->getWidth() / 2, p->getHeight() / 2});
        }
    }
    isMinimized = false;
    place(*page);
}

void GeometryToolLayer::setAngleSteps(bool on) {
    steps = on;
    if (!tool) {
        return;
    }
    freeRotation = tool->getRotation();
    if (steps) {
        // Straight onto the nearest step
        tool->setRotation(std::round(freeRotation / ANGLE_STEP) * ANGLE_STEP);
        tool->notify(true);
        Q_EMIT view.updateRequested();
    }
}

void GeometryToolLayer::hide() {
    if (tool) {
        remove();
        Q_EMIT view.updateRequested();
    }
}

bool GeometryToolLayer::contains(QPointF pagePoint) const {
    if (!tool) {
        return false;
    }
    // The tool measures in centimetres, and so does everything below
    const QPointF onTool = toTool(*tool, pagePoint);
    const double height = tool->getHeight();
    if (type() == GeometryToolType::COMPASS) {
        return std::hypot(onTool.x(), onTool.y()) <= height;  // the disc
    }
    // The triangle of the setsquare: below the long edge, between its legs
    return onTool.y() >= -0.1 && onTool.y() <= height && std::abs(onTool.x()) <= height - onTool.y() + 0.1;
}

void GeometryToolLayer::moveBy(QPointF delta) {
    if (!tool) {
        return;
    }
    cairo_matrix_t matrix = tool->getMatrix();
    tool->setOrigin({matrix.x0 + delta.x(), matrix.y0 + delta.y()});
    tool->notify(false);
    Q_EMIT view.updateRequested();
}

void GeometryToolLayer::turnAndSize(double angle, double factor) {
    if (!tool) {
        return;
    }
    if (angle != 0) {
        freeRotation += angle;
        tool->setRotation(steps ? std::round(freeRotation / ANGLE_STEP) * ANGLE_STEP : freeRotation);
    }
    if (factor > 0 && std::abs(factor - 1) > 0.001) {
        tool->setHeight(std::clamp(tool->getHeight() * factor, 2.0, 30.0));
    }
    tool->notify(true);
    Q_EMIT view.updateRequested();
}

double GeometryToolLayer::rotation() const { return tool ? tool->getRotation() : 0; }

double GeometryToolLayer::height() const { return tool ? tool->getHeight() : 0; }

QPointF GeometryToolLayer::snap(QPointF pagePoint) const {
    if (!tool) {
        return pagePoint;
    }
    // Everything here is in the tool's own coordinates: centimetres, its middle at the origin.
    const QPointF onTool = toTool(*tool, pagePoint);
    const double height = tool->getHeight();
    if (type() == GeometryToolType::COMPASS) {
        // Its edge is the circle of its radius: on the disc and just around it, the line follows that circle
        const double distance = std::hypot(onTool.x(), onTool.y());
        if (distance > height + SNAP_CM || distance < 0.05) {
            return pagePoint;
        }
        return toPage(*tool, QPointF(onTool.x() * height / distance, onTool.y() * height / distance));
    }
    // The setsquare: the nearest of its three edges - the long one and the two legs. Drawing on the triangle
    // itself is what a real setsquare is for, so it counts as well, not only the strip around it.
    const QPointF corners[3] = {QPointF(-height, 0), QPointF(height, 0), QPointF(0, height)};
    QPointF nearest;
    double distance = std::numeric_limits<double>::max();
    for (int i = 0; i < 3; ++i) {
        const QPointF p = onSegment(onTool, corners[i], corners[(i + 1) % 3]);
        const double d = std::hypot(onTool.x() - p.x(), onTool.y() - p.y());
        if (d < distance) {
            distance = d;
            nearest = p;
        }
    }
    const bool inside = onTool.y() >= 0 && onTool.y() <= height && std::abs(onTool.x()) <= height - onTool.y();
    if (!inside && distance > SNAP_CM) {
        return pagePoint;  // far away from it: a free line
    }
    return toPage(*tool, nearest);
}

}  // namespace xqt
