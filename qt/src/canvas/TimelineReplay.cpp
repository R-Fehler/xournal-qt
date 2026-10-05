#include "TimelineReplay.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "render/ElementFilter.h"
#include "session/StickyNote.h"
#include "util/Range.h"
#include "view/View.h"

#include "CanvasPage.h"
#include "CanvasView.h"

namespace xqt {

namespace {
/// The pictures are drawn with the first `shown` elements of the timeline (and every element not on it)
class ShownFilter final: public render::ElementFilter {
public:
    ShownFilter(std::shared_ptr<const timeline::Timeline> line, size_t shown): line(std::move(line)), shown(shown) {}
    bool shows(const Element* e) const override {
        const auto i = line->indexOf(e);
        return !i || *i < shown;
    }
    const std::shared_ptr<const timeline::Timeline> line;
    const size_t shown;
};

double lengthOf(const std::vector<Point>& points) {
    double length = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        length += points[i - 1].lineLengthTo(points[i]);
    }
    return length;
}

/// The part of the points up to `fraction` of their length (the last one between two points)
std::vector<Point> partOf(const std::vector<Point>& points, double fraction) {
    if (points.size() < 2 || fraction >= 1) {
        return points;
    }
    const double target = lengthOf(points) * std::max(0.0, fraction);
    std::vector<Point> out{points.front()};
    double walked = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        const double step = points[i - 1].lineLengthTo(points[i]);
        if (walked + step >= target) {
            const double f = step > 0 ? (target - walked) / step : 0;
            const Point& a = points[i - 1];
            const Point& b = points[i];
            const double z = a.z == Point::NO_PRESSURE || b.z == Point::NO_PRESSURE ? a.z : a.z + (b.z - a.z) * f;
            out.emplace_back(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, z);
            break;
        }
        walked += step;
        out.push_back(points[i]);
    }
    if (out.size() < 2) {
        out.push_back(out.front());  // (a dot so far)
    }
    return out;
}

/// The area of a stroke between two fractions of its length (page coordinates, with its width)
Range rangeOf(const Stroke& s, double from, double to) {
    const auto& points = s.getPointVector();
    const auto head = partOf(points, from);
    const auto part = partOf(points, to);
    Range r(head.back().x, head.back().y);
    for (size_t i = head.size() >= 2 ? head.size() - 2 : 0; i < part.size(); ++i) {
        r.addPoint(part[i].x, part[i].y);
    }
    r.addPadding(s.getWidth() + 2);
    return r;
}

Range areaOf(const Element* e) {
    Range r(e->getBoundingBox());
    r.addPadding(2);
    return r;
}

bool isNotePaper(const timeline::Event& e) { return e.layer && sticky::paperOf(*e.layer) == e.element; }
}  // namespace

TimelineReplay::TimelineReplay(CanvasView& view, std::shared_ptr<const timeline::Timeline> timeline, int64_t t):
        view(view), line(std::move(timeline)), at(t), now(line->frameAt(t)) {
    committedCount = now.shown;
    committed = std::make_shared<ShownFilter>(line, committedCount);
    sinceCommit.start();
}

std::shared_ptr<const render::ElementFilter> TimelineReplay::rasterFilter() const {
    std::lock_guard lock(filterMutex);
    return committed;
}

size_t TimelineReplay::committedShown() const { return committedCount; }

size_t TimelineReplay::shownBy(const render::ElementFilter* drawn) const {
    if (const auto* f = dynamic_cast<const ShownFilter*>(drawn); f && f->line == line) {
        return f->shown;
    }
    return line->events().size();  // (drawn whole: before the replay, or by another one)
}

void TimelineReplay::commit(size_t shown) {
    const size_t from = std::min(shown, committedCount);
    const size_t to = std::max(shown, committedCount);
    std::set<size_t> pages;
    for (size_t i = from; i < to && i < line->events().size(); ++i) {
        pages.insert(line->events()[i].page);
    }
    {
        std::lock_guard lock(filterMutex);
        committed = std::make_shared<ShownFilter>(line, shown);
        committedCount = shown;
    }
    sinceCommit.restart();
    for (size_t p: pages) {
        if (CanvasPage* page = p < view.pageCount() ? view.getPage(p) : nullptr; page && page->bufferInfo().valid) {
            page->rerenderPage();  // (the old picture stays until the new one is there; the overlay fills in)
        }
    }
}

