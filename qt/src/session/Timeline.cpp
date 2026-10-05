#include "Timeline.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QTime>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "audio/DocumentAudio.h"
#include "model/AudioContent.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"

#include "StickyNote.h"

#include "filesystem.h"

namespace xqt::timeline {

std::optional<int64_t> startFromName(const std::string& name) {
    const QString stem = QString::fromStdU16String(fs::path(name).filename().u16string());
    static const QRegularExpression re(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})_(\\d{2})-(\\d{2})-(\\d{2})"));
    const auto m = re.match(stem);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    const QTime time(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt());
    if (!date.isValid() || !time.isValid()) {
        return std::nullopt;
    }
    return QDateTime(date, time, Qt::LocalTime).toMSecsSinceEpoch();
}

namespace {
double pathLength(const Element* e) {
    if (e->getType() != ELEMENT_STROKE) {
        return 0;
    }
    const auto& points = static_cast<const Stroke*>(e)->getPointVector();
    double length = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        length += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    }
    return length;
}

/// An element of the document, with what places it
struct Item {
    const Element* element = nullptr;
    const Layer* layerOf = nullptr;
    size_t page = 0;
    size_t layer = 0;
    size_t position = 0;
    std::string recording;  ///< upstream's fn ("": none)
    int64_t ts = 0;
    int64_t created = 0;
    int64_t when = 0;  ///< the absolute time it gets (0: none)
};
}  // namespace

Timeline Timeline::build(const Document& doc, const LengthOf& lengthOf) {
    Timeline out;
    std::vector<Item> items;
    std::set<std::string> names;  // every recording referred to (elements and memos)
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        const PageRef page = doc.getPage(p);
        for (const std::string& memo: audio::memosOf(*page)) {
            names.insert(memo);
        }
        size_t l = 0;
        for (const Layer* layer: page->getLayersView()) {
            ++l;
            if (!layer->isVisible()) {
                continue;
            }
            size_t i = 0;
            for (const Element* e: layer->getElementsView()) {
                Item item;
                item.element = e;
                item.layerOf = layer;
                item.page = p;
                item.layer = l;
                item.position = i++;
                item.created = std::max<int64_t>(0, e->getCreated());
                if (const AudioContent* a = audio::audioOf(e)) {
                    item.recording = audio::nameOf(*a);
                    item.ts = static_cast<int64_t>(a->getTimestamp());
                    names.insert(item.recording);
                }
                items.push_back(std::move(item));
            }
        }
    }

    // Where each recording starts: what its elements say (made at `created`, `ts` into it), else its name
    std::map<std::string, int64_t> starts;
    for (const Item& item: items) {
        if (!item.recording.empty() && item.created > 0) {
            const int64_t start = item.created - item.ts;
            auto [it, added] = starts.emplace(item.recording, start);
            if (!added) {
                it->second = std::min(it->second, start);
            }
        }
    }
    std::map<std::string, int64_t> lastTs;
    for (const Item& item: items) {
        if (!item.recording.empty()) {
            lastTs[item.recording] = std::max(lastTs[item.recording], item.ts);
        }
    }
    for (const std::string& name: names) {
        if (!starts.count(name)) {
            if (const auto s = startFromName(name)) {
                starts[name] = *s;
            }
        }
    }
    for (Item& item: items) {
        if (!item.recording.empty()) {
            if (auto it = starts.find(item.recording); it != starts.end()) {
                item.when = it->second + item.ts;
                continue;
            }
        }
        item.when = item.created;
    }
    // The tracks (recordings placed on the clock)
    for (const auto& [name, start]: starts) {
        Track t;
        t.name = name;
        t.start = start;
        if (const auto length = lengthOf ? lengthOf(name) : std::nullopt) {
            t.length = std::max<int64_t>(0, *length);
            t.found = true;
        }
        if (auto it = lastTs.find(name); it != lastTs.end()) {
            t.length = std::max(t.length, it->second + 1000);  // (a file shorter than its ink, or none: the ink's span)
        }
        if (t.length > 0) {
            out.recordings.push_back(std::move(t));
        }
    }

    // The prelude: the elements without a time, in the document's order
    std::vector<const Item*> untimed;
    std::vector<const Item*> timed;
    for (const Item& item: items) {
        (item.when > 0 ? timed : untimed).push_back(&item);
    }
    const int64_t step =
            untimed.empty() ?
                    0 :
                    std::max<int64_t>(
                            1, std::min<int64_t>(PRELUDE_STEP, PRELUDE_MAX / static_cast<int64_t>(untimed.size())));
    for (size_t i = 0; i < untimed.size(); ++i) {
        out.list.push_back(
                {untimed[i]->element, untimed[i]->layerOf, untimed[i]->page, static_cast<int64_t>(i) * step, 0, 0});
    }
    int64_t bar = 0;
    if (!untimed.empty()) {
        out.sessions.push_back({0, 0});
        bar = static_cast<int64_t>(untimed.size()) * step + PAUSE_ON_BAR;
    }

    // The clock with its long pauses taken out: what is busy (elements, recordings) in the order it starts
    std::stable_sort(timed.begin(), timed.end(), [](const Item* a, const Item* b) { return a->when < b->when; });
    std::vector<std::pair<int64_t, int64_t>> busy;
    for (const Item* item: timed) {
        busy.emplace_back(item->when, item->when);
    }
    for (const Track& t: out.recordings) {
        busy.emplace_back(t.start, t.start + t.length);
    }
    std::sort(busy.begin(), busy.end());
    int64_t coveredTo = std::numeric_limits<int64_t>::min();
    for (const auto& [from, to]: busy) {
        if (out.segments.empty()) {
            out.segments.push_back({from, bar});
            out.sessions.push_back({bar, from});
        } else if (const int64_t pause = from - coveredTo; pause > IDLE_GAP) {
            const Segment& last = out.segments.back();
            const int64_t barAt = last.bar + (coveredTo - last.abs) + PAUSE_ON_BAR;
            out.segments.push_back({from, barAt});
            if (pause > SESSION_GAP) {
                out.sessions.push_back({barAt, from});
            }
        }
        coveredTo = std::max(coveredTo, to);
    }
    for (const Item* item: timed) {
        out.list.push_back({item->element, item->layerOf, item->page, out.barOf(item->when), 0, item->when});
    }
    for (Track& t: out.recordings) {
        t.at = out.barOf(t.start);
    }
    // (in the document's order where they are at one time: stable sorts)

    // How long each stroke is drawn on: at handwriting speed, until the next element at most
    for (size_t i = 0; i < out.list.size(); ++i) {
        Event& e = out.list[i];
        if (e.element->getType() != ELEMENT_STROKE || (e.layer && sticky::paperOf(*e.layer) == e.element)) {
            continue;  // (a sticky note's paper is there at once)
        }
        int64_t length = e.when == 0 ?
                                 step :
                                 std::clamp<int64_t>(static_cast<int64_t>(pathLength(e.element) / STROKE_SPEED * 1000),
                                                     STROKE_MIN, STROKE_MAX);
        if (i + 1 < out.list.size()) {
            length = std::min(length, out.list[i + 1].at - e.at);
        }
        e.length = std::max<int64_t>(0, length);
    }
    for (size_t i = 0; i < out.list.size(); ++i) {
        out.index.emplace(out.list[i].element, i);
    }
    int64_t end = 0;
    for (const Event& e: out.list) {
        end = std::max(end, e.at + e.length);
    }
    for (const Track& t: out.recordings) {
        end = std::max(end, t.at + t.length);
    }
    out.total = out.empty() ? 0 : end + 500;
    return out;
}

