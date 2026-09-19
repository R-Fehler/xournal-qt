#include "InkModel.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double MIN_PRESSURE = 0.05;  // Xournal++ default "minimumPressure"

double effectiveWidth(const InkModel::Stroke& s, double pressure) {
    return s.width * std::max(MIN_PRESSURE, pressure);
}
}  // namespace

InkModel::InkModel(QObject* parent): QObject(parent) { setRenderScale(scale); }

InkModel::~InkModel() {
    if (surface) {
        cairo_surface_destroy(surface);
    }
}

void InkModel::setRenderScale(double pxPerPt) {
    // Keep the buffer to a sane size in the spike (a real implementation uses windowed buffers).
    constexpr double MAX_DIM = 6000.0;
    pxPerPt = std::clamp(pxPerPt, 0.25, MAX_DIM / PAGE_H);
    if (surface && std::abs(pxPerPt - scale) < 1e-6) {
        return;
    }
    scale = pxPerPt;
    if (surface) {
        cairo_surface_destroy(surface);
    }
    const QSize sz = bufferSize();
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sz.width(), sz.height());
    renderAll();
}

QSize InkModel::bufferSize() const {
    return {static_cast<int>(std::ceil(PAGE_W * scale)), static_cast<int>(std::ceil(PAGE_H * scale))};
}

QImage InkModel::bufferImage() const {
    cairo_surface_flush(surface);
    // CAIRO_FORMAT_ARGB32 has the same memory layout as QImage::Format_ARGB32_Premultiplied.
    return QImage(cairo_image_surface_get_data(surface), cairo_image_surface_get_width(surface),
                  cairo_image_surface_get_height(surface), cairo_image_surface_get_stride(surface),
                  QImage::Format_ARGB32_Premultiplied);
}

void InkModel::drawBackground(cairo_t* cr) const {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    // Ruled paper, like Xournal++'s "lined" background.
    cairo_set_line_width(cr, 0.5);
    cairo_set_source_rgb(cr, 0.63, 0.77, 0.93);
    for (double y = 80; y < PAGE_H - 20; y += 24) {
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, PAGE_W, y);
    }
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 1, 0.5, 0.5);
    cairo_move_to(cr, 72, 0);
    cairo_line_to(cr, 72, PAGE_H);
    cairo_stroke(cr);
}

void InkModel::drawSegment(cairo_t* cr, const Stroke& s, const Pt& a, const Pt& b) const {
    cairo_set_source_rgba(cr, s.color.redF(), s.color.greenF(), s.color.blueF(), s.color.alphaF());
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    // Like Xournal++'s StrokeViewHelper::drawWithPressure: each segment uses the width of its start point.
    cairo_set_line_width(cr, effectiveWidth(s, a.pressure));
    cairo_move_to(cr, a.x, a.y);
    cairo_line_to(cr, b.x, b.y);
    cairo_stroke(cr);
}

void InkModel::drawStroke(cairo_t* cr, const Stroke& s) const {
    if (s.pts.size() == 1) {
        drawSegment(cr, s, s.pts[0], s.pts[0]);
        return;
    }
    for (size_t i = 1; i < s.pts.size(); ++i) {
        drawSegment(cr, s, s.pts[i - 1], s.pts[i]);
    }
}

void InkModel::renderAll() {
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    drawBackground(cr);
    for (const auto& s: strokes) {
        drawStroke(cr, s);
    }
    if (current) {
        drawStroke(cr, *current);
    }
    cairo_destroy(cr);
    ++gen;
    Q_EMIT fullyDirty();
}

QRect InkModel::segmentBufferRect(const Stroke& s, const Pt& a, const Pt& b) const {
    const double half = effectiveWidth(s, std::max(a.pressure, b.pressure)) / 2.0 + 1.0;
    const double x0 = (std::min(a.x, b.x) - half) * scale;
    const double y0 = (std::min(a.y, b.y) - half) * scale;
    const double x1 = (std::max(a.x, b.x) + half) * scale;
    const double y1 = (std::max(a.y, b.y) + half) * scale;
    return QRect(QPoint(static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0))),
                 QPoint(static_cast<int>(std::ceil(x1)), static_cast<int>(std::ceil(y1))))
            .intersected(QRect(QPoint(0, 0), bufferSize()));
}

void InkModel::beginStroke(QPointF p, double pressure, QColor color, double width) {
    current = Stroke{{Pt{p.x(), p.y(), pressure}}, width, color};
    redoStack.clear();
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    drawSegment(cr, *current, current->pts[0], current->pts[0]);
    cairo_destroy(cr);
    Q_EMIT dirty(segmentBufferRect(*current, current->pts[0], current->pts[0]));
}

void InkModel::extendStroke(QPointF p, double pressure) {
    if (!current) {
        return;
    }
    const Pt prev = current->pts.back();
    // Xournal++ ignores motion below 0.3 page units (PIXEL_MOTION_THRESHOLD).
    if (std::hypot(p.x() - prev.x, p.y() - prev.y) < 0.3) {
        return;
    }
    const Pt next{p.x(), p.y(), pressure};
    current->pts.push_back(next);
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    drawSegment(cr, *current, prev, next);
    cairo_destroy(cr);
    Q_EMIT dirty(segmentBufferRect(*current, prev, next));
}

void InkModel::endStroke() {
    if (current) {
        strokes.push_back(std::move(*current));
        current.reset();
    }
}

bool InkModel::eraseAt(QPointF p, double radius) {
    const auto hit = [&](const Stroke& s) {
        return std::any_of(s.pts.begin(), s.pts.end(), [&](const Pt& q) {
            return std::hypot(q.x - p.x(), q.y - p.y()) <= radius + effectiveWidth(s, q.pressure) / 2;
        });
    };
    const auto before = strokes.size();
    strokes.erase(std::remove_if(strokes.begin(), strokes.end(), hit), strokes.end());
    if (strokes.size() != before) {
        renderAll();
        return true;
    }
    return false;
}

void InkModel::clear() {
    strokes.clear();
    redoStack.clear();
    current.reset();
    renderAll();
}

void InkModel::undo() {
    if (!strokes.empty()) {
        redoStack.push_back(std::move(strokes.back()));
        strokes.pop_back();
        renderAll();
    }
}

void InkModel::redo() {
    if (!redoStack.empty()) {
        strokes.push_back(std::move(redoStack.back()));
        redoStack.pop_back();
        renderAll();
    }
}
