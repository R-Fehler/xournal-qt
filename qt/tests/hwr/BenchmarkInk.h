/*
 * xournal-qt: real handwriting for the tests of handwriting at an angle (InkRotationTest, RotationBenchmarkTest): the
 * lines of upstream's handwriting benchmark (test/files/benchmark/handwritten-text.xopp, "This is a dumb test, written
 * many times..." twice per line) that stand alone, turned around their middle or written one word below the other.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <set>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "hwr/InkLayout.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/InkText.h"

#include "config-test.h"

namespace xqt::test {

/// The pen strokes of the benchmark's page, each once (the file has each stroke about five times, over itself)
inline std::vector<hwr::InkStroke> benchmarkInk() {
    std::vector<hwr::InkStroke> out;
    auto r = DocumentSession::loadFile(GET_TESTFILE(u8"benchmark/handwritten-text.xopp"));
    if (!r.document) {
        return out;
    }
    std::shared_lock lock(*r.document);
    std::set<std::vector<std::pair<double, double>>> seen;
    for (hwr::InkStroke& s: hwr::strokesOf(*r.document->getPage(0))) {
        std::vector<std::pair<double, double>> key;
        for (const QPointF& p: s.points) {
            key.emplace_back(p.x(), p.y());
        }
        if (seen.insert(std::move(key)).second) {
            out.push_back(std::move(s));
        }
    }
    return out;
}

/// The benchmark's lines that stand alone (most of its lines are written over each other): of 10 words or more and
/// not higher than a line, each once (the page has some of them several times); their strokes in the order of writing
inline std::vector<std::vector<hwr::InkStroke>> benchmarkLines() {
    const std::vector<hwr::InkStroke> page = benchmarkInk();
    const hwr::Layout level = hwr::layout(page);
    std::vector<std::vector<hwr::InkStroke>> lines;
    std::set<quint64> hashes;
    for (const hwr::InkLine& line: level.lines) {
        if (line.words.size() >= 10 && line.box.height() <= 30 && hashes.insert(line.hash).second) {
            std::vector<hwr::InkStroke> s;
            for (const uint32_t i: line.strokes) {
                s.push_back(page[i]);
            }
            lines.push_back(std::move(s));
        }
    }
    return lines;
}

/// The strokes turned by `degrees` (clockwise) around the middle of the box around them
inline std::vector<hwr::InkStroke> turnedAround(const std::vector<hwr::InkStroke>& strokes, double degrees) {
    QRectF box;
    for (const hwr::InkStroke& s: strokes) {
        box = box.isNull() ? s.box : box.united(s.box);
    }
    const QPointF c = box.center();
    std::vector<hwr::InkStroke> out;
    for (const hwr::InkStroke& s: strokes) {
        std::vector<QPointF> pts;
        for (const QPointF& p: s.points) {
            pts.push_back(c + ink::turned(p - c, degrees));
        }
        hwr::InkStroke t = hwr::InkStroke::of(std::move(pts), s.width, s.widths);
        t.filled = s.filled;
        out.push_back(std::move(t));
    }
    return out;
}

/// The words of a line written one below the other, left aligned, `pitch` times the line's height apart (a list), the
/// strokes of each word in the order they were written
inline std::vector<hwr::InkStroke> listOf(const std::vector<hwr::InkStroke>& line, double pitch) {
    const hwr::Layout l = hwr::layout(line);
    std::vector<hwr::InkStroke> list;
    if (l.lines.empty()) {
        return list;
    }
    const double step = pitch * l.lines[0].box.height();
    for (size_t w = 0; w < l.lines[0].words.size(); ++w) {
        const hwr::InkWordBox& word = l.lines[0].words[w];
        std::vector<uint32_t> which = word.strokes;
        std::sort(which.begin(), which.end());
        for (const uint32_t i: which) {
            std::vector<QPointF> pts = line[i].points;
            for (QPointF& p: pts) {
                p += QPointF(100 - word.box.left(), 100 + step * static_cast<double>(w) - l.lines[0].box.top());
            }
            list.push_back(hwr::InkStroke::of(std::move(pts), line[i].width, line[i].widths));
        }
    }
    return list;
}

}  // namespace xqt::test
