#include "MultiRecognizer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>

#include "LanguagePlan.h"

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

namespace {
uint32_t bitsOf(size_t n) { return n >= 32 ? ~0u : (1u << n) - 1; }
}  // namespace

float MultiRecognizer::scoreOf(const ink::LineResult& result) {
    if (result.words.empty()) {
        return 0;
    }
    double sum = 0;
    for (const ink::Word& w: result.words) {
        sum += w.conf;
    }
    return static_cast<float>(sum / static_cast<double>(result.words.size()));
}

bool MultiRecognizer::enough(const ink::LineResult& known, const Context& context) const {
    uint32_t able = 0;
    for (size_t i = 0; i < all.size() && i < 32; ++i) {
        if (all[i]->ready()) {
            able |= 1u << i;
        }
    }
    const uint32_t everyone = bitsOf(all.size());
    const uint32_t wanted = (context.plan ? context.plan->first(capabilities().modelLanguages) : everyone) & able;
    const uint32_t have = known.models != 0 ? known.models : everyone;  // (0: read before there were several)
    return (wanted & ~have) == 0;
}

std::optional<ink::LineResult> MultiRecognizer::recognizeLine(const LineInput& line, const Context& context) {
    const std::vector<QStringList> languages = capabilities().modelLanguages;
    const uint32_t everyone = bitsOf(all.size());
    const uint32_t have = context.before ? (context.before->models != 0 ? context.before->models : everyone) : 0;
    std::vector<ink::LineResult> read;
    std::vector<std::optional<float>> scores(all.size());
    uint32_t done = have;
    auto readBy = [&](uint32_t members) -> bool {
        for (size_t i = 0; i < all.size() && i < 32; ++i) {
            const uint32_t bit = 1u << i;
            if (!(members & bit) || (done & bit)) {
                continue;
            }
            if (context.cancelled && context.cancelled()) {
                return false;
            }
            if (!all[i]->ready()) {
                continue;  // (its lines are read when it is: the line does not have its bit)
            }
            auto r = all[i]->recognizeLine(line, context);
            if (!r) {
                if (context.cancelled && context.cancelled()) {
                    return false;
                }
                continue;
            }
            done |= bit;
            scores[i] = scoreOf(*r);
            read.push_back(tagged(std::move(*r), i));
        }
        return true;
    };
    const uint32_t first = context.plan ? context.plan->first(languages) : everyone;
    if (!readBy(first)) {
        return std::nullopt;
    }
    if (context.plan) {
        // How sure the first ones are (read now, or before)
        double sum = 0;
        int n = 0;
        for (size_t i = 0; i < scores.size(); ++i) {
            if (scores[i] && (first & (1u << i))) {
                sum += *scores[i];
                ++n;
            }
        }
        const float sure = n > 0 ? static_cast<float>(sum / n) : context.before ? scoreOf(*context.before) : 1.0f;
        if (context.plan->othersToo(sure) && !readBy(everyone & ~first)) {
            return std::nullopt;
        }
        context.plan->observe(scores, languages);
    }
    if (read.empty()) {
        return context.before ? std::optional<ink::LineResult>(*context.before) : std::nullopt;
    }
    std::vector<const ink::LineResult*> results;
    if (context.before) {
        results.push_back(context.before);
    }
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
