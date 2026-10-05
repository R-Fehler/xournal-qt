#include "InkCopy.h"

#include <algorithm>
#include <cmath>

#include <QLineF>
#include <QPolygonF>
#include <QStringList>

namespace xqt::hwr {

namespace {

struct Line {
    std::vector<CopiedWord> words;
    double middle = 0;  ///< the mean middle of its words
    double height = 0;  ///< their mean height
    double top = 0, bottom = 0;
    void add(CopiedWord w) {
        const double n = static_cast<double>(words.size());
        middle = (middle * n + w.box.center().y()) / (n + 1);
        height = (height * n + w.box.height()) / (n + 1);
        top = words.empty() ? w.box.top() : std::min(top, w.box.top());
        bottom = words.empty() ? w.box.bottom() : std::max(bottom, w.box.bottom());
        words.push_back(std::move(w));
    }
    /// How much of the smaller height a word overlaps the line's band
    double overlap(const QRectF& box) const {
        const double a = std::max(middle - height / 2, box.top());
        const double b = std::min(middle + height / 2, box.bottom());
        const double smaller = std::max(1e-6, std::min(height, box.height()));
        return (b - a) / smaller;
    }
};

bool touches(const QRectF& box, const QLineF& segment) {
    if (box.contains(segment.p1()) || box.contains(segment.p2())) {
        return true;
    }
    const QLineF edges[4] = {{box.topLeft(), box.topRight()},
                             {box.topRight(), box.bottomRight()},
                             {box.bottomRight(), box.bottomLeft()},
                             {box.bottomLeft(), box.topLeft()}};
    for (const QLineF& e: edges) {
        if (segment.intersects(e, nullptr) == QLineF::BoundedIntersection) {
            return true;
        }
    }
    return false;
}

CopiedWord copied(const ink::Word& w) { return {w.text, w.conf, w.box}; }

}  // namespace

CopiedText inReadingOrder(std::vector<CopiedWord> words) {
    CopiedText out;
    words.erase(std::remove_if(words.begin(), words.end(),
                               [](const CopiedWord& w) { return w.text.trimmed().isEmpty(); }),
                words.end());
    // From the top: each word joins the line it overlaps most (by half of the smaller height at least)
    std::stable_sort(words.begin(), words.end(), [](const CopiedWord& a, const CopiedWord& b) {
        return a.box.center().y() < b.box.center().y();
    });
    std::vector<Line> lines;
    for (CopiedWord& w: words) {
        Line* best = nullptr;
        double most = 0.5;
        for (Line& l: lines) {
            if (const double o = l.overlap(w.box); o >= most) {
                most = o;
                best = &l;
            }
        }
        if (!best) {
            lines.emplace_back();
            best = &lines.back();
        }
        best->add(std::move(w));
    }
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.middle < b.middle; });
    // The usual gap between two lines here (the lower median): a paragraph is a gap well above it
    std::vector<double> gaps;
    for (size_t i = 1; i < lines.size(); ++i) {
        gaps.push_back(lines[i].top - lines[i - 1].bottom);
    }
    std::sort(gaps.begin(), gaps.end());
    const double usualGap = gaps.empty() ? 0 : gaps[(gaps.size() - 1) / 2];
    QStringList text;
    for (size_t i = 0; i < lines.size(); ++i) {
        Line& l = lines[i];
        std::stable_sort(l.words.begin(), l.words.end(),
                         [](const CopiedWord& a, const CopiedWord& b) { return a.box.left() < b.box.left(); });
        if (i > 0) {
            const Line& before = lines[i - 1];
            const double gap = l.top - before.bottom;
            if (gap > 1.5 * std::max(l.height, before.height) && gap > 2 * std::max(0.0, usualGap)) {
                text << QString();  // (a paragraph)
            }
        }
        QStringList line;
        for (const CopiedWord& w: l.words) {
            line << w.text.trimmed();
            out.box = out.box.isNull() ? w.box : out.box.united(w.box);
            ++out.words;
            out.unsure += w.unsure() ? 1 : 0;
        }
        text << line.join(u' ');
        out.lines.push_back(std::move(l.words));
    }
    out.text = text.join(u'\n');
    return out;
}

std::vector<CopiedWord> sweptWords(const ink::PageText& ink, const std::vector<QPointF>& path, double reach) {
    std::vector<CopiedWord> out;
    if (path.empty()) {
        return out;
    }
    QPolygonF loop;
    for (const QPointF& p: path) {
        loop << p;
    }
    for (const ink::Word& w: ink.words) {
        const QRectF box = w.box.adjusted(-reach, -reach, reach, reach);
        bool taken = path.size() >= 3 && loop.containsPoint(w.box.center(), Qt::OddEvenFill);
        if (path.size() == 1) {
            taken = box.contains(path.front());
        }
        for (size_t i = 1; i < path.size() && !taken; ++i) {
            taken = touches(box, QLineF(path[i - 1], path[i]));
        }
        if (taken) {
            out.push_back(copied(w));
        }
    }
    return out;
}

std::vector<CopiedWord> allWords(const ink::PageText& ink) {
    std::vector<CopiedWord> out;
    out.reserve(ink.words.size());
    for (const ink::Word& w: ink.words) {
        out.push_back(copied(w));
    }
    return out;
}

}  // namespace xqt::hwr