int64_t Timeline::barOf(int64_t when) const {
    if (segments.empty()) {
        return 0;
    }
    auto it = std::upper_bound(segments.begin(), segments.end(), when,
                               [](int64_t w, const Segment& s) { return w < s.abs; });
    if (it == segments.begin()) {
        return segments.front().bar;
    }
    const Segment& s = *std::prev(it);
    int64_t bar = s.bar + (when - s.abs);
    if (it != segments.end()) {
        bar = std::min(bar, it->bar);  // (in a shortened pause: no later than what comes after it)
    }
    return bar;
}

Frame Timeline::frameAt(int64_t t) const {
    Frame f;
    const auto it = std::upper_bound(list.begin(), list.end(), t, [](int64_t v, const Event& e) { return v < e.at; });
    const size_t k = static_cast<size_t>(it - list.begin());
    f.shown = k;
    if (k > 0) {
        const Event& last = list[k - 1];
        if (last.length > 0 && t < last.at + last.length) {
            f.shown = k - 1;
            f.drawing = k - 1;
            f.fraction = std::clamp(static_cast<double>(t - last.at) / static_cast<double>(last.length), 0.0, 1.0);
        }
    }
    return f;
}

std::optional<size_t> Timeline::indexOf(const Element* e) const {
    if (auto it = index.find(e); it != index.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<int64_t> Timeline::wallTime(int64_t t) const {
    if (segments.empty() || t < segments.front().bar) {
        return std::nullopt;
    }
    auto it = std::upper_bound(segments.begin(), segments.end(), t,
                               [](int64_t v, const Segment& s) { return v < s.bar; });
    const Segment& s = *std::prev(it);
    return s.abs + (t - s.bar);
}

std::optional<Heard> Timeline::heardAt(int64_t t) const {
    std::optional<Heard> heard;
    int64_t latest = std::numeric_limits<int64_t>::min();
    for (size_t i = 0; i < recordings.size(); ++i) {
        const Track& r = recordings[i];
        if (r.found && t >= r.at && t < r.at + r.length && r.at >= latest) {
            latest = r.at;
            heard = Heard{i, t - r.at};
        }
    }
    return heard;
}

}  // namespace xqt::timeline
