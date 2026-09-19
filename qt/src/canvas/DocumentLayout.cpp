#include "DocumentLayout.h"

#include <algorithm>
#include <shared_mutex>

#include "model/Document.h"
#include "model/XojPage.h"

namespace xqt {

void DocumentLayout::update(Document& doc) {
    std::shared_lock lock(doc);
    const size_t n = doc.getPageCount();
    sizes.resize(n);
    heightPrefix.resize(n + 1);
    heightPrefix[0] = 0;
    maxWidth = 0;
    for (size_t i = 0; i < n; ++i) {
        auto page = doc.getPage(i);
        sizes[i] = QSizeF(page->getWidth(), page->getHeight());
        heightPrefix[i + 1] = heightPrefix[i] + sizes[i].height();
        maxWidth = std::max(maxWidth, sizes[i].width());
    }
}

double DocumentLayout::pageTop(size_t page, double zoom) const {
    return PADDING + heightPrefix[page] * zoom + static_cast<double>(page) * PADDING_BETWEEN;
}

QRectF DocumentLayout::pageRect(size_t page, double zoom) const {
    const QSizeF s = sizes[page] * zoom;
    const double x = PADDING + (maxWidth * zoom - s.width()) / 2.0;
    return QRectF(QPointF(x, pageTop(page, zoom)), s);
}

QSizeF DocumentLayout::contentSize(double zoom) const {
    if (sizes.empty()) {
        return QSizeF(2 * PADDING, 2 * PADDING);
    }
    const double h = 2 * PADDING + heightPrefix.back() * zoom + static_cast<double>(sizes.size() - 1) * PADDING_BETWEEN;
    return QSizeF(2 * PADDING + maxWidth * zoom, h);
}

size_t DocumentLayout::nearestPage(QPointF content, double zoom) const {
    if (sizes.empty()) {
        return 0;
    }
    // First page whose bottom (+ half the padding) is below the point.
    size_t lo = 0, hi = sizes.size() - 1;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const double bottom = pageTop(mid, zoom) + sizes[mid].height() * zoom + PADDING_BETWEEN / 2.0;
        if (content.y() < bottom) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
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
    size_t first = nearestPage(content.topLeft(), zoom);
    size_t last = nearestPage(content.bottomLeft(), zoom);
    // nearestPage includes the padding: drop pages that do not really intersect
    if (!pageRect(first, zoom).intersects(content) && first < last) {
        ++first;
    }
    if (!pageRect(last, zoom).intersects(content) && last > first) {
        --last;
    }
    return {first, last};
}

}  // namespace xqt
