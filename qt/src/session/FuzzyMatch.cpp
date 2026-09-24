// Port of fzf's src/algo/algo.go (MIT License, Copyright (c) 2013-2026 Junegunn Choi; see FuzzyMatch.h).
#include "FuzzyMatch.h"

#include <algorithm>
#include <array>

#include <QChar>

#include "TextMatch.h"

namespace xqt::fuzzy {

namespace {
constexpr int scoreMatch = 16;
constexpr int scoreGapStart = -3;
constexpr int scoreGapExtension = -1;
constexpr int bonusBoundary = scoreMatch / 2;
constexpr int bonusNonWord = scoreMatch / 2;
constexpr int bonusCamel123 = bonusBoundary + scoreGapExtension;
constexpr int bonusConsecutive = -(scoreGapStart + scoreGapExtension);
constexpr int bonusFirstCharMultiplier = 2;
constexpr int bonusBoundaryWhite = bonusBoundary + 2;
constexpr int bonusBoundaryDelimiter = bonusBoundary + 1;

enum CharClass { White, NonWord, Delimiter, Lower, Upper, Letter, Number, CLASSES };
constexpr CharClass initialCharClass = White;

CharClass classOf(char16_t c) {
    if (c < 0x80) {
        if (c >= u'a' && c <= u'z') {
            return Lower;
        }
        if (c >= u'A' && c <= u'Z') {
            return Upper;
        }
        if (c >= u'0' && c <= u'9') {
            return Number;
        }
        switch (c) {
            case u' ': case u'\t': case u'\n': case u'\v': case u'\f': case u'\r': return White;
            case u'/': case u',': case u':': case u';': case u'|': return Delimiter;
            default: return NonWord;
        }
    }
    const QChar q(c);
    if (q.isLower()) {
        return Lower;
    }
    if (q.isUpper()) {
        return Upper;
    }
    if (q.isNumber()) {
        return Number;
    }
    if (q.isLetter()) {
        return Letter;
    }
    if (q.isSpace()) {
        return White;
    }
    return NonWord;
}

int bonusFor(CharClass prev, CharClass cls) {
    if (cls >= NonWord) {
        switch (prev) {
            case White: return bonusBoundaryWhite;
            case Delimiter: return bonusBoundaryDelimiter;
            case NonWord: return bonusBoundary;
            default: break;
        }
    }
    if ((prev == Lower && cls == Upper) || (prev != Number && cls == Number)) {
        return bonusCamel123;  // camelCase letter123
    }
    switch (cls) {
        case NonWord:
        case Delimiter: return bonusNonWord;
        case White: return bonusBoundaryWhite;
        default: return 0;
    }
}

const std::array<std::array<int, CLASSES>, CLASSES>& bonusMatrix() {
    static const auto m = [] {
        std::array<std::array<int, CLASSES>, CLASSES> b{};
        for (int i = 0; i < CLASSES; ++i) {
            for (int j = 0; j < CLASSES; ++j) {
                b[i][j] = bonusFor(static_cast<CharClass>(i), static_cast<CharClass>(j));
            }
        }
        return b;
    }();
    return m;
}

inline char16_t at(QStringView t, int i) { return textmatch::fold(t[i].unicode()); }

int bonusAt(QStringView t, int i) {
    if (i == 0) {
        return bonusBoundaryWhite;
    }
    return bonusMatrix()[classOf(t[i - 1].unicode())][classOf(t[i].unicode())];
}

Result none() { return {}; }

Result range(int start, int end, int score) {
    Result r{start, end, score, {}};
    for (int i = start; i < end; ++i) {
        r.positions.push_back(i);
    }
    return r;
}
}  // namespace

int scoreOf(QStringView text, QStringView pattern, int sidx, int eidx) {
    // calculateScore
    int pidx = 0, score = 0, consecutive = 0, firstBonus = 0;
    bool inGap = false;
    CharClass prevClass = sidx > 0 ? classOf(text[sidx - 1].unicode()) : initialCharClass;
    const int m = static_cast<int>(pattern.size());
    for (int idx = sidx; idx < eidx; ++idx) {
        const CharClass cls = classOf(text[idx].unicode());
        if (pidx < m && at(text, idx) == pattern[pidx].unicode()) {
            score += scoreMatch;
            int bonus = bonusMatrix()[prevClass][cls];
            if (consecutive == 0) {
                firstBonus = bonus;
            } else {
                if (bonus >= bonusBoundary && bonus > firstBonus) {
                    firstBonus = bonus;  // break the consecutive chunk
                }
                bonus = std::max({bonus, firstBonus, bonusConsecutive});
            }
            score += pidx == 0 ? bonus * bonusFirstCharMultiplier : bonus;
            inGap = false;
            ++consecutive;
            ++pidx;
        } else {
            score += inGap ? scoreGapExtension : scoreGapStart;
            inGap = true;
            consecutive = 0;
            firstBonus = 0;
        }
        prevClass = cls;
    }
    return score;
}

Result match(QStringView text, QStringView pattern) {
    const int M = static_cast<int>(pattern.size());
    if (M == 0) {
        return {0, 0, 0, {}};
    }
    const int fullN = static_cast<int>(text.size());
    if (M > fullN) {
        return none();
    }
    // Phase 1: is there a match at all, and where can it be (from one character before the first pattern character,
    // for its bonus, to the last occurrence of the last one)
    int minIdx = 0, maxIdx = 0;
    {
        int idx = 0, lastIdx = 0;
        for (int p = 0; p < M; ++p) {
            const char16_t c = pattern[p].unicode();
            while (idx < fullN && at(text, idx) != c) {
                ++idx;
            }
            if (idx >= fullN) {
                return none();
            }
            if (p == 0 && idx > 0) {
                minIdx = idx - 1;
            }
            lastIdx = idx;
            ++idx;
        }
        maxIdx = lastIdx + 1;
        const char16_t last = pattern[M - 1].unicode();
        for (int i = fullN - 1; i > lastIdx; --i) {
            if (at(text, i) == last) {
                maxIdx = i + 1;
                break;
            }
        }
    }
    const int N = maxIdx - minIdx;

    // Phase 2: the bonus of each position, the first occurrence of each pattern character, the scores of row 0
    std::vector<int> H0(static_cast<size_t>(N), 0), C0(static_cast<size_t>(N), 0), B(static_cast<size_t>(N), 0);
    std::vector<int> F(static_cast<size_t>(M), 0);
    std::vector<char16_t> T(static_cast<size_t>(N));
    int maxScore = 0, maxScorePos = 0;
    int pidx = 0, lastIdx = 0;
    const char16_t pchar0 = pattern[0].unicode();
    char16_t pchar = pchar0;
    int prevH0 = 0;
    CharClass prevClass = initialCharClass;
    bool inGap = false;
    for (int off = 0; off < N; ++off) {
        const char16_t raw = text[minIdx + off].unicode();
        const CharClass cls = classOf(raw);
        const char16_t c = textmatch::fold(raw);
        T[static_cast<size_t>(off)] = c;
        const int bonus = bonusMatrix()[prevClass][cls];
        B[static_cast<size_t>(off)] = bonus;
        prevClass = cls;
        if (c == pchar) {
            if (pidx < M) {
                F[static_cast<size_t>(pidx)] = off;
                ++pidx;
                pchar = pattern[std::min(pidx, M - 1)].unicode();
            }
            lastIdx = off;
        }
        if (c == pchar0) {
            const int score = scoreMatch + bonus * bonusFirstCharMultiplier;
            H0[static_cast<size_t>(off)] = score;
            C0[static_cast<size_t>(off)] = 1;
            if (M == 1 && score > maxScore) {
                maxScore = score;
                maxScorePos = off;
                if (bonus >= bonusBoundary) {
                    break;
                }
            }
            inGap = false;
        } else {
            H0[static_cast<size_t>(off)] = std::max(prevH0 + (inGap ? scoreGapExtension : scoreGapStart), 0);
            C0[static_cast<size_t>(off)] = 0;
            inGap = true;
        }
        prevH0 = H0[static_cast<size_t>(off)];
    }
    if (pidx != M) {
        return none();
    }
    if (M == 1) {
        return {minIdx + maxScorePos, minIdx + maxScorePos + 1, maxScore, {minIdx + maxScorePos}};
    }

    // Phase 3: the score matrix; a character of the pattern is never left out
    const int f0 = F[0];
    const int width = lastIdx - f0 + 1;
    std::vector<int> H(static_cast<size_t>(width) * static_cast<size_t>(M), 0);
    std::vector<int> C(static_cast<size_t>(width) * static_cast<size_t>(M), 0);
    std::copy(H0.begin() + f0, H0.begin() + lastIdx + 1, H.begin());
    std::copy(C0.begin() + f0, C0.begin() + lastIdx + 1, C.begin());
    for (int p = 1; p < M; ++p) {
        const int f = F[static_cast<size_t>(p)];
        const char16_t pc = pattern[p].unicode();
        const int row = p * width;
        bool gap = false;
        H[static_cast<size_t>(row + f - f0 - 1)] = 0;  // (left of the first column of the row)
        for (int col = f; col <= lastIdx; ++col) {
            const size_t cell = static_cast<size_t>(row + col - f0);
            const int left = H[cell - 1];
            const int s2 = left + (gap ? scoreGapExtension : scoreGapStart);
            int s1 = 0, consecutive = 0;
            if (pc == T[static_cast<size_t>(col)]) {
                const size_t diag = cell - 1 - static_cast<size_t>(width);
                s1 = H[diag] + scoreMatch;
                int b = B[static_cast<size_t>(col)];
                consecutive = C[diag] + 1;
                if (consecutive > 1) {
                    const int fb = B[static_cast<size_t>(col - consecutive + 1)];
                    if (b >= bonusBoundary && b > fb) {
                        consecutive = 1;  // break the consecutive chunk
                    } else {
                        b = std::max({b, bonusConsecutive, fb});
                    }
                }
                if (s1 + b < s2) {
                    s1 += B[static_cast<size_t>(col)];
                    consecutive = 0;
                } else {
                    s1 += b;
                }
            }
            C[cell] = consecutive;
            gap = s1 < s2;
            const int score = std::max({s1, s2, 0});
            if (p == M - 1 && score > maxScore) {
                maxScore = score;
                maxScorePos = col;
            }
            H[cell] = score;
        }
    }

    // Phase 4: back from the best end to the characters matched
    Result r;
    r.score = maxScore;
    r.end = minIdx + maxScorePos + 1;
    int i = M - 1;
    int j = maxScorePos;
    bool preferMatch = true;
    for (;;) {
        const int I = i * width;
        const int j0 = j - f0;
        const int s = H[static_cast<size_t>(I + j0)];
        int s1 = 0, s2 = 0;
        if (i > 0 && j >= F[static_cast<size_t>(i)]) {
            s1 = H[static_cast<size_t>(I - width + j0 - 1)];
        }
        if (j > F[static_cast<size_t>(i)]) {
            s2 = H[static_cast<size_t>(I + j0 - 1)];
        }
        const int row = i;
        if (s > s1 && (s > s2 || (s == s2 && preferMatch))) {
            r.positions.push_back(j + minIdx);
            if (i == 0) {
                break;
            }
            --i;
        }
        preferMatch = C[static_cast<size_t>(I + j0)] > 1 ||
                      (row + 1 < M && j < lastIdx && j + 1 >= F[static_cast<size_t>(row + 1)] &&
                       C[static_cast<size_t>(I + width + j0 + 1)] > 0);
        --j;
    }
    std::reverse(r.positions.begin(), r.positions.end());
    r.start = r.positions.front();
    return r;
}

Result exact(QStringView text, QStringView pattern, bool boundary) {
    // exactMatchNaive (forward)
    const int m = static_cast<int>(pattern.size());
    const int n = static_cast<int>(text.size());
    if (m == 0) {
        return {0, 0, 0, {}};
    }
    if (n < m) {
        return none();
    }
    int pidx = 0, bestPos = -1, bonus = 0, bbonus = 0, bestBonus = -1;
    for (int index = 0; index < n; ++index) {
        bool ok = pattern[pidx].unicode() == at(text, index);
        if (ok) {
            if (pidx == 0) {
                bonus = bonusAt(text, index);
            }
            if (boundary) {
                if (pidx == 0) {
                    bbonus = bonus;
                }
                ok = bbonus >= bonusBoundary;
                if (ok && pidx == 0) {
                    ok = index == 0 || classOf(text[index - 1].unicode()) <= Delimiter;
                }
                if (ok && pidx == m - 1) {
                    ok = index == n - 1 || classOf(text[index + 1].unicode()) <= Delimiter;
                }
            }
        }
        if (ok) {
            ++pidx;
            if (pidx == m) {
                if (bonus > bestBonus) {
                    bestPos = index;
                    bestBonus = bonus;
                }
                if (bonus >= bonusBoundary) {
                    break;
                }
                index -= pidx - 1;
                pidx = 0;
                bonus = 0;
            }
        } else {
            index -= pidx;
            pidx = 0;
            bonus = 0;
        }
    }
    if (bestPos < 0) {
        return none();
    }
    const int sidx = bestPos - m + 1;
    const int eidx = bestPos + 1;
    int score = 0;
    if (boundary) {
        // Underscore boundaries rank lower than the other kinds
        score = bestBonus;
        int deduct = bestBonus - bonusBoundary + 1;
        if (sidx > 0 && text[sidx - 1] == u'_') {
            score -= deduct + 1;
            deduct = 1;
        }
        if (eidx < n && text[eidx] == u'_') {
            score -= deduct;
        }
        score += scoreMatch * m + bonusBoundaryWhite * (m + 1);
    } else {
        score = scoreOf(text, pattern, sidx, eidx);
    }
    return range(sidx, eidx, score);
}

Result prefix(QStringView text, QStringView pattern) {
    const int m = static_cast<int>(pattern.size());
    if (m == 0) {
        return {0, 0, 0, {}};
    }
    int trimmed = 0;
    if (!QChar(pattern[0]).isSpace()) {
        while (trimmed < text.size() && text[trimmed].isSpace()) {
            ++trimmed;
        }
    }
    if (text.size() - trimmed < m) {
        return none();
    }
    for (int i = 0; i < m; ++i) {
        if (at(text, trimmed + i) != pattern[i].unicode()) {
            return none();
        }
    }
    return range(trimmed, trimmed + m, scoreOf(text, pattern, trimmed, trimmed + m));
}

Result suffix(QStringView text, QStringView pattern) {
    const int m = static_cast<int>(pattern.size());
    int trimmed = static_cast<int>(text.size());
    if (m == 0 || !QChar(pattern[m - 1]).isSpace()) {
        while (trimmed > 0 && text[trimmed - 1].isSpace()) {
            --trimmed;
        }
    }
    if (m == 0) {
        return {trimmed, trimmed, 0, {}};
    }
    const int diff = trimmed - m;
    if (diff < 0) {
        return none();
    }
    for (int i = 0; i < m; ++i) {
        if (at(text, diff + i) != pattern[i].unicode()) {
            return none();
        }
    }
    return range(diff, trimmed, scoreOf(text, pattern, diff, trimmed));
}

Result equal(QStringView text, QStringView pattern) {
    const int m = static_cast<int>(pattern.size());
    if (m == 0) {
        return none();
    }
    int start = 0, end = static_cast<int>(text.size());
    if (!QChar(pattern[0]).isSpace()) {
        while (start < end && text[start].isSpace()) {
            ++start;
        }
    }
    if (!QChar(pattern[m - 1]).isSpace()) {
        while (end > start && text[end - 1].isSpace()) {
            --end;
        }
    }
    if (end - start != m) {
        return none();
    }
    for (int i = 0; i < m; ++i) {
        if (at(text, start + i) != pattern[i].unicode()) {
            return none();
        }
    }
    return range(start, end, (scoreMatch + bonusBoundaryWhite) * m + (bonusFirstCharMultiplier - 1) * bonusBoundaryWhite);
}

}  // namespace xqt::fuzzy
