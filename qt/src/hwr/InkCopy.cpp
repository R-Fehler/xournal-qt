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
    double angle = 0;
    QRectF bounds;  ///< on the page
    /// `box`: the word's box in the frame of its angle
    void add(CopiedWord w, const QRectF& box) {
        const double n = static_cast<double>(words.size());
        middle = (middle * n + box.center().y()) / (n + 1);
        height = (height * n + box.height()) / (n + 1);
        top = words.empty() ? box.top() : std::min(top, box.top());
        bottom = words.empty() ? box.bottom() : std::max(bottom, box.bottom());
        angle = w.angle;
        bounds = words.empty() ? w.bounds() : bounds.united(w.bounds());
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

CopiedWord copied(const ink::Word& w) { return {w.text, w.conf, w.box, w.angle}; }

/// A word's box in its line's frame (the page turned by -angle around (0, 0))
QRectF framed(const CopiedWord& w) {
    QRectF b = w.box;
    if (w.angle != 0) {
        b.moveCenter(ink::turned(w.box.center(), -w.angle));
    }
    return b;
}

/// Lines of these words by their heights (in their frame), from the top
std::vector<Line> linesOf(std::vector<CopiedWord> words) {
    std::stable_sort(words.begin(), words.end(), [](const CopiedWord& a, const CopiedWord& b) {
        return framed(a).center().y() < framed(b).center().y();
    });
    std::vector<Line> lines;
    for (CopiedWord& w: words) {
        const QRectF box = framed(w);
        Line* best = nullptr;
        double most = 0.5;
        for (Line& l: lines) {
            if (const double o = l.overlap(box); o >= most) {
                most = o;
                best = &l;
            }
        }
        if (!best) {
            lines.emplace_back();
            best = &lines.back();
        }
        best->add(std::move(w), box);
    }
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.middle < b.middle; });
    for (Line& l: lines) {
        std::stable_sort(l.words.begin(), l.words.end(), [](const CopiedWord& a, const CopiedWord& b) {
            return framed(a).left() < framed(b).left();
        });
    }
    return lines;
}

}  // namespace

QRectF CopiedWord::bounds() const { return angle == 0 ? box : ink::quadOf(box, angle).boundingRect(); }

CopiedText inReadingOrder(std::vector<CopiedWord> words) {
    CopiedText out;
    words.erase(std::remove_if(words.begin(), words.end(),
                               [](const CopiedWord& w) { return w.text.trimmed().isEmpty(); }),
                words.end());
    // From the top: each word joins the line it overlaps most (by half of the smaller height at least); words at an
    // angle in the frame of their angle
    std::vector<CopiedWord> level;
    std::vector<std::pair<float, std::vector<CopiedWord>>> turned;
    for (CopiedWord& w: words) {
        if (w.angle == 0) {
            level.push_back(std::move(w));
            continue;
        }
        auto it = std::find_if(turned.begin(), turned.end(), [&](const auto& t) { return t.first == w.angle; });
        if (it == turned.end()) {
            turned.emplace_back(w.angle, std::vector<CopiedWord>());
            it = turned.end() - 1;
        }
        it->second.push_back(std::move(w));
    }
    std::vector<Line> lines = linesOf(std::move(level));
    // The usual gap between two lines here (the lower median): a paragraph is a gap well above it
    std::vector<double> gaps;
    for (size_t i = 1; i < lines.size(); ++i) {
        gaps.push_back(lines[i].top - lines[i - 1].bottom);
    }
    std::sort(gaps.begin(), gaps.end());
    const double usualGap = gaps.empty() ? 0 : gaps[(gaps.size() - 1) / 2];
    // Lines at an angle among them, by the top of their box on the page
    for (auto& [angle, list]: turned) {
        for (Line& l: linesOf(std::move(list))) {
            const double top = l.bounds.top();
            auto at = std::find_if(lines.begin(), lines.end(),
                                   [&](const Line& o) { return (o.angle == 0 ? o.middle : o.bounds.top()) > top; });
            lines.insert(at, std::move(l));
        }
    }
    QStringList text;
    for (size_t i = 0; i < lines.size(); ++i) {
        Line& l = lines[i];
        if (i > 0) {
            const Line& before = lines[i - 1];
            const double gap = l.top - before.bottom;
            if (l.angle != 0 || before.angle != 0) {
                text << QString();  // (a line at an angle: a paragraph of its own)
            } else if (gap > 1.5 * std::max(l.height, before.height) && gap > 2 * std::max(0.0, usualGap)) {
                text << QString();  // (a paragraph)
            }
        }
        QStringList line;
        for (const CopiedWord& w: l.words) {
            line << w.text.trimmed();
            out.box = out.box.isNull() ? w.bounds() : out.box.united(w.bounds());
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
    std::vector<QPointF> turnedPath;
    for (const ink::Word& w: ink.words) {
        const QRectF box = w.box.adjusted(-reach, -reach, reach, reach);
        bool taken = path.size() >= 3 && loop.containsPoint(w.box.center(), Qt::OddEvenFill);
        // (a word at an angle: the path turned into its frame, around its middle)
        const std::vector<QPointF>* p = &path;
        if (w.angle != 0) {
            turnedPath.clear();
            const QPointF c = w.box.center();
            for (const QPointF& q: path) {
                turnedPath.push_back(c + ink::turned(q - c, -w.angle));
            }
            p = &turnedPath;
        }
        if (p->size() == 1) {
            taken = box.contains(p->front());
        }
        for (size_t i = 1; i < p->size() && !taken; ++i) {
            taken = touches(box, QLineF((*p)[i - 1], (*p)[i]));
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
