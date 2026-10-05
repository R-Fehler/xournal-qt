#include "CtcDecode.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include <QFile>

namespace xqt::hwr {

namespace {
constexpr double NONE = -std::numeric_limits<double>::infinity();
constexpr size_t PRUNE = 8;  ///< classes tried per frame (besides the blank)

double add(double a, double b) {
    if (a == NONE) {
        return b;
    }
    if (b == NONE) {
        return a;
    }
    const double m = std::max(a, b);
    return m + std::log(std::exp(a - m) + std::exp(b - m));
}

struct Probs {
    double blank = NONE;    ///< paths ending in a blank
    double nonBlank = NONE; ///< paths ending in the prefix's last character
    double total() const { return add(blank, nonBlank); }
};
}  // namespace

std::vector<CtcReading> ctcBeamSearch(const std::vector<float>& logits, size_t frames, size_t classes, int blank,
                                      int width, int topK) {
    std::vector<CtcReading> out;
    if (frames == 0 || classes == 0 || logits.size() < frames * classes || blank < 0 ||
        static_cast<size_t>(blank) >= classes) {
        return out;
    }
    width = std::max(1, width);
    std::map<std::vector<int>, Probs> beams;
    beams[{}] = Probs{0.0, NONE};
    std::vector<double> row(classes);
    std::vector<size_t> order(classes);
    for (size_t t = 0; t < frames; ++t) {
        // The frame as log-probabilities
        const float* in = logits.data() + t * classes;
        const double top = *std::max_element(in, in + classes);
        double sum = 0;
        for (size_t c = 0; c < classes; ++c) {
            sum += std::exp(static_cast<double>(in[c]) - top);
        }
        const double norm = top + std::log(sum);
        for (size_t c = 0; c < classes; ++c) {
            row[c] = static_cast<double>(in[c]) - norm;
            order[c] = c;
        }
        const size_t take = std::min(classes, PRUNE + 1);
        std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(take), order.end(),
                          [&](size_t a, size_t b) { return row[a] > row[b]; });
        std::vector<size_t> tried(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(take));
        if (std::find(tried.begin(), tried.end(), static_cast<size_t>(blank)) == tried.end()) {
            tried.push_back(static_cast<size_t>(blank));
        }
        std::map<std::vector<int>, Probs> next;
        for (const auto& [prefix, p]: beams) {
            for (const size_t c: tried) {
                const double lp = row[c];
                if (static_cast<int>(c) == blank) {
                    Probs& n = next[prefix];
                    n.blank = add(n.blank, p.total() + lp);
                    continue;
                }
                std::vector<int> longer = prefix;
                longer.push_back(static_cast<int>(c));
                Probs& n = next[longer];
                if (!prefix.empty() && prefix.back() == static_cast<int>(c)) {
                    // The same character again: a new one only after a blank; else the same one goes on
                    n.nonBlank = add(n.nonBlank, p.blank + lp);
                    Probs& same = next[prefix];
                    same.nonBlank = add(same.nonBlank, p.nonBlank + lp);
                } else {
                    n.nonBlank = add(n.nonBlank, p.total() + lp);
                }
            }
        }
        // The likeliest prefixes go on
        std::vector<std::pair<double, std::vector<int>>> ranked;
        ranked.reserve(next.size());
        for (auto& [prefix, p]: next) {
            if (p.total() != NONE) {  // (a letter repeated without a blank before is no reading)
                ranked.emplace_back(p.total(), prefix);
            }
        }
        const size_t keep = std::min(ranked.size(), static_cast<size_t>(width));
        std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(keep), ranked.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        beams.clear();
        for (size_t i = 0; i < keep; ++i) {
            beams[ranked[i].second] = next[ranked[i].second];
        }
    }
    for (const auto& [prefix, p]: beams) {
        out.push_back({prefix, p.total()});
    }
    std::sort(out.begin(), out.end(), [](const CtcReading& a, const CtcReading& b) {
        return a.logProb != b.logProb ? a.logProb > b.logProb : a.labels < b.labels;
    });
    if (out.size() > static_cast<size_t>(std::max(1, topK))) {
        out.resize(static_cast<size_t>(std::max(1, topK)));
    }
    return out;
}

double ctcConfidence(const std::vector<float>& logits, size_t frames, size_t classes, int blank) {
    constexpr double NOTHING = -14.0;  ///< (about 1e-6)
    if (frames == 0 || classes == 0 || logits.size() < frames * classes) {
        return NOTHING;
    }
    double sum = 0;
    int count = 0;
    int previous = blank;
    double best = NONE;  ///< of the character being read
    for (size_t t = 0; t < frames; ++t) {
        const float* in = logits.data() + t * classes;
        const auto top = static_cast<int>(std::max_element(in, in + classes) - in);
        double total = 0;
        for (size_t c = 0; c < classes; ++c) {
            total += std::exp(static_cast<double>(in[c] - in[top]));
        }
        const double lp = -std::log(total);  ///< the top class's log-probability
        if (top != previous && previous != blank) {
            sum += best;
            ++count;
        }
        if (top != blank) {
            best = top == previous ? std::max(best, lp) : lp;
        }
        previous = top;
    }
    if (previous != blank) {
        sum += best;
        ++count;
    }
    return count > 0 ? sum / count : NOTHING;
}

bool CtcAlphabet::load(const QString& file, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot read %1").arg(file);
        }
        return false;
    }
    return parse(f.readAll(), error);
}

bool CtcAlphabet::parse(const QByteArray& text, QString* error) {
    chars.clear();
    QString all = QString::fromUtf8(text);
    all.remove(u'\r');
    if (all.endsWith(u'\n')) {
        all.chop(1);
    }
    for (const QString& line: all.split(u'\n')) {
        chars << line.normalized(QString::NormalizationForm_C);
    }
    if (chars.isEmpty() || (chars.size() == 1 && chars.front().isEmpty())) {
        chars.clear();
        if (error) {
            *error = QStringLiteral("The alphabet is empty");
        }
        return false;
    }
    return true;
}

QString CtcAlphabet::text(const std::vector<int>& labels, int blank) const {
    QString out;
    for (const int l: labels) {
        if (l == blank || l < 0) {
            continue;
        }
        const int i = l < blank ? l : l - 1;
        if (i >= 0 && i < chars.size()) {
            out += chars[i];
        }
    }
    return out;
}

}  // namespace xqt::hwr