void TimelineReplay::flagChanges(const timeline::Frame& before) {
    const auto& events = line->events();
    const auto flag = [&](size_t index, const Range& r) {
        if (CanvasPage* page = events[index].page < view.pageCount() ? view.getPage(events[index].page) : nullptr) {
            page->flagDirtyRegion(r);
        }
    };
    // Elements that came or went whole
    const size_t lo = std::min(before.shown, now.shown);
    const size_t hi = std::max(before.shown, now.shown);
    for (size_t i = lo; i < hi && i < events.size(); ++i) {
        flag(i, areaOf(events[i].element));
    }
    // The stroke being written: the part added (or all of it when it changed)
    if (now.drawing && before.drawing == now.drawing && now.fraction >= before.fraction) {
        if (const auto* s = dynamic_cast<const Stroke*>(events[*now.drawing].element)) {
            flag(*now.drawing, rangeOf(*s, before.fraction, now.fraction));
        }
    } else {
        if (before.drawing && *before.drawing < events.size()) {
            flag(*before.drawing, areaOf(events[*before.drawing].element));
        }
        if (now.drawing) {
            flag(*now.drawing, areaOf(events[*now.drawing].element));
        }
    }
}

void TimelineReplay::seek(int64_t t) {
    const timeline::Frame before = now;
    at = std::clamp<int64_t>(t, 0, line->duration());
    now = line->frameAt(at);
    if (now == before) {
        return;
    }
    flagChanges(before);
    bool commitNow = now.shown < committedCount || now.shown - committedCount > MAX_OVERLAY ||
                     (now.shown > committedCount && sinceCommit.elapsed() > COMMIT_AFTER_MS);
    for (size_t i = std::min(committedCount, now.shown); !commitNow && i < now.shown; ++i) {
        commitNow = isNotePaper(line->events()[i]);
    }
    if (commitNow) {
        commit(now.shown);
    }
}

void TimelineReplay::settle() {
    if (now.shown != committedCount) {
        commit(now.shown);
    }
}

void TimelineReplay::drawEvent(cairo_t* cr, size_t index, double fraction) const {
    const timeline::Event& ev = line->events()[index];
    cairo_save(cr);
    if (ev.layer) {
        if (const auto look = sticky::lookOf(*ev.layer); look && !isNotePaper(ev)) {
            cairo_rectangle(cr, look->rect.x, look->rect.y, look->rect.width, look->rect.height);  // (in its note)
            cairo_clip(cr);
        }
    }
    const auto ctx = xoj::view::Context::createDefault(cr);
    if (const auto* s = dynamic_cast<const Stroke*>(ev.element); s && fraction < 1) {
        auto part = s->cloneStroke();
        part->setPointVector(partOf(s->getPointVector(), fraction));
        xoj::view::ElementView::createFromElement(part.get())->draw(ctx);
    } else {
        xoj::view::ElementView::createFromElement(ev.element)->draw(ctx);
    }
    cairo_restore(cr);
}

void TimelineReplay::drawOverlay(cairo_t* cr, size_t page, const render::ElementFilter* drawn) const {
    const auto& events = line->events();
    const size_t from = shownBy(drawn);
    for (size_t i = from; i < now.shown && i < events.size(); ++i) {
        if (events[i].page == page) {
            drawEvent(cr, i, 1);
        }
    }
    if (now.drawing && *now.drawing >= from && events[*now.drawing].page == page) {
        drawEvent(cr, *now.drawing, now.fraction);
    }
}

std::optional<size_t> TimelineReplay::shownAt(size_t page, double x, double y, double radius) const {
    std::optional<size_t> best;
    double nearest = radius;
    const auto& events = line->events();
    const size_t end = now.drawing ? *now.drawing + 1 : now.shown;
    for (size_t i = 0; i < end && i < events.size(); ++i) {
        if (events[i].page != page || isNotePaper(events[i])) {
            continue;
        }
        const double d = events[i].element->distanceTo(x, y);
        if (d <= nearest) {  // (the later one of two as near: drawn over it)
            nearest = d;
            best = i;
        }
    }
    return best;
}

}  // namespace xqt
