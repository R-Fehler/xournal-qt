#include "InkLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <tuple>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/InkText.h"

namespace xqt::hwr {

InkStroke InkStroke::of(std::vector<QPointF> points, float width, std::vector<float> widths) {
    InkStroke s;
    s.points = std::move(points);
    s.width = width;
    s.widths = std::move(widths);
    if (!s.points.empty()) {
        double x0 = s.points[0].x(), y0 = s.points[0].y(), x1 = x0, y1 = y0;
        for (const QPointF& p: s.points) {
            x0 = std::min(x0, p.x());
            y0 = std::min(y0, p.y());
            x1 = std::max(x1, p.x());
            y1 = std::max(y1, p.y());
        }
        s.box = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    return s;
}

namespace {
/// A pen stroke as the layout takes it (none: not a pen stroke, or empty)
std::optional<InkStroke> inkOf(const Element* e) {
    if (e->getType() != ELEMENT_STROKE) {
        return std::nullopt;
    }
    const auto* stroke = static_cast<const Stroke*>(e);
    if (stroke->getToolType() != StrokeTool::PEN || stroke->getPointCount() == 0) {
        return std::nullopt;
    }
    const std::vector<Point>& pts = stroke->getPointVector();
    std::vector<QPointF> points;
    points.reserve(pts.size());
    std::vector<float> widths;
    const bool pressure = std::any_of(pts.begin(), pts.end(), [](const Point& p) { return p.z != Point::NO_PRESSURE; });
    if (pressure) {
        widths.reserve(pts.size());
    }
    for (const Point& p: pts) {
        points.emplace_back(p.x, p.y);
        if (pressure) {
            widths.push_back(static_cast<float>(p.z != Point::NO_PRESSURE ? p.z : stroke->getWidth()));
        }
    }
    InkStroke s = InkStroke::of(std::move(points), static_cast<float>(stroke->getWidth()), std::move(widths));
    s.filled = stroke->getFill() > 0;
    return s;
}
}  // namespace

std::vector<InkStroke> strokesOf(const XojPage& page) {
    std::vector<InkStroke> out;
    for (const Layer* layer: page.getLayersView()) {
        if (!layer->isVisible()) {
            continue;
        }
        for (const auto& e: layer->getElementsView()) {
            if (auto s = inkOf(e)) {
                out.push_back(std::move(*s));
            }
        }
    }
    return out;
}

std::vector<InkStroke> strokesOf(const std::vector<const Element*>& elements) {
    std::vector<InkStroke> out;
    for (const Element* e: elements) {
        if (auto s = inkOf(e)) {
            out.push_back(std::move(*s));
        }
    }
    return out;
}

namespace {

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

double pathLength(const InkStroke& s) {
    double len = 0;
    for (size_t i = 1; i < s.points.size(); ++i) {
        len += std::hypot(s.points[i].x() - s.points[i - 1].x(), s.points[i].y() - s.points[i - 1].y());
    }
    return len;
}

bool isDrawing(const InkStroke& s, double u) {
    if (s.filled || s.box.height() > 2.5 * u) {
        return true;
    }
    const double len = pathLength(s);
    const double chord = s.points.size() > 1 ? std::hypot(s.points.back().x() - s.points.front().x(),
                                                          s.points.back().y() - s.points.front().y())
                                             : 0;
    return len > 4 * u && chord > 0.95 * len;
}

/// The threshold between the gaps inside words and those between them: two classes with the largest variance between
/// them (Otsu), halfway between the two gaps where they split.
double otsu(std::vector<double> gaps) {
    std::sort(gaps.begin(), gaps.end());
    const size_t n = gaps.size();
    std::vector<double> prefix(n + 1, 0);
    for (size_t i = 0; i < n; ++i) {
        prefix[i + 1] = prefix[i] + gaps[i];
    }
    double best = -1, threshold = 0;
    for (size_t i = 1; i < n; ++i) {
        const double ma = prefix[i] / static_cast<double>(i);
        const double mb = (prefix[n] - prefix[i]) / static_cast<double>(n - i);
        const double v = static_cast<double>(i) * static_cast<double>(n - i) * (ma - mb) * (ma - mb);
        if (v > best) {
            best = v;
            threshold = (gaps[i - 1] + gaps[i]) / 2;
        }
    }
    return threshold;
}

QRectF unite(const QRectF& a, const QRectF& b) {
    return QRectF(QPointF(std::min(a.left(), b.left()), std::min(a.top(), b.top())),
                  QPointF(std::max(a.right(), b.right()), std::max(a.bottom(), b.bottom())));
}

constexpr quint64 FNV_OFFSET = 1469598103934665603ULL;
constexpr quint64 FNV_PRIME = 1099511628211ULL;
void mix(quint64& h, qint32 v) {
    auto u = static_cast<quint32>(v);
    for (int i = 0; i < 4; ++i) {
        h ^= (u >> (8 * i)) & 0xff;
        h *= FNV_PRIME;
    }
}
/// In tenths of a point. (The odd offset keeps the coordinates of files, which have 5 decimals at most, off the
/// boundaries between two tenths: there the error of the subtraction from the line's origin could round them either
/// way, and a moved line would get another hash.)
qint32 tenths(double v) { return static_cast<qint32>(std::lround(v * 10 + 0.0137137)); }
}  // namespace

quint64 hashOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>& which, QPointF origin) {
    quint64 h = FNV_OFFSET;
    for (const uint32_t i: which) {
        const InkStroke& s = strokes[i];
        mix(h, static_cast<qint32>(s.points.size()));
        mix(h, tenths(s.width));
        for (size_t k = 0; k < s.points.size(); ++k) {
            mix(h, tenths(s.points[k].x() - origin.x()));
            mix(h, tenths(s.points[k].y() - origin.y()));
            if (k < s.widths.size()) {
                mix(h, tenths(s.widths[k]));
            }
        }
    }
    return h;
}

std::vector<InkWordBox> wordsOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>& line, double h) {
    std::vector<uint32_t> big, small;
    for (const uint32_t i: line) {
        const QRectF& b = strokes[i].box;
        (std::max(b.width(), b.height()) >= 0.5 * h ? big : small).push_back(i);
    }
    std::stable_sort(big.begin(), big.end(),
                     [&](uint32_t a, uint32_t b) { return strokes[a].box.left() < strokes[b].box.left(); });
    std::vector<double> gaps;
    double right = std::numeric_limits<double>::lowest();
    for (size_t k = 0; k < big.size(); ++k) {
        const QRectF& b = strokes[big[k]].box;
        if (k > 0 && b.left() - right > 0) {
            gaps.push_back(b.left() - right);
        }
        right = k == 0 ? b.right() : std::max(right, b.right());
    }
    double threshold = gaps.size() >= 4 ? otsu(gaps) : 2.0 * h;
    threshold = std::clamp(threshold, 0.6 * h, 2.0 * h);
    std::vector<InkWordBox> words;
    for (size_t k = 0; k < big.size(); ++k) {
        const QRectF& b = strokes[big[k]].box;
        if (words.empty() || b.left() - right > threshold) {
            words.push_back({b, {big[k]}});
            right = b.right();
        } else {
            words.back().strokes.push_back(big[k]);
            words.back().box = unite(words.back().box, b);
            right = std::max(right, b.right());
        }
    }
    if (words.empty()) {
        return words;
    }
    // Small strokes join the word nearest to them (by their middle)
    std::vector<QRectF> grown;
    for (const InkWordBox& w: words) {
        grown.push_back(w.box);
    }
    for (const uint32_t i: small) {
        const double cx = strokes[i].box.center().x();
        size_t best = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (size_t k = 0; k < words.size(); ++k) {
            const QRectF& b = words[k].box;
            const double d = cx >= b.left() && cx <= b.right() ? 0 : std::min(std::abs(cx - b.left()), std::abs(cx - b.right()));
            if (d < bestDistance) {
                bestDistance = d;
                best = k;
            }
        }
        words[best].strokes.push_back(i);
        grown[best] = unite(grown[best], strokes[i].box);
    }
    for (size_t k = 0; k < words.size(); ++k) {
        words[k].box = grown[k];
        std::sort(words[k].strokes.begin(), words[k].strokes.end());
    }
    return words;
}

