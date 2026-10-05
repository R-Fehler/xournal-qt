#include "LanguagePlan.h"

#include <algorithm>

namespace xqt::hwr {

LanguagePlan::LanguagePlan(Choice choice, QString decided): chosen(choice), language(std::move(decided)) {}

QString LanguagePlan::nameOf(Choice choice) {
    switch (choice) {
    case Choice::English: return QStringLiteral("en");
    case Choice::German: return QStringLiteral("de");
    case Choice::Both: return QStringLiteral("both");
    case Choice::Automatic: break;
    }
    return QStringLiteral("auto");
}

LanguagePlan::Choice LanguagePlan::choiceNamed(const QString& name) {
    if (name == QLatin1String("en")) {
        return Choice::English;
    }
    if (name == QLatin1String("de")) {
        return Choice::German;
    }
    if (name == QLatin1String("both")) {
        return Choice::Both;
    }
    return Choice::Automatic;
}

LanguagePlan::Choice LanguagePlan::choice() const {
    std::lock_guard lock(mtx);
    return chosen;
}

void LanguagePlan::setChoice(Choice choice) {
    std::lock_guard lock(mtx);
    chosen = choice;
}

QString LanguagePlan::decided() const {
    std::lock_guard lock(mtx);
    return language;
}

int LanguagePlan::probed() const {
    std::lock_guard lock(mtx);
    return counts.empty() ? 0 : *std::max_element(counts.begin(), counts.end());
}

uint32_t LanguagePlan::membersOf(const QString& l, const std::vector<QStringList>& models) const {
    uint32_t bits = 0;
    for (size_t i = 0; i < models.size() && i < 32; ++i) {
        if (models[i].contains(l)) {
            bits |= 1u << i;
        }
    }
    return bits;
}

uint32_t LanguagePlan::first(const std::vector<QStringList>& models) const {
    const uint32_t everyone = models.size() >= 32 ? ~0u : (1u << models.size()) - 1;
    std::lock_guard lock(mtx);
    QString l;
    switch (chosen) {
    case Choice::English: l = QStringLiteral("en"); break;
    case Choice::German: l = QStringLiteral("de"); break;
    case Choice::Both: return everyone;
    case Choice::Automatic: l = language; break;
    }
    const uint32_t bits = l.isEmpty() ? 0 : membersOf(l, models);
    return bits != 0 ? bits : everyone;  // (no model reads it: what there is)
}

bool LanguagePlan::othersToo(float firstScore) const {
    std::lock_guard lock(mtx);
    return chosen == Choice::Automatic && !language.isEmpty() && firstScore < UNSURE;
}

void LanguagePlan::observe(const std::vector<std::optional<float>>& scores, const std::vector<QStringList>& models) {
    std::lock_guard lock(mtx);
    if (chosen != Choice::Automatic || models.size() < 2) {
        return;
    }
    sums.resize(models.size(), 0.0);
    counts.resize(models.size(), 0);
    if (language.isEmpty()) {
        // Deciding: the lines both read
        const auto read = std::count_if(scores.begin(), scores.end(), [](const auto& s) { return s.has_value(); });
        if (read < 2) {
            return;
        }
        int least = PROBE_LINES;
        for (size_t i = 0; i < scores.size() && i < models.size(); ++i) {
            if (scores[i]) {
                sums[i] += *scores[i];
                least = std::min(least, ++counts[i]);
            }
        }
        if (least >= PROBE_LINES) {
            decide(models);
        }
        return;
    }
    // Decided: how sure the preferred model is
    const uint32_t preferred = membersOf(language, models);
    double sum = 0;
    int n = 0;
    for (size_t i = 0; i < scores.size() && i < models.size(); ++i) {
        if (scores[i] && (preferred & (1u << i))) {
            sum += *scores[i];
            ++n;
        }
    }
    if (n == 0) {
        return;
    }
    recent.push_back(sum / n < UNSURE);
    if (recent.size() > static_cast<size_t>(WINDOW)) {
        recent.pop_front();
    }
    if (std::count(recent.begin(), recent.end(), true) >= WINDOW / 2) {
        // Its confidence dropped (another language, another hand): both read again, and decide anew
        language.clear();
        recent.clear();
        std::fill(sums.begin(), sums.end(), 0.0);
        std::fill(counts.begin(), counts.end(), 0);
    }
}

void LanguagePlan::decide(const std::vector<QStringList>& models) {
    int best = -1, second = -1;
    std::vector<double> mean(models.size(), -1.0);
    for (size_t i = 0; i < models.size(); ++i) {
        if (counts[i] == 0) {
            continue;
        }
        mean[i] = sums[i] / counts[i];
        const int k = static_cast<int>(i);
        if (best < 0 || mean[i] > mean[static_cast<size_t>(best)]) {
            second = best;
            best = k;
        } else if (second < 0 || mean[i] > mean[static_cast<size_t>(second)]) {
            second = k;
        }
    }
    if (best >= 0 && second >= 0 && mean[static_cast<size_t>(best)] - mean[static_cast<size_t>(second)] >= MARGIN &&
        !models[static_cast<size_t>(best)].isEmpty()) {
        language = models[static_cast<size_t>(best)].front();
        recent.clear();
    }
    // (else a mixed document, or both alike: both go on reading, and it is tried again after PROBE_LINES more lines)
    std::fill(sums.begin(), sums.end(), 0.0);
    std::fill(counts.begin(), counts.end(), 0);
}

}  // namespace xqt::hwr
