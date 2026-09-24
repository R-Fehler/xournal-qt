#include "DocumentLayout.h"

#include <algorithm>
#include <shared_mutex>

#include "model/Document.h"
#include "model/XojPage.h"

namespace xqt {

void DocumentLayout::update(Document& doc, const std::vector<PageRef>& pages, Config cfg) {
    config = cfg;
    const size_t n = pages.size();
    {
        std::shared_lock lock(doc);
        sizes.resize(n);
        for (size_t i = 0; i < n; ++i) {
            sizes[i] = QSizeF(pages[i]->getWidth(), pages[i]->getHeight());
        }
    }
    // Like LayoutMapper (horizontal layout, fixed columns): an even column count for pairs.
    cols = std::max<size_t>(1, config.columns);
    if (config.paired) {
        cols += cols % 2;
    }
    offset = config.paired ? config.pairsOffset % cols : 0;
    rowCount = n == 0 ? 0 : (n + offset + cols - 1) / cols;

    colWidth.assign(cols, 0);
    rowHeight.assign(rowCount, 0);
    for (size_t i = 0; i < n; ++i) {
        const Cell c = cellOf(i);
        colWidth[c.col] = std::max(colWidth[c.col], sizes[i].width());
        rowHeight[c.row] = std::max(rowHeight[c.row], sizes[i].height());
    }
    colPrefix.assign(cols + 1, 0);
    gapPrefix.assign(cols + 1, 0);
    for (size_t c = 0; c < cols; ++c) {
        colPrefix[c + 1] = colPrefix[c] + colWidth[c];
        gapPrefix[c + 1] = gapPrefix[c] + gapAfterColumn(c);
    }
    rowPrefix.assign(rowCount + 1, 0);
    for (size_t r = 0; r < rowCount; ++r) {
        rowPrefix[r + 1] = rowPrefix[r] + rowHeight[r];
    }
}

DocumentLayout::Cell DocumentLayout::cellOf(size_t page) const {
    const size_t slot = page + offset;
    return {slot % cols, slot / cols};
}

double DocumentLayout::gapAfterColumn(size_t col) const {
    return config.paired && col % 2 == 0 ? PAIR_GAP : PADDING_BETWEEN;
}

double DocumentLayout::colX(size_t col, double zoom) const { return PADDING + colPrefix[col] * zoom + gapPrefix[col]; }

double DocumentLayout::rowY(size_t row, double zoom) const {
    return PADDING + rowPrefix[row] * zoom + static_cast<double>(row) * PADDING_BETWEEN;
}

double DocumentLayout::offsetInColumn(size_t page) const {
    const Cell c = cellOf(page);
    const double free = colWidth[c.col] - sizes[page].width();
    // Pairs meet in the middle (left page to the right of its cell, right page to the left); else centered.
    return !config.paired ? free / 2.0 : (c.col % 2 == 0 ? free : 0.0);
}

QRectF DocumentLayout::pageRect(size_t page, double zoom) const {
    const Cell c = cellOf(page);
    const QSizeF s = sizes[page] * zoom;
    const double dx = offsetInColumn(page) * zoom;
    const double dy = (rowHeight[c.row] * zoom - s.height()) / 2.0;
    return QRectF(QPointF(colX(c.col, zoom) + dx, rowY(c.row, zoom) + dy), s);
}

std::pair<size_t, size_t> DocumentLayout::rowPages(size_t page) const {
    const size_t row = cellOf(page).row;
    const size_t n = sizes.size();
    const size_t first = std::clamp(row * cols, offset, offset + n - 1) - offset;
    const size_t last = std::clamp(row * cols + cols - 1, offset, offset + n - 1) - offset;
    return {first, last};
}

QRectF DocumentLayout::rowSpan(size_t page, double zoom) const {
    if (sizes.empty()) {
        return {};
    }
    const auto [first, last] = rowPages(std::min(page, sizes.size() - 1));
    QRectF span = pageRect(first, zoom);
    for (size_t i = first + 1; i <= last; ++i) {
        span = span.united(pageRect(i, zoom));
    }
    return span;
}

QSizeF DocumentLayout::contentSize(double zoom) const {
    if (sizes.empty()) {
        return QSizeF(2 * PADDING, 2 * PADDING);
    }
    const double w = 2 * PADDING + colPrefix[cols] * zoom + gapPrefix[cols - 1];
    const double h = 2 * PADDING + rowPrefix[rowCount] * zoom + static_cast<double>(rowCount - 1) * PADDING_BETWEEN;
    return QSizeF(w, h);
}

size_t DocumentLayout::rowAt(double y, double zoom) const {
    // First row whose bottom (+ half the padding) is below y.
    size_t lo = 0, hi = rowCount - 1;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (y < rowY(mid, zoom) + rowHeight[mid] * zoom + PADDING_BETWEEN / 2.0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

size_t DocumentLayout::colAt(double x, double zoom) const {
    for (size_t c = 0; c + 1 < cols; ++c) {
        if (x < colX(c, zoom) + colWidth[c] * zoom + gapAfterColumn(c) / 2.0) {
            return c;
        }
    }
    return cols - 1;
}

size_t DocumentLayout::nearestPage(QPointF content, double zoom) const {
    if (sizes.empty()) {
        return 0;
    }
    const size_t slot = rowAt(content.y(), zoom) * cols + colAt(content.x(), zoom);
    // Empty slots (before the first page, after the last one): the closest page.
    return std::clamp(slot, offset, offset + sizes.size() - 1) - offset;
}

std::optional<size_t> DocumentLayout::pageAt(QPointF content, double zoom) const {
    if (sizes.empty()) {
        return std::nullopt;
    }
    const size_t p = nearestPage(content, zoom);
    if (pageRect(p, zoom).contains(content)) {
        return p;
    }
    return std::nullopt;
}

std::pair<size_t, size_t> DocumentLayout::pagesIn(const QRectF& content, double zoom) const {
    if (sizes.empty()) {
        return {1, 0};  // empty range
    }
    size_t r0 = rowAt(content.top(), zoom);
    size_t r1 = rowAt(content.bottom(), zoom);
    // rowAt includes the padding: drop rows that do not really intersect
    if (r0 < r1 && rowY(r0, zoom) + rowHeight[r0] * zoom < content.top()) {
        ++r0;
    }
    if (r1 > r0 && rowY(r1, zoom) > content.bottom()) {
        --r1;
    }
    const size_t n = sizes.size();
    const size_t first = std::clamp(r0 * cols, offset, offset + n - 1) - offset;
    const size_t last = std::clamp((r1 + 1) * cols - 1, offset, offset + n - 1) - offset;
    return {first, last};
}

double DocumentLayout::fitWidthZoom(double viewWidth, size_t page) const {
    if (sizes.empty()) {
        return 0;
    }
    // The pages of the row, from the left edge of the first to the right edge of the last: their widths and the
    // room left in their columns grow with the zoom (points), the gaps between the columns do not (pixels).
    const auto [first, last] = rowPages(std::min(page, sizes.size() - 1));
    const size_t c0 = cellOf(first).col, c1 = cellOf(last).col;
    const double left = colPrefix[c0] + offsetInColumn(first);
    const double right = colPrefix[c1] + offsetInColumn(last) + sizes[last].width();
    const double width = right - left;
    if (width <= 0) {
        return 0;
    }
    // Upstream ZoomControl: viewport width / (page width + 20); for several columns the whole row with its gaps,
    // but never wider than the view.
    const double gaps = gapPrefix[c1] - gapPrefix[c0];
    const double upstream = (viewWidth - gaps) / (width + 20.0);
    const double exact = (viewWidth - 2 * PADDING - gaps - 4) / width;  // a little air: no scroll bar
    return cols == 1 ? upstream : std::min(upstream, exact);
}

}  // namespace xqt