namespace {
/// The page's units (step 1) from the heights of these strokes' boxes
std::pair<double, double> unitsOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>* which) {
    std::vector<double> heights;
    if (which) {
        heights.reserve(which->size());
        for (const uint32_t i: *which) {
            heights.push_back(strokes[i].box.height());
        }
    } else {
        heights.reserve(strokes.size());
        for (const InkStroke& s: strokes) {
            heights.push_back(s.box.height());
        }
    }
    double h = median(heights);
    h = h > 0 ? h : 1;
    std::vector<double> upper;
    for (const double v: heights) {
        if (v >= h) {
            upper.push_back(v);
        }
    }
    double u = median(upper);
    u = u > 0 ? u : h;
    return {h, u};
}

/// Steps 2-6 on these strokes (in the order of writing) with the units h, u: their lines (not sorted yet) to `lines`,
/// the drawings to `drawings`
void linesOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>& which, double h, double u,
             std::vector<InkLine>& lines, std::vector<uint32_t>& drawings) {
    // Lines in the order of writing
    struct Group {
        std::vector<uint32_t> strokes;
        std::vector<double> middles;  ///< of its strokes that are not small
    };
    std::vector<Group> groups;
    const InkStroke* previous = nullptr;
    for (const uint32_t i: which) {
        const InkStroke& s = strokes[i];
        if (s.points.empty() || isDrawing(s, u)) {
            drawings.push_back(i);
            continue;
        }
        const QPointF c = s.box.center();
        const bool big = s.box.height() >= 0.5 * u;
        bool fresh = !previous;
        if (previous) {
            const Group& g = groups.back();
            const size_t take = std::min<size_t>(8, g.middles.size());
            double line = c.y();
            if (take > 0) {
                line = std::accumulate(g.middles.end() - static_cast<std::ptrdiff_t>(take), g.middles.end(), 0.0) /
                       static_cast<double>(take);
            }
            const QPointF pc = previous->box.center();
            fresh = (c.x() - pc.x() < -2 * u && c.y() - line > 0.5 * u) || std::abs(c.y() - line) > 1.5 * u;
        }
        if (fresh) {
            groups.push_back({});
        }
        groups.back().strokes.push_back(i);
        if (big) {
            groups.back().middles.push_back(c.y());
        }
        previous = &s;
    }

    // Pieces at the same height are one line
    struct Band {
        double top = 0, bottom = 0;
        std::vector<uint32_t> strokes;
    };
    std::vector<Band> bands;
    for (Group& g: groups) {
        Band b{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), std::move(g.strokes)};
        bool any = false;
        for (const uint32_t i: b.strokes) {
            if (strokes[i].box.height() >= 0.5 * u) {
                b.top = std::min(b.top, strokes[i].box.top());
                b.bottom = std::max(b.bottom, strokes[i].box.bottom());
                any = true;
            }
        }
        if (!any) {
            for (const uint32_t i: b.strokes) {
                b.top = std::min(b.top, strokes[i].box.top());
                b.bottom = std::max(b.bottom, strokes[i].box.bottom());
            }
        }
        bands.push_back(std::move(b));
    }
    std::sort(bands.begin(), bands.end(),
              [](const Band& a, const Band& b) { return a.top + a.bottom < b.top + b.bottom; });
    for (bool changed = true; changed;) {
        changed = false;
        std::vector<Band> merged;
        for (Band& b: bands) {
            bool joined = false;
            for (Band& m: merged) {
                const double overlap = std::min(m.bottom, b.bottom) - std::max(m.top, b.top);
                const bool small = b.bottom - b.top < 0.5 * u;
                if ((overlap > 0 && overlap >= 0.5 * std::min(m.bottom - m.top, b.bottom - b.top)) ||
                    (small && overlap >= 0)) {
                    m.top = std::min(m.top, b.top);
                    m.bottom = std::max(m.bottom, b.bottom);
                    m.strokes.insert(m.strokes.end(), b.strokes.begin(), b.strokes.end());
                    joined = changed = true;
                    break;
                }
            }
            if (!joined) {
                merged.push_back(std::move(b));
            }
        }
        bands = std::move(merged);
    }
    std::sort(bands.begin(), bands.end(), [](const Band& a, const Band& b) { return a.top < b.top; });

    for (Band& b: bands) {
        std::sort(b.strokes.begin(), b.strokes.end());  // (the order of writing)
        InkLine line;
        line.words = wordsOf(strokes, b.strokes, h);
        if (line.words.empty()) {
            continue;  // only small strokes
        }
        line.strokes = std::move(b.strokes);
        line.box = strokes[line.strokes.front()].box;
        for (const uint32_t i: line.strokes) {
            line.box = unite(line.box, strokes[i].box);
        }
        line.upright = line.box;
        line.hash = hashOf(strokes, line.strokes, line.box.topLeft());
        lines.push_back(std::move(line));
    }
}

