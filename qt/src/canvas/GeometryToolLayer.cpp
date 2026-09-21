#include "GeometryToolLayer.h"

#include <cmath>

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
        tool->setRotation(tool->getRotation() + angle);
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
    const QPointF onTool = toTool(*tool, pagePoint);
    if (type() == GeometryToolType::COMPASS) {
        // Around the middle: the circle through the point, so a curve of that radius comes out
        const double radius = tool->getHeight();
        const double distance = std::hypot(onTool.x(), onTool.y());
        if (std::abs(distance - radius) > SNAP_CM || distance < 0.05) {
            return pagePoint;
        }
        return toPage(*tool, QPointF(onTool.x() * radius / distance, onTool.y() * radius / distance));
    }
    // The setsquare: along its long edge (y = 0 in its own coordinates)
    if (std::abs(onTool.y()) > SNAP_CM) {
        return pagePoint;
    }
    return toPage(*tool, QPointF(onTool.x(), 0));
}

}  // namespace xqt
