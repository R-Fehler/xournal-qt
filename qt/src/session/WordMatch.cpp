#include "WordMatch.h"

#include <algorithm>
#include <array>

#include <QChar>

namespace xqt::wordmatch {

bool perWord(QStringView term) {
    return term.size() >= MIN_LETTERS &&
           std::all_of(term.begin(), term.end(), [](QChar c) { return c.isLetterOrNumber(); });
}

int gapsOf(QStringView term, QStringView word) {
    if (term.isEmpty() || word.isEmpty() || word[0] != term[0]) {
        return -1;
    }
    // (each letter as early as possible: then the last one is found first, with the fewest letters in between)
    qsizetype k = 1, last = 0;
    for (qsizetype j = 1; j < word.size() && k < term.size(); ++j) {
        if (word[j] == term[k]) {
            ++k;
            last = j;
        }
    }
    return k < term.size() ? -1 : static_cast<int>(last + 1 - term.size());
}

int typosAllowed(int letters, int typos) {
    if (typos <= 0 || letters < TYPO_LETTERS) {
        return 0;
    }
    return typos >= 2 && letters >= TWO_TYPOS_LETTERS ? 2 : 1;
}

int editDistance(QStringView a, QStringView b, int max) {
    const qsizetype n = a.size(), m = b.size();
    constexpr qsizetype LONGEST = 63;
    if (std::abs(n - m) > max || m > LONGEST) {
        return max + 1;  // (no word is that long; a term of that length has no typos)
    }
    // Three rows of the matrix (the one before the last for a swap)
    std::array<int, LONGEST + 1> rows[3];
    auto* before = &rows[0];
    auto* last = &rows[1];
    auto* row = &rows[2];
    for (qsizetype j = 0; j <= m; ++j) {
        (*last)[static_cast<size_t>(j)] = static_cast<int>(j);
    }
    for (qsizetype i = 1; i <= n; ++i) {
        (*row)[0] = static_cast<int>(i);
        int best = (*row)[0];
        for (qsizetype j = 1; j <= m; ++j) {
            const auto J = static_cast<size_t>(j);
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int d = std::min({(*last)[J] + 1, (*row)[J - 1] + 1, (*last)[J - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                d = std::min(d, (*before)[J - 2] + 1);  // two letters swapped
            }
            (*row)[J] = d;
            best = std::min(best, d);
        }
        if (best > max) {
            return max + 1;  // (the least of a row does not shrink by more than a swap can take back)
        }
        std::swap(before, last);
        std::swap(last, row);
    }
    return std::min((*last)[static_cast<size_t>(m)], max + 1);
}

Rule::Rule(QStringView term, int typos):
        text(term.toString()),
        gaps(gapsAllowed(static_cast<int>(term.size()))),
        edits(typosAllowed(static_cast<int>(term.size()), typos)) {}

Quality Rule::match(QStringView word) const {
    const qsizetype n = text.size();
    if (n == 0 || word.isEmpty()) {
        return None;
    }
    if (word.size() >= n && word.contains(text)) {
        return Exact;
    }
    if (const int g = gapsOf(text, word); g >= 0 && g <= gaps) {
        return Fuzzy;  // its letters in this order, from the word's first one on
    }
    if (edits > 0 && editDistance(text, word, edits) <= edits) {
        return Fuzzy;
    }
    return None;
}

}  // namespace xqt::wordmatch