/// How far apart two boxes are (0: they overlap)
double distance(const QRectF& a, const QRectF& b) {
    const double dx = std::max({0.0, a.left() - b.right(), b.left() - a.right()});
    const double dy = std::max({0.0, a.top() - b.bottom(), b.top() - a.bottom()});
    return std::hypot(dx, dy);
}

/// The direction of a run of strokes (degrees, clockwise on the page), if it has one (see InkLayout.h); its length
/// along that direction and its width across it
struct Direction {
    double angle = 0;
    double length = 0, width = 0;
};
std::optional<Direction> directionOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>& run,
                                     double s) {
    // The principal axis of its ink: the segments' middles weighted by their lengths (not by how fast the pen went)
    double weight = 0, mx = 0, my = 0;
    for (const uint32_t i: run) {
        const auto& p = strokes[i].points;
        for (size_t k = 1; k < p.size(); ++k) {
            const double w = std::hypot(p[k].x() - p[k - 1].x(), p[k].y() - p[k - 1].y());
            mx += w * (p[k].x() + p[k - 1].x()) / 2;
            my += w * (p[k].y() + p[k - 1].y()) / 2;
            weight += w;
        }
    }
    if (weight <= 0) {
        return std::nullopt;
    }
    mx /= weight;
    my /= weight;
    double sxx = 0, sxy = 0, syy = 0;
    for (const uint32_t i: run) {
        const auto& p = strokes[i].points;
        for (size_t k = 1; k < p.size(); ++k) {
            const double w = std::hypot(p[k].x() - p[k - 1].x(), p[k].y() - p[k - 1].y());
            const double x = (p[k].x() + p[k - 1].x()) / 2 - mx, y = (p[k].y() + p[k - 1].y()) / 2 - my;
            sxx += w * x * x;
            sxy += w * x * y;
            syy += w * y * y;
        }
    }
    const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
    const QPointF e(std::cos(theta), std::sin(theta)), n(-e.y(), e.x());
    auto dot = [](QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); };
    double a0 = std::numeric_limits<double>::max(), a1 = std::numeric_limits<double>::lowest(), n0 = a0, n1 = a1;
    std::vector<double> shapes;  ///< of each stroke: how much longer across the axis than along it
    for (const uint32_t i: run) {
        double sa0 = std::numeric_limits<double>::max(), sa1 = std::numeric_limits<double>::lowest(), sn0 = sa0,
               sn1 = sa1;
        for (const QPointF& p: strokes[i].points) {
            sa0 = std::min(sa0, dot(p, e));
            sa1 = std::max(sa1, dot(p, e));
            sn0 = std::min(sn0, dot(p, n));
            sn1 = std::max(sn1, dot(p, n));
        }
        a0 = std::min(a0, sa0);
        a1 = std::max(a1, sa1);
        n0 = std::min(n0, sn0);
        n1 = std::max(n1, sn1);
        shapes.push_back((sn1 - sn0) / std::max(sa1 - sa0, 1e-3));
    }
    Direction d;
    d.length = a1 - a0;
    d.width = n1 - n0;
    if (run.size() == 2 || d.length < 6 * s || d.length < 3 * d.width) {
        return std::nullopt;  // (too short or not narrow, or two strokes: an i and its dot; no direction to tell)
    }
    if (median(shapes) > 2) {
        return std::nullopt;  // (its strokes lie across it: words written one below the other, a list)
    }
    // Which way along the axis: the strokes one after the other, or a single stroke from its start to its end
    double along = 0;
    if (run.size() >= 2) {
        double forth = 0, across = 0;
        for (size_t k = 1; k < run.size(); ++k) {
            const QPointF step = strokes[run[k]].box.center() - strokes[run[k - 1]].box.center();
            along += dot(step, e);
            forth += std::abs(dot(step, e));
            across += std::abs(dot(step, n));
        }
        if (forth < 2 * across || std::abs(along) < 0.5 * forth) {
            return std::nullopt;  // (back and forth, or across: a list, a column of words)
        }
    } else {
        const auto& p = strokes[run.front()].points;
        along = dot(p.back() - p.front(), e);
        if (std::abs(along) < 0.5 * d.length) {
            return std::nullopt;
        }
    }
    const QPointF v = along > 0 ? e : -e;
    d.angle = std::atan2(v.y(), v.x()) * 180 / M_PI;
    return d;
}

