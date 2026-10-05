/*
 * xournal-qt: what the audience's screen of the presenter view shows while it follows the presenter's view
 * (qt/docs/presenter-view.md, "The audience follows the presenter's view").
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <utility>

#include <QRectF>
#include <QSizeF>

namespace xqt::presenter {

/// The part of a page the audience's screen shows while the presenter sees `seen` of it (page points), on a screen of
/// the shape `screen`: what the presenter sees of `frame` (the slide, or the whole page when the space for notes is
/// shown too), made wider (or taller) to the screen's shape around its middle, moved into `frame` and cut to it where
/// it is larger than it. It always contains `seen` ∩ `frame`: the audience never sees less than the presenter does of
/// the frame. Nothing of the frame seen: the whole frame.
inline QRectF audienceRegion(const QRectF& frame, const QRectF& seen, const QSizeF& screen) {
    const QRectF part = seen.intersected(frame);
    if (frame.isEmpty() || part.isEmpty()) {
        return frame;
    }
    QPointF centre = part.center();
    QSizeF size = part.size();
    if (screen.width() > 0 && screen.height() > 0) {
        const double aspect = screen.width() / screen.height();
        if (size.width() < size.height() * aspect) {
            size.setWidth(size.height() * aspect);
        } else {
            size.setHeight(size.width() / aspect);
        }
    }
    // (along one axis: moved into [lo, hi], or all of it when it is larger)
    auto into = [](double middle, double length, double lo, double hi) -> std::pair<double, double> {
        if (length >= hi - lo) {
            return {lo, hi - lo};
        }
        return {std::clamp(middle - length / 2, lo, hi - length), length};
    };
    const auto [x, w] = into(centre.x(), size.width(), frame.left(), frame.right());
    const auto [y, h] = into(centre.y(), size.height(), frame.top(), frame.bottom());
    return {x, y, w, h};
}

}  // namespace xqt::presenter
