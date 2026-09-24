#include "Vocabulary.h"

#include <algorithm>
#include <list>
#include <mutex>

#include <QHash>
#include <QString>

namespace xqt::words {

namespace {

/// Every word seen, numbered: the characters one after the other, an open hash table of the numbers.
class Dictionary {
public:
    static Dictionary& instance() {
        static Dictionary d;
        return d;
    }
    std::mutex mtx;

    /// The number of a word, added if new (mtx held)
    Id add(QStringView w) {
        const auto h = static_cast<std::uint32_t>(qHash(w));
        if ((count() + 1) * 2 > table.size()) {
            grow();
        }
        const size_t mask = table.size() - 1;
        for (size_t slot = h & mask;; slot = (slot + 1) & mask) {
            const std::uint32_t entry = table[slot];
            if (entry == 0) {
                const auto id = static_cast<Id>(count());
                chars.insert(chars.end(), w.utf16(), w.utf16() + w.size());
                starts.push_back(static_cast<std::uint32_t>(chars.size()));
                hashes.push_back(h);
                table[slot] = id + 1;
                return id;
            }
            if (hashes[entry - 1] == h && at(entry - 1) == w) {
                return entry - 1;
            }
        }
    }
    /// A word (mtx held; valid until the next add())
    QStringView at(Id id) const {
        return QStringView(chars.data() + starts[id], static_cast<qsizetype>(starts[id + 1] - starts[id]));
    }
    size_t count() const { return starts.size() - 1; }
    size_t bytes() const {
        return chars.capacity() * sizeof(char16_t) + (starts.capacity() + hashes.capacity() + table.capacity()) * 4;
    }

private:
    Dictionary() { table.assign(1024, 0); }
    void grow() {
        std::vector<std::uint32_t> bigger(table.size() * 2, 0);
        const size_t mask = bigger.size() - 1;
        for (Id id = 0; id < count(); ++id) {
            size_t slot = hashes[id] & mask;
            while (bigger[slot] != 0) {
                slot = (slot + 1) & mask;
            }
            bigger[slot] = id + 1;
        }
        table = std::move(bigger);
    }

    std::vector<char16_t> chars;
    std::vector<std::uint32_t> starts{0};
    std::vector<std::uint32_t> hashes;
    std::vector<std::uint32_t> table;  ///< number + 1 (0: free)
};

}  // namespace

size_t dictionarySize() {
    Dictionary& d = Dictionary::instance();
    std::lock_guard lock(d.mtx);
    return d.count();
}

size_t dictionaryBytes() {
    Dictionary& d = Dictionary::instance();
    std::lock_guard lock(d.mtx);
    return d.bytes();
}

// --- Vocabulary ------------------------------------------------------------------------------------------------

Vocabulary::Vocabulary(std::initializer_list<QStringView> texts) {
    QHash<QString, std::uint32_t> seen;
    for (const QStringView text: texts) {
        textmatch::words(text, [&](qsizetype, qsizetype, QStringView word) {
            auto it = seen.find(QString::fromRawData(word.data(), word.size()));  // (no copy of words seen before)
            if (it == seen.end()) {
                seen.insert(word.toString(), 1);
            } else {
                ++*it;
            }
        });
    }
    list.reserve(static_cast<size_t>(seen.size()));
    {
        Dictionary& d = Dictionary::instance();
        std::lock_guard lock(d.mtx);
        for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
            list.push_back({d.add(it.key()), it.value()});
        }
    }
    std::sort(list.begin(), list.end(), [](const Word& a, const Word& b) { return a.id < b.id; });
}

// --- Matches ---------------------------------------------------------------------------------------------------

Matches::Matches(const textmatch::Term& term): rule(term.text, textmatch::typosOf(term.bounds)) {}

wordmatch::Quality Matches::quality(Id id) const {
    if (id < known.size()) {
        return known[id];
    }
    // A word added since: matched now
    Dictionary& d = Dictionary::instance();
    std::lock_guard lock(d.mtx);
    return id < d.count() ? rule.match(d.at(id)) : wordmatch::None;
}

struct MatchCache {
    std::mutex mtx;
    std::list<std::pair<textmatch::Term, std::shared_ptr<const Matches>>> recent;  ///< most recently used first
    static constexpr size_t KEPT = 16;

    static MatchCache& instance() {
        static MatchCache c;
        return c;
    }

    /// `m` extended to all words of the dictionary (a copy: the one before may be in use)
    static std::shared_ptr<const Matches> extended(const textmatch::Term& term, const Matches* before) {
        auto m = std::make_shared<Matches>(term);
        Dictionary& d = Dictionary::instance();
        std::lock_guard lock(d.mtx);
        const size_t from = before ? before->known.size() : 0;
        m->known.reserve(d.count());
        if (before) {
            m->known = before->known;
        }
        for (size_t id = from; id < d.count(); ++id) {
            m->known.push_back(m->rule.match(d.at(static_cast<Id>(id))));
        }
        return m;
    }
};