/// A run's angle as it is laid out: 0 (as on the page), ±90, or its own between 20 and 70 degrees either way
double snapped(double angle) {
    const double a = std::abs(angle);
    if (a <= 20 || a >= 110) {
        return 0;  // (about left to right; leftwards is not taken for writing)
    }
    if (a >= 70) {
        return angle > 0 ? 90 : -90;
    }
    return angle;
}
}  // namespace

InkStroke turned(const InkStroke& s, double degrees) {
    std::vector<QPointF> points;
    points.reserve(s.points.size());
    for (const QPointF& p: s.points) {
        points.push_back(ink::turned(p, degrees));
    }
    InkStroke t = InkStroke::of(std::move(points), s.width, s.widths);
    t.filled = s.filled;
    return t;
}

QPointF InkLine::origin() const { return angle == 0 ? box.topLeft() : ink::turned(upright.topLeft(), angle); }

std::vector<Frame> framesOf(const std::vector<InkStroke>& strokes, double h, double u) {
    std::vector<Frame> out;
    // s: the median of the strokes' smaller sides (about a letter's width or height whichever way it was written)
    std::vector<double> sides;
    for (const InkStroke& st: strokes) {
        if (!st.points.empty() && !st.filled && std::max(st.box.width(), st.box.height()) >= 0.5 * h) {
            sides.push_back(std::min(st.box.width(), st.box.height()));
        }
    }
    double s = median(std::move(sides));
    s = s > 0 ? s : h;
    // Runs: strokes written one after the other, close to each other; small ones (dots, accents) aside
    struct Run {
        std::vector<uint32_t> strokes;
        double angle = 0;  ///< as laid out (0: as on the page)
        bool directed = false;  ///< has a direction (even if 0)
    };
    std::vector<Run> runs;
    std::vector<uint32_t> small;
    std::vector<int> runOf(strokes.size(), -1);
    const InkStroke* previous = nullptr;
    for (uint32_t i = 0; i < strokes.size(); ++i) {
        const InkStroke& st = strokes[i];
        if (st.points.empty() || st.filled) {
            continue;
        }
        const double len = pathLength(st);
        const double chord = st.points.size() > 1 ? std::hypot(st.points.back().x() - st.points.front().x(),
                                                               st.points.back().y() - st.points.front().y())
                                                  : 0;
        if ((len > 4 * u && chord > 0.95 * len) || std::min(st.box.width(), st.box.height()) > 5 * s) {
            continue;  // (a straight line or a big shape: a drawing whichever way)
        }
        if (std::max(st.box.width(), st.box.height()) < 0.5 * h) {
            small.push_back(i);
            continue;
        }
        if (!previous || distance(previous->box, st.box) > 3 * s) {
            runs.push_back({});
        }
        runs.back().strokes.push_back(i);
        runOf[i] = static_cast<int>(runs.size() - 1);
        previous = &st;
    }
    // Their directions; runs of about the same angle are one frame
    struct Group {
        double angle = 0;
        double weight = 0;
        std::vector<size_t> runs;
    };
    std::vector<Group> groups;
    for (size_t r = 0; r < runs.size(); ++r) {
        const auto d = directionOf(strokes, runs[r].strokes, s);
        runs[r].directed = d.has_value();
        runs[r].angle = d ? snapped(d->angle) : 0;
        if (runs[r].angle == 0) {
            continue;
        }
        Group* into = nullptr;
        for (Group& g: groups) {
            const bool right = std::abs(g.angle) == 90 ? g.angle == runs[r].angle
                                                       : std::abs(runs[r].angle) != 90 &&
                                                                 std::abs(g.angle - runs[r].angle) <= 8;
            if (right) {
                into = &g;
                break;
            }
        }
        if (!into) {
            groups.push_back({runs[r].angle, 0, {}});
            into = &groups.back();
        }
        if (std::abs(into->angle) != 90) {
            // (the mean of its runs' angles, by their lengths)
            into->angle = (into->angle * into->weight + runs[r].angle * d->length) / (into->weight + d->length);
        }
        into->weight += d->length;
        into->runs.push_back(r);
    }
    if (groups.empty()) {
        return out;
    }
    // Each frame takes its runs, and the dots and short runs inside the box of one of them (a little bigger)
    std::vector<int> frameOf(strokes.size(), -1);
    std::vector<std::vector<QRectF>> areas(groups.size());  ///< in the frame
    for (size_t g = 0; g < groups.size(); ++g) {
        for (const size_t r: groups[g].runs) {
            QRectF box;
            for (const uint32_t i: runs[r].strokes) {
                frameOf[i] = static_cast<int>(g);
                const QRectF b = turned(strokes[i], -groups[g].angle).box;
                box = box.isNull() ? b : unite(box, b);
            }
            const double w = std::max(box.height(), s);
            areas[g].push_back(box.adjusted(-1.5 * w, -0.25 * w, 1.5 * w, 0.25 * w));
        }
    }
    auto take = [&](uint32_t i) {
        for (size_t g = 0; g < groups.size(); ++g) {
            const QPointF c = ink::turned(strokes[i].box.center(), -groups[g].angle);
            for (const QRectF& a: areas[g]) {
                if (a.contains(c)) {
                    frameOf[i] = static_cast<int>(g);
                    return;
                }
            }
        }
    };
    for (const uint32_t i: small) {
        take(i);
    }
    for (const Run& r: runs) {
        if (!r.directed) {
            for (const uint32_t i: r.strokes) {
                take(i);
            }
        }
    }
    out.resize(groups.size());
    for (size_t g = 0; g < groups.size(); ++g) {
        out[g].angle = groups[g].angle;
    }
    for (uint32_t i = 0; i < strokes.size(); ++i) {
        if (frameOf[i] >= 0) {
            out[static_cast<size_t>(frameOf[i])].strokes.push_back(i);
        }
    }
    return out;
}

