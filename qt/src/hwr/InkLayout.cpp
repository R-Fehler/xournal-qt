#include "InkLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"

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

std::vector<InkStroke> strokesOf(const XojPage& page) {
    std::vector<InkStroke> out;
    for (const Layer* layer: page.getLayersView()) {
        if (!layer->isVisible()) {
            continue;
        }
        for (const auto& e: layer->getElementsView()) {
            if (e->getType() != ELEMENT_STROKE) {
                continue;
            }
            const auto* stroke = static_cast<const Stroke*>(e);
            if (stroke->getToolType() != StrokeTool::PEN || stroke->getPointCount() == 0) {
                continue;
            }
            const std::vector<Point>& pts = stroke->getPointVector();
            std::vector<QPointF> points;
            points.reserve(pts.size());
            std::vector<float> widths;
            const bool pressure = std::any_of(pts.begin(), pts.end(), [](const Point& p) {
                return p.z != Point::NO_PRESSURE;
            });
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
            out.push_back(std::move(s));
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

Layout layout(const std::vector<InkStroke>& strokes) {
    Layout out;
    if (strokes.empty()) {
        return out;
    }
    std::vector<double> heights;
    heights.reserve(strokes.size());
    for (const InkStroke& s: strokes) {
        heights.push_back(s.box.height());
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
    out.h = h;
    out.u = u;

    // Lines in the order of writing
    struct Group {
        std::vector<uint32_t> strokes;
        std::vector<double> middles;  ///< of its strokes that are not small
    };
    std::vector<Group> groups;
    const InkStroke* previous = nullptr;
    for (uint32_t i = 0; i < strokes.size(); ++i) {
        const InkStroke& s = strokes[i];
        if (s.points.empty() || isDrawing(s, u)) {
            out.drawings.push_back(i);
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
        line.hash = hashOf(strokes, line.strokes, line.origin());
        out.lines.push_back(std::move(line));
    }
    std::stable_sort(out.lines.begin(), out.lines.end(),
                     [](const InkLine& a, const InkLine& b) { return a.box.top() < b.box.top(); });
    return out;
}

}  // namespace xqt::hwr
