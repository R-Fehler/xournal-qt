#include "ScrollLock.h"

#include <algorithm>

#include <QTimer>

#include "DocumentLayout.h"
#include "ViewController.h"

namespace xqt {

namespace {
QPointF middleOf(const ViewController& v) { return QPointF(v.viewSize().width() / 2, v.viewSize().height() / 2); }
}  // namespace

QPointF ScrollLock::anchorOf(const ViewController& v) {
    // (the start of the view where reading starts: two documents shown from their beginnings are on their first
    // pages, whatever the size of their pages)
    // A little in from the edge: a page jumped to has its top there, below the gap before it (fixed pixels)
    constexpr double IN = 2 * DocumentLayout::PADDING_BETWEEN;
    return v.horizontal() ? QPointF(std::min(IN, v.viewSize().width() / 2), v.viewSize().height() / 2)
                          : QPointF(v.viewSize().width() / 2, std::min(IN, v.viewSize().height() / 2));
}

ScrollLock::ScrollLock(QObject* parent): QObject(parent) {}

ViewController* ScrollLock::first() const { return a.data(); }
ViewController* ScrollLock::second() const { return b.data(); }

ScrollLock::~ScrollLock() {
    for (auto& c: connections) {
        disconnect(c);
    }
}

double ScrollLock::relativeZoom(const ViewController& v) {
    const auto place = v.placeAt(anchorOf(v));
    if (!place || v.viewSize().isEmpty()) {
        return 0;
    }
    const double fit = v.fitWidthZoom(place->page);
    return fit > 0 ? v.zoom() / fit : 0;
}

void ScrollLock::lock(ViewController* first, ViewController* second) {
    unlock();
    if (!first || !second || first == second) {
        return;
    }
    a = first;
    b = second;
    recapture();
    lastSizeA = a->viewSize();
    lastSizeB = b->viewSize();
    connections.push_back(connect(a, &ViewController::changed, this, [this] {
        if (a && b) {
            moved(*a, *b, true);
        }
    }));
    connections.push_back(connect(b, &ViewController::changed, this, [this] {
        if (a && b) {
            moved(*b, *a, false);
        }
    }));
    connections.push_back(connect(a, &ViewController::zoomChanged, this, [this] {
        if (a && b) {
            zoomed(*a, *b, true);
        }
    }));
    connections.push_back(connect(b, &ViewController::zoomChanged, this, [this] {
        if (a && b) {
            zoomed(*b, *a, false);
        }
    }));
    connections.push_back(connect(a, &QObject::destroyed, this, &ScrollLock::unlock));
    connections.push_back(connect(b, &QObject::destroyed, this, &ScrollLock::unlock));
    // b comes to a's place within the page
    if (!lastSizeA.isEmpty() && !lastSizeB.isEmpty()) {
        follow(*a, *b, offset);
    }
    Q_EMIT lockedChanged();
}

void ScrollLock::recapture() {
    if (!a || !b) {
        return;
    }
    const auto pa = a->placeAt(anchorOf(*a));
    const auto pb = b->placeAt(anchorOf(*b));
    offset = pa && pb ? static_cast<int>(pb->page) - static_cast<int>(pa->page) : 0;
    const double ra = relativeZoom(*a), rb = relativeZoom(*b);
    zoomRatio = ra > 0 && rb > 0 ? std::optional<double>(rb / ra) : std::nullopt;
    lastZoomA = a->zoom();
    lastZoomB = b->zoom();
}

void ScrollLock::unlock() {
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    const bool was = !a.isNull() || !b.isNull();
    a.clear();
    b.clear();
    settling = false;
    offset = 0;
    zoomRatio.reset();
    if (was) {
        Q_EMIT lockedChanged();
    }
}

void ScrollLock::follow(ViewController& from, ViewController& to, int toOffset) {
    const auto place = from.placeAt(anchorOf(from));
    if (!place || to.pageCount() == 0) {
        return;
    }
    const long page = std::clamp(static_cast<long>(place->page) + toOffset, 0L, static_cast<long>(to.pageCount()) - 1);
    syncing = true;
    to.showPlace({static_cast<size_t>(page), place->relative}, anchorOf(to));
    syncing = false;
}

void ScrollLock::moved(ViewController& from, ViewController& to, bool fromIsA) {
    if (syncing) {
        return;
    }
    QSizeF& lastSize = fromIsA ? lastSizeA : lastSizeB;
    if (settling) {
        return;
    }
    if (lastSize.isEmpty() && !from.viewSize().isEmpty()) {
        // Its first size (a view locked before it was shown): where it opens is where it is meant to be (a page it
        // was sent to, after the fit of its first size). The pair is taken from there once it has settled.
        lastSize = from.viewSize();
        settling = true;
        QTimer::singleShot(0, this, [this] {
            settling = false;
            recapture();
        });
        return;
    }
    if (from.viewSize() != lastSize) {
        // Its size changed (the divider, the window): it follows the other one, never the other way round
        lastSize = from.viewSize();
        follow(to, from, fromIsA ? -offset : offset);
        return;
    }
    follow(from, to, fromIsA ? offset : -offset);
}

void ScrollLock::zoomed(ViewController& from, ViewController& to, bool fromIsA) {
    if (syncing) {
        return;
    }
    double& lastFrom = fromIsA ? lastZoomA : lastZoomB;
    double& lastTo = fromIsA ? lastZoomB : lastZoomA;
    const QSizeF& lastSize = fromIsA ? lastSizeA : lastSizeB;
    const double before = lastFrom;
    lastFrom = from.zoom();
    if (settling || from.viewSize() != lastSize || from.viewSize().isEmpty() || to.viewSize().isEmpty()) {
        return;  // (a new size that keeps a fit: not a zoom of the reader's)
    }
    double target = 0;
    const double rel = relativeZoom(from);
    const auto toPlace = to.placeAt(anchorOf(to));
    const double toFit = toPlace ? to.fitWidthZoom(toPlace->page) : 0;
    if (zoomRatio && rel > 0 && toFit > 0) {
        target = (fromIsA ? rel * *zoomRatio : rel / *zoomRatio) * toFit;
    } else if (before > 0) {
        target = to.zoom() * from.zoom() / before;  // (not known how wide: by the same factor)
    }
    if (target > 0) {
        syncing = true;
        to.setZoom(target, middleOf(to));
        syncing = false;
    }
    lastTo = to.zoom();
    // (where: the `changed` that follows the zoom)
}

void ScrollLock::showPages(ViewController& a, size_t pageA, ViewController& b, size_t pageB) {
    if (pageA >= a.pageCount() || pageB >= b.pageCount()) {
        return;
    }
    a.scrollToPage(pageA);
    const auto place = a.placeAt(anchorOf(a), pageA);
    // The same place on its page: its top where a's top is, or as far into it
    b.showPlace({pageB, place ? place->relative : QPointF(0.5, 0)}, anchorOf(b));
}

void ScrollLock::showPages(size_t pageA, size_t pageB) {
    if (!a || !b || pageA >= a->pageCount() || pageB >= b->pageCount()) {
        return;
    }
    offset = static_cast<int>(pageB) - static_cast<int>(pageA);
    syncing = true;
    showPages(*a, pageA, *b, pageB);
    syncing = false;
}

}  // namespace xqt