Layout layout(const std::vector<InkStroke>& strokes) {
    Layout out;
    if (strokes.empty()) {
        return out;
    }
    const auto [h, u] = unitsOf(strokes, nullptr);
    out.h = h;
    out.u = u;
    const std::vector<Frame> frames = framesOf(strokes, h, u);
    std::vector<uint32_t> upright;  ///< the strokes laid out on the page as they are
    {
        std::vector<bool> framed(strokes.size(), false);
        for (const Frame& f: frames) {
            for (const uint32_t i: f.strokes) {
                framed[i] = true;
            }
        }
        upright.reserve(strokes.size());
        for (uint32_t i = 0; i < strokes.size(); ++i) {
            if (!framed[i]) {
                upright.push_back(i);
            }
        }
    }
    if (!frames.empty()) {
        // (the units of the strokes as they are: those at an angle are as tall as they are long)
        std::tie(out.h, out.u) = unitsOf(strokes, &upright);
    }
    linesOf(strokes, upright, out.h, out.u, out.lines, out.drawings);
    for (const Frame& f: frames) {
        // Turned upright and laid out as a page of their own
        std::vector<InkStroke> turnedStrokes(strokes.size());
        for (const uint32_t i: f.strokes) {
            turnedStrokes[i] = turned(strokes[i], -f.angle);
        }
        const auto [fh, fu] = unitsOf(turnedStrokes, &f.strokes);
        std::vector<InkLine> lines;
        linesOf(turnedStrokes, f.strokes, fh, fu, lines, out.drawings);
        for (InkLine& line: lines) {
            line.angle = f.angle;
            line.box = strokes[line.strokes.front()].box;
            for (const uint32_t i: line.strokes) {
                line.box = unite(line.box, strokes[i].box);
            }
            out.lines.push_back(std::move(line));
        }
    }
    if (!frames.empty()) {
        std::sort(out.drawings.begin(), out.drawings.end());
    }
    std::stable_sort(out.lines.begin(), out.lines.end(),
                     [](const InkLine& a, const InkLine& b) { return a.box.top() < b.box.top(); });
    return out;
}

}  // namespace xqt::hwr
