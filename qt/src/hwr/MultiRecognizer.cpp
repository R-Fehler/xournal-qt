#include "MultiRecognizer.h"

#include <algorithm>
#include <map>

namespace xqt::hwr {

MultiRecognizer::MultiRecognizer(std::vector<std::shared_ptr<Recognizer>> members): all(std::move(members)) {
    all.erase(std::remove(all.begin(), all.end(), nullptr), all.end());
}

Capabilities MultiRecognizer::capabilities() const {
    Capabilities c;
    QStringList ids;
    for (const auto& r: all) {
        const Capabilities m = r->capabilities();
        ids << m.id;
        c.strokes = c.strokes || m.strokes;
        for (const QString& l: m.languages) {
            if (!c.languages.contains(l)) {
                c.languages << l;
            }
        }
        c.topK = std::max(c.topK, m.topK);
    }
    c.id = ids.join(u'+');
    return c;
}

bool MultiRecognizer::ready(QString* why) const {
    QString first;
    for (const auto& r: all) {
        QString w;
        if (r->ready(&w)) {
            return true;
        }
        if (first.isEmpty()) {
            first = w;
        }
    }
    if (why) {
        *why = all.empty() ? QStringLiteral("No model") : first;
    }
    return false;
}

std::optional<ink::LineResult> MultiRecognizer::recognizeLine(const LineInput& line, const Context& context) {
    std::vector<std::optional<ink::LineResult>> read(all.size());
    bool any = false;
    for (size_t i = 0; i < all.size(); ++i) {
        if (context.cancelled && context.cancelled()) {
            return std::nullopt;
        }
        if (!all[i]->ready()) {
            continue;
        }
        read[i] = all[i]->recognizeLine(line, context);
        if (!read[i] && context.cancelled && context.cancelled()) {
            return std::nullopt;
        }
        any = any || read[i].has_value();
    }
    if (!any) {
        return std::nullopt;
    }
    std::vector<const ink::LineResult*> results;
    for (const auto& r: read) {
        results.push_back(r ? &*r : nullptr);
    }
    return merge(results, capabilities().topK);
}

ink::LineResult MultiRecognizer::merge(const std::vector<const ink::LineResult*>& results, int topK) {
    // Per word box (the members align their readings to the same boxes of the layout)
    struct Box {
        QRectF box;
        std::vector<const ink::Word*> words;
    };
    std::vector<Box> boxes;
    for (const ink::LineResult* r: results) {
        if (!r) {
            continue;
        }
        for (const ink::Word& w: r->words) {
            auto it = std::find_if(boxes.begin(), boxes.end(), [&](const Box& b) {
                return std::abs(b.box.left() - w.box.left()) < 0.05 && std::abs(b.box.right() - w.box.right()) < 0.05;
            });
            if (it == boxes.end()) {
                boxes.push_back({w.box, {}});
                it = std::prev(boxes.end());
            }
            it->words.push_back(&w);
        }
    }
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.box.left() < b.box.left(); });
    ink::LineResult out;
    for (const Box& b: boxes) {
        ink::Word w;
        w.box = b.box;
        std::map<words::Id, float> best;  ///< a reading's share: the most any model gave it
        for (const ink::Word* m: b.words) {
            w.conf = std::max(w.conf, m->conf);
            for (const ink::Candidate& c: m->candidates) {
                float& p = best[c.word];
                p = std::max(p, c.p);
            }
        }
        for (const auto& [id, p]: best) {
            w.candidates.push_back({id, p});
        }
        std::stable_sort(w.candidates.begin(), w.candidates.end(),
                         [](const ink::Candidate& x, const ink::Candidate& y) { return x.p > y.p; });
        if (w.candidates.size() > static_cast<size_t>(std::max(1, topK))) {
            w.candidates.resize(static_cast<size_t>(std::max(1, topK)));
        }
        // The best reading as one of the models wrote it
        for (const ink::Word* m: b.words) {
            if (!w.candidates.empty() && !m->candidates.empty() && m->candidates.front().word == w.candidates.front().word) {
                w.text = m->text;
                break;
            }
        }
        if (w.text.isEmpty() && !w.candidates.empty()) {
            w.text = words::textOf(w.candidates.front().word);
        }
        out.words.push_back(std::move(w));
    }
    return out;
}

void MultiRecognizer::unload() {
    for (const auto& r: all) {
        r->unload();
    }
}

void MultiRecognizer::interrupt() {
    for (const auto& r: all) {
        r->interrupt();
    }
}

}  // namespace xqt::hwr
