#include "MultiRecognizer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>

namespace xqt::hwr {

MultiRecognizer::MultiRecognizer(std::vector<std::shared_ptr<Recognizer>> members): all(std::move(members)) {
    all.erase(std::remove(all.begin(), all.end(), nullptr), all.end());
}

Capabilities MultiRecognizer::capabilities() const {
    Capabilities c;
    for (const auto& r: all) {
        const Capabilities m = r->capabilities();
        c.models << m.id;
        c.modelLanguages.push_back(m.languages);
        c.strokes = c.strokes || m.strokes;
        for (const QString& l: m.languages) {
            if (!c.languages.contains(l)) {
                c.languages << l;
            }
        }
        c.topK = std::max(c.topK, m.topK);
    }
    c.id = c.models.join(u'+');
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
    std::vector<ink::LineResult> read;
    for (size_t i = 0; i < all.size(); ++i) {
        if (context.cancelled && context.cancelled()) {
            return std::nullopt;
        }
        if (!all[i]->ready()) {
            continue;  // (its lines are read when it is: the line does not have its bit)
        }
        auto r = all[i]->recognizeLine(line, context);
        if (!r) {
            if (context.cancelled && context.cancelled()) {
                return std::nullopt;
            }
            continue;
        }
        read.push_back(tagged(std::move(*r), i));
    }
    if (read.empty()) {
        return std::nullopt;
    }
    std::vector<const ink::LineResult*> results;
    for (const auto& r: read) {
        results.push_back(&r);
    }
    return merge(results, capabilities().topK);
}

ink::LineResult MultiRecognizer::tagged(ink::LineResult result, size_t member) {
    const auto bit = static_cast<uint8_t>(member < 8 ? 1u << member : 0u);
    result.models = bit;
    for (ink::Word& w: result.words) {
        for (ink::Candidate& c: w.candidates) {
            c.models = bit;
        }
    }
    return result;
}

ink::LineResult MultiRecognizer::merge(const std::vector<const ink::LineResult*>& results, int topK) {
    ink::LineResult out;
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
        out.models |= r->models;
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
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.box.left() < b.box.left(); });
    for (const Box& b: boxes) {
        ink::Word w;
        w.box = b.box;
        struct Reading {
            double miss = 1;  ///< the probability that none of the models that gave it is right
            uint8_t models = 0;
            size_t order = 0;  ///< (ties: as the first model listed it)
        };
        std::map<words::Id, Reading> readings;
        size_t order = 0;
        int sources = 0;
        for (const ink::Word* m: b.words) {
            w.conf = std::max(w.conf, m->conf);
            uint8_t bits = 0;
            for (const ink::Candidate& c: m->candidates) {
                auto [it, fresh] = readings.try_emplace(c.word);
                if (fresh) {
                    it->second.order = order;
                }
                ++order;
                it->second.miss *= 1.0 - std::clamp(static_cast<double>(c.p), 0.0, 1.0);
                it->second.models |= c.models;
                bits |= c.models;
            }
            sources += std::max(1, std::popcount(static_cast<unsigned>(bits)));
        }
        for (const auto& [id, r]: readings) {
            w.candidates.push_back({id, static_cast<float>(1.0 - r.miss), r.models});
        }
        std::stable_sort(w.candidates.begin(), w.candidates.end(), [&](const ink::Candidate& x, const ink::Candidate& y) {
            return x.p != y.p ? x.p > y.p : readings[x.word].order < readings[y.word].order;
        });
        const size_t keep = static_cast<size_t>(std::max(1, topK)) * static_cast<size_t>(std::max(1, sources));
        if (w.candidates.size() > keep) {
            w.candidates.resize(keep);
        }
        // The best reading as the model that gave it wrote it
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