std::shared_ptr<const Matches> Matches::of(const textmatch::Term& term) {
    MatchCache& c = MatchCache::instance();
    std::lock_guard lock(c.mtx);
    const size_t words = dictionarySize();
    auto it = std::find_if(c.recent.begin(), c.recent.end(), [&](const auto& e) { return e.first == term; });
    if (it != c.recent.end()) {
        c.recent.splice(c.recent.begin(), c.recent, it);
        auto& m = c.recent.front().second;
        // Words added since are matched when asked for; many of them: all at once
        const size_t have = m->known.size();
        if (words > have + std::max<size_t>(1024, have / 16)) {
            m = MatchCache::extended(term, m.get());
        }
        return m;
    }
    c.recent.emplace_front(term, MatchCache::extended(term, nullptr));
    if (c.recent.size() > MatchCache::KEPT) {
        c.recent.pop_back();
    }
    return c.recent.front().second;
}

// --- Terms -----------------------------------------------------------------------------------------------------

Terms::Terms(std::vector<textmatch::Term> terms, std::vector<char> counted): list(std::move(terms)), counts(std::move(counted)) {
    counts.resize(list.size(), 1);
    matches.resize(list.size());
    for (size_t t = 0; t < list.size(); ++t) {
        const bool fuzzy = (list[t].bounds & textmatch::Fuzzy) != 0;
        if (fuzzy) {
            matches[t] = Matches::of(list[t]);
            anyFuzzy = true;
        }
        if (counts[t]) {
            countedTerms.push_back(list[t]);
            if (fuzzy) {
                countedFuzzy = true;
            } else {
                countedPlain.push_back(list[t]);
                onlyPlain = t;
            }
        }
    }
}

Terms::Found Terms::examine(std::initializer_list<QStringView> texts, const Vocabulary* vocab) const {
    Found f;
    f.on.assign(list.size(), 0);
    // Fuzzy terms: from the vocabulary. A word that matches several of them is one hit (its hits are the same spans)
    int fuzzyHits = 0;
    if (anyFuzzy && vocab) {
        for (const Vocabulary::Word& w: vocab->words()) {
            bool counted = false;
            for (size_t t = 0; t < list.size(); ++t) {
                if (!matches[t]) {
                    continue;
                }
                const wordmatch::Quality q = matches[t]->quality(w.id);
                if (q == wordmatch::None) {
                    continue;
                }
                f.on[t] = 1;
                if (counts[t]) {
                    counted = true;
                    f.exact = f.exact || q == wordmatch::Exact;
                }
            }
            if (counted) {
                fuzzyHits += static_cast<int>(w.count);
            }
        }
    }
    // The others: in the text. The counted ones are found (their hits may overlap), the rest only looked for
    int plainHits = 0;
    for (const QStringView text: texts) {
        if (text.isEmpty()) {
            continue;
        }
        for (size_t t = 0; t < list.size(); ++t) {
            if (!matches[t] && !counts[t] && !f.on[t]) {
                f.on[t] = textmatch::contains(text, list[t].text, list[t].bounds) ? 1 : 0;
            }
        }
        if (countedPlain.empty()) {
            continue;
        }
        const int n = textmatch::count(text, countedPlain);
        if (n > 0) {
            plainHits += n;
            f.exact = true;
            if (countedPlain.size() == 1) {
                f.on[onlyPlain] = 1;
            } else {
                for (size_t t = 0; t < list.size(); ++t) {
                    if (!matches[t] && counts[t] && !f.on[t]) {
                        f.on[t] = textmatch::contains(text, list[t].text, list[t].bounds) ? 1 : 0;
                    }
                }
            }
        }
    }
    // Hits of counted terms of both kinds may overlap: counted in the text then, as they are marked
    if (fuzzyHits == 0 || plainHits == 0) {
        f.count = fuzzyHits + plainHits;
        return f;
    }
    for (const QStringView text: texts) {
        f.count += textmatch::count(text, countedTerms);
    }
    return f;
}

int Terms::count(std::initializer_list<QStringView> texts, const Vocabulary* vocab) const {
    return examine(texts, vocab).count;
}

bool Terms::contains(size_t i, std::initializer_list<QStringView> texts, const Vocabulary* vocab) const {
    if (i >= list.size()) {
        return false;
    }
    if (matches[i]) {
        if (!vocab) {
            return false;
        }
        const auto& w = vocab->words();
        return std::any_of(w.begin(), w.end(), [&](const Vocabulary::Word& v) {
            return matches[i]->quality(v.id) != wordmatch::None;
        });
    }
    return std::any_of(texts.begin(), texts.end(), [&](QStringView text) {
        return textmatch::contains(text, list[i].text, list[i].bounds);
    });
}

}  // namespace xqt::words
