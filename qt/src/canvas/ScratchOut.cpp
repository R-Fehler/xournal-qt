#include "ScratchOut.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"

namespace xqt::scratchout {

namespace {
constexpr double degrees(double rad) { return rad * 180.0 / M_PI; }

/// Do the segments ab and cd cross (or touch)?
bool segmentsCross(const Point& a, const Point& b, const Point& c, const Point& d) {
    auto orient = [](const Point& p, const Point& q, const Point& r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };
    const double d1 = orient(c, d, a), d2 = orient(c, d, b), d3 = orient(a, b, c), d4 = orient(a, b, d);
    return ((d1 > 0) != (d2 > 0) || d1 == 0 || d2 == 0) && ((d3 > 0) != (d4 > 0) || d3 == 0 || d4 == 0) &&
           std::min(a.x, b.x) <= std::max(c.x, d.x) && std::min(c.x, d.x) <= std::max(a.x, b.x) &&
           std::min(a.y, b.y) <= std::max(c.y, d.y) && std::min(c.y, d.y) <= std::max(a.y, b.y);
}

double distanceToSegment(const Point& p, const Point& a, const Point& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}
}  // namespace

Shape analyse(const std::vector<Point>& pts) {
    Shape s;
    const size_t n = pts.size();
    if (n < 2) {
        return s;
    }
    // The main axis (principal component of the points)
    for (const Point& p: pts) {
        s.cx += p.x;
        s.cy += p.y;
    }
    s.cx /= static_cast<double>(n);
    s.cy /= static_cast<double>(n);
    double sxx = 0, syy = 0, sxy = 0;
    for (const Point& p: pts) {
        sxx += (p.x - s.cx) * (p.x - s.cx);
        syy += (p.y - s.cy) * (p.y - s.cy);
        sxy += (p.x - s.cx) * (p.y - s.cy);
    }
    s.angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
    const double ux = std::cos(s.angle), uy = std::sin(s.angle);
    std::vector<double> u(n), v(n);
    s.uMin = s.vMin = std::numeric_limits<double>::max();
    s.uMax = s.vMax = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < n; ++i) {
        const double dx = pts[i].x - s.cx, dy = pts[i].y - s.cy;
        u[i] = dx * ux + dy * uy;
        v[i] = -dx * uy + dy * ux;
        s.uMin = std::min(s.uMin, u[i]);
        s.uMax = std::max(s.uMax, u[i]);
        s.vMin = std::min(s.vMin, v[i]);
        s.vMax = std::max(s.vMax, v[i]);
        if (i > 0) {
            s.length += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        }
    }
    s.width = s.uMax - s.uMin;
    if (s.width <= 0) {
        return s;
    }

    // The turns: where it goes back along the axis by at least TURN_FRACTION of the width
    const double h = TURN_FRACTION * s.width;
    std::vector<size_t> turns;
    int dir = 0;
    size_t lo = 0, hi = 0, ext = 0;
    for (size_t i = 1; i < n; ++i) {
        if (dir == 0) {
            lo = u[i] < u[lo] ? i : lo;
            hi = u[i] > u[hi] ? i : hi;
            if (u[i] - u[lo] >= h) {
                turns.push_back(lo);
                dir = 1;
                ext = i;
            } else if (u[hi] - u[i] >= h) {
                turns.push_back(hi);
                dir = -1;
                ext = i;
            }
        } else if (dir > 0) {
            if (u[i] >= u[ext]) {
                ext = i;
            } else if (u[ext] - u[i] >= h) {
                turns.push_back(ext);
                dir = -1;
                ext = i;
            }
        } else {
            if (u[i] <= u[ext]) {
                ext = i;
            } else if (u[i] - u[ext] >= h) {
                turns.push_back(ext);
                dir = 1;
                ext = i;
            }
        }
    }
    if (dir != 0) {
        turns.push_back(ext);
    }
    if (turns.size() < 2) {
        return s;
    }
    s.reversals = static_cast<int>(turns.size()) - 2;

