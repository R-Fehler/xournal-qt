#include "WordAlignment.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <QStringList>

namespace xqt::hwr {

std::vector<QString> align(const QString& reading, const std::vector<QRectF>& boxes) {
    std::vector<QString> out(boxes.size());
    const QStringList tokens = reading.split(u' ', Qt::SkipEmptyParts);
    if (boxes.empty() || tokens.isEmpty()) {
        return out;
    }
    if (static_cast<size_t>(tokens.size()) == boxes.size()) {
        for (size_t i = 0; i < boxes.size(); ++i) {
            out[i] = tokens[static_cast<qsizetype>(i)];
        }
        return out;
    }
    // Shares of the width: the boxes by their widths, the words by their letters
    std::vector<double> boxEnds;
    double total = 0;
    for (const QRectF& b: boxes) {
        total += std::max(b.width(), 1e-6);
        boxEnds.push_back(total);
    }
    double letters = 0;
    for (const QString& t: tokens) {
        letters += static_cast<double>(t.size());
    }
    double at = 0;
    for (const QString& t: tokens) {
        const double from = at / letters, to = (at + static_cast<double>(t.size())) / letters;
        at += static_cast<double>(t.size());
        size_t best = 0;
        double bestOverlap = -1;
        double start = 0;
        for (size_t i = 0; i < boxes.size(); ++i) {
            const double a = start / total, b = boxEnds[i] / total;
            start = boxEnds[i];
            const double overlap = std::min(to, b) - std::max(from, a);
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                best = i;
            }
        }
        out[best] += out[best].isEmpty() ? t : u' ' + t;
    }
    return out;
}

std::vector<ink::Word> wordsOf(const std::vector<Beam>& beams, const std::vector<QRectF>& boxes, int topK) {
    std::vector<ink::Word> out;
    if (beams.empty() || boxes.empty()) {
        return out;
    }
    // The beams' shares
    double best = beams.front().logProb;
    for (const Beam& b: beams) {
        best = std::max(best, b.logProb);
    }
    std::vector<double> share;
    double sum = 0;
    for (const Beam& b: beams) {
        share.push_back(std::exp(b.logProb - best));
        sum += share.back();
    }
    struct Reading {
        QString text;  ///< as read by the likeliest beam that read it
        double p = 0;
        size_t order = 0;
    };
    std::vector<std::map<QString, Reading>> perBox(boxes.size());  ///< by its letters (folded)
    for (size_t b = 0; b < beams.size(); ++b) {
        const std::vector<QString> words = align(beams[b].text, boxes);
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (words[i].isEmpty()) {
                continue;
            }
            const QString key = ink::folded(words[i]);
            Reading& r = perBox[i][key.isEmpty() ? words[i] : key];
            if (r.text.isEmpty()) {
                r.text = words[i];
                r.order = b;
            }
            r.p += share[b] / sum;
        }
    }
    const float conf = static_cast<float>(std::exp(std::min(0.0, beams.front().tokenLogProb)));
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (perBox[i].empty()) {
            continue;
        }
        std::vector<Reading> list;
        for (auto& [key, r]: perBox[i]) {
            list.push_back(r);
        }
        std::sort(list.begin(), list.end(), [](const Reading& a, const Reading& b) {
            return a.p != b.p ? a.p > b.p : a.order < b.order;
        });
        if (list.size() > static_cast<size_t>(std::max(1, topK))) {
            list.resize(static_cast<size_t>(std::max(1, topK)));
        }
        ink::Word w;
        w.box = boxes[i];
        w.conf = conf;
        w.text = list.front().text;
        for (const Reading& r: list) {
            w.candidates.push_back(ink::candidate(r.text, static_cast<float>(r.p)));
        }
        out.push_back(std::move(w));
    }
    return out;
}

}  // namespace xqt::hwr