    // The sweeps between the turns: their direction, how straight, how far
    double path = 0, along = 0;
    std::vector<double> shares;
    for (size_t k = 0; k + 1 < turns.size(); ++k) {
        const size_t a = turns[k], b = turns[k + 1];
        const double du = std::abs(u[b] - u[a]), dv = std::abs(v[b] - v[a]);
        s.steepest = std::max(s.steepest, std::atan2(dv, du));
        for (size_t i = a + 1; i <= b; ++i) {
            path += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        }
        along += du;
        shares.push_back(du / s.width);
    }
    s.straightness = along > 0 ? path / along : 1e9;
    std::sort(shares.begin(), shares.end());
    s.sweepShare = shares[shares.size() / 2];

    s.zigzag = n >= MIN_POINTS && s.width >= MIN_WIDTH && std::abs(degrees(s.angle)) <= MAX_AXIS_DEGREES &&
               s.reversals >= MIN_REVERSALS && degrees(s.steepest) <= MAX_SWEEP_DEGREES &&
               s.straightness <= MAX_STRAIGHTNESS && s.sweepShare >= MIN_SWEEP_SHARE;
    return s;
}

bool quick(double length, double durationMs, double zoom) {
    if (durationMs <= 0) {
        return true;
    }
    return length * zoom / (durationMs / 1000.0) >= MIN_SPEED_PX_PER_S;
}

std::vector<const Element*> covered(const Layer& layer, const std::vector<Point>& zigzag, const Shape& shape,
                                    double zigzagWidth) {
    std::vector<const Element*> out;
    if (zigzag.size() < 2) {
        return out;
    }
    const double ux = std::cos(shape.angle), uy = std::sin(shape.angle);
    // The band: the zigzag's extent along and across its axis, a little wider (its ink, a letter's edge)
    const double margin = 0.5 * zigzagWidth + 0.1 * (shape.vMax - shape.vMin);
    auto inBand = [&](const Point& p) {
        const double dx = p.x - shape.cx, dy = p.y - shape.cy;
        const double pu = dx * ux + dy * uy, pv = -dx * uy + dy * ux;
        return pu >= shape.uMin - margin && pu <= shape.uMax + margin && pv >= shape.vMin - margin &&
               pv <= shape.vMax + margin;
    };
    double x0 = zigzag.front().x, y0 = zigzag.front().y, x1 = x0, y1 = y0;
    for (const Point& p: zigzag) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    x0 -= margin;
    y0 -= margin;
    x1 += margin;
    y1 += margin;

    for (const Element* e: layer.getElementsView()) {
        if (e->getType() != ELEMENT_STROKE) {
            continue;  // (strokes only: typed text, pictures and the rest stay)
        }
        const auto* stroke = static_cast<const Stroke*>(e);
        const auto box = stroke->getBoundingBox();
        if (box.x > x1 || box.x + box.width < x0 || box.y > y1 || box.y + box.height < y0) {
            continue;
        }
        const auto& pts = stroke->getPointVector();
        if (pts.empty()) {
            continue;
        }
        const auto inside = std::count_if(pts.begin(), pts.end(), inBand);
        if (static_cast<double>(inside) < MIN_COVERED * static_cast<double>(pts.size())) {
            continue;
        }
        // The zigzag crosses it, or touches it (a dot, a short tick: the two inks overlap)
        const double touch = 0.5 * (zigzagWidth + stroke->getWidth());
        bool crosses = false;
        for (size_t i = 1; i < zigzag.size() && !crosses; ++i) {
            const Point& a = zigzag[i - 1];
            const Point& b = zigzag[i];
            if (std::max(a.x, b.x) < box.x - touch || std::min(a.x, b.x) > box.x + box.width + touch ||
                std::max(a.y, b.y) < box.y - touch || std::min(a.y, b.y) > box.y + box.height + touch) {
                continue;
            }
            for (size_t j = 0; j < pts.size() && !crosses; ++j) {
                crosses = (j > 0 && segmentsCross(a, b, pts[j - 1], pts[j])) ||
                          distanceToSegment(pts[j], a, b) <= touch;
            }
        }
        if (crosses) {
            out.push_back(e);
        }
    }
    return out;
}

}  // namespace xqt::scratchout
