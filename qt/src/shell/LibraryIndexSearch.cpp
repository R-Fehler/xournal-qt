#include "LibraryIndex.h"
#include "LibraryIndexEntry.h"

#include <algorithm>
#include <set>

#include <QThreadPool>

#include "session/Citation.h"
#include "session/FuzzyQuery.h"
#include "session/TextMatch.h"
#include "session/Vocabulary.h"

#include "Tags.h"

namespace xqt {

using namespace library_index;

namespace {
/// The words of handwriting around a hit (for a snippet).
QString inkSnippet(const ink::PageText& text, const ink::Hit& hit) {
    const uint32_t from = hit.first >= 4 ? hit.first - 4 : 0;
    const auto to = std::min<uint32_t>(static_cast<uint32_t>(text.words.size()), hit.last + 5);
    QStringList words;
    for (uint32_t i = from; i < to; ++i) {
        words << text.words[i].text;
    }
    return (from > 0 ? QStringLiteral("…") : QString()) + words.join(u' ') +
           (to < text.words.size() ? QStringLiteral("…") : QString());
}
}  // namespace

std::vector<LibraryIndex::Hit> LibraryIndex::search(const QString& query) const {
    // "tag:name" terms: only documents with these tags (qt/docs/features/tags.md); the rest is the text searched
    const tags::Query tagged = tags::splitQuery(simplified(query).trimmed());
    const QString q = tagged.rest;
    const QString folded = textmatch::prepare(q);
    std::vector<Hit> hits;
    if (q.isEmpty() && tagged.tags.isEmpty()) {
        return hits;
    }
    std::vector<EntryPtr> snapshot;
    {
        std::lock_guard lock(mtx);
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                snapshot.push_back(e);
            }
        }
    }
    for (const auto& e: snapshot) {
        if (!tagged.tags.isEmpty()) {
            const QStringList docTags = e->tags();
            if (!std::all_of(tagged.tags.begin(), tagged.tags.end(),
                             [&](const QString& t) { return tags::anyMatches(docTags, t); })) {
                continue;
            }
            if (q.isEmpty()) {
                Hit h;
                h.file = e->file;
                hits.push_back(std::move(h));  // (all documents with the tags)
                continue;
            }
        }
        Hit h;
        h.file = e->file;
        h.inName = e->name.contains(q, Qt::CaseInsensitive);
        // Matches in a text (and the text around the first one), as the search of an open document matches them
        auto count = [&](const QString& text) {
            if (!h.snippet.isEmpty()) {
                return textmatch::count(text, folded);
            }
            const auto found = textmatch::find(text, folded);
            if (!found.empty()) {
                const qsizetype from = found.front().start;
                const qsizetype start = std::max<qsizetype>(0, from - 40);
                const qsizetype length = found.front().end - start + 60;
                h.snippet = (start > 0 ? QStringLiteral("…") : QString()) + text.mid(start, length) +
                            (start + length < text.size() ? QStringLiteral("…") : QString());
            }
            return static_cast<int>(found.size());
        };
        // A Markdown file: its passages, each with the headings above it
        std::vector<std::pair<int, QString>> headings;
        for (qsizetype b = 0; b < e->blockText.size(); ++b) {
            const int level = e->blockLevel[static_cast<size_t>(b)];
            if (const int n = count(e->blockText[b]); n > 0) {
                h.count += n;
                if (e->kind != QLatin1String("md")) {
                    continue;  // a text file: its hits and the text around the first one
                }
                ++h.pages;
                QStringList path;
                for (const auto& [l, text]: headings) {
                    path << (text.size() > 40 ? text.left(39) + QStringLiteral("…") : text);
                }
                h.blockHits.push_back({static_cast<int>(b), n, path.join(QStringLiteral(" › "))});
            }
            if (level > 0) {
                while (!headings.empty() && headings.back().first >= level) {
                    headings.pop_back();
                }
                headings.emplace_back(level, e->blockText[b]);
            }
        }
        const auto ink = inkOf(*e);
        int textHits = h.count;  // (of the text, not the handwriting: exact)
        bool inkExact = false;
        for (int p = 0; p < e->pageCount(); ++p) {
            int n = 0;
            if (const int pdfNr = e->pdfPage[static_cast<size_t>(p)]; pdfNr >= 0) {
                if (auto it = e->pdfText.find(pdfNr); it != e->pdfText.end()) {
                    n += count(it->second);
                }
            }
            n += count(e->elementText[p]);
            if (auto mark = e->bookmarks.find(p); mark != e->bookmarks.end()) {
                n += count(mark->second);  // (its bookmark's label)
            }
            textHits += n;
            if (const auto& text = ink && static_cast<size_t>(p) < ink->texts.size() ? ink->texts[static_cast<size_t>(p)]
                                                                                  : nullptr) {
                const auto found = ink::find(*text, {{folded, textmatch::Anywhere}});
                for (const ink::Hit& hit: found) {
                    inkExact = inkExact || hit.exact;
                    h.inkScore += hit.p;
                }
                if (!found.empty() && h.snippet.isEmpty()) {
                    h.snippet = inkSnippet(*text, found.front());
                }
                n += static_cast<int>(found.size());
            }
            if (n > 0) {
                h.count += n;
                ++h.pages;
                if (h.firstPage < 0) {
                    h.firstPage = p;
                }
                h.pageHits.push_back({p, n, e->aspects[static_cast<size_t>(p)]});
            }
        }
        h.fuzzyOnly = h.count > 0 && textHits == 0 && !inkExact;
        if (h.count > 0 || h.inName) {
            hits.push_back(std::move(h));
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.inName != b.inName) {
            return a.inName;
        }
        if (a.fuzzyOnly != b.fuzzyOnly) {
            return b.fuzzyOnly;  // (found only in handwriting the recogniser was unsure of: after the others)
        }
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.inkScore > b.inkScore;
    });
    return hits;
}

std::vector<LibraryIndex::Hit> LibraryIndex::search(const FuzzyQuery& query) const {
    if (!query.isValid()) {
        return search(query.source());
    }
    const auto& terms = query.terms();
    const std::vector<textmatch::Term> marks = query.markTerms();
    std::vector<textmatch::Term> termText;
    std::vector<char> counted;  // the hits of the terms that are not negated count
    for (size_t t = 0; t < terms.size(); ++t) {
        termText.push_back(terms[t].textTerm());
        counted.push_back(query.positive(t) ? 1 : 0);
    }
    std::vector<EntryPtr> snapshot;
    {
        std::lock_guard lock(mtx);
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                snapshot.push_back(e);
            }
        }
    }
    // Fuzzy terms match words: from the vocabularies of the documents, made first (then the words are matched at once)
    const bool fuzzy = std::any_of(termText.begin(), termText.end(),
                                   [](const textmatch::Term& t) { return (t.bounds & textmatch::Fuzzy) != 0; });
    std::vector<std::shared_ptr<const EntryWords>> vocabularies;
    if (fuzzy) {
        vocabularies = wordsOf(snapshot);
    }
    const words::Terms prepared(termText, counted);
    // A page, a passage of a Markdown file or a text file's text: which terms are on it, and the hits of those that
    // are not negated
    struct Unit {
        int index = 0;
        int count = 0;
        bool exact = false;
        std::vector<char> on;
    };
    std::vector<Hit> hits;
    for (size_t d = 0; d < snapshot.size(); ++d) {
        const EntryPtr& e = snapshot[d];
        const EntryWords* vocab = fuzzy ? vocabularies[d].get() : nullptr;
        Hit h;
        h.file = e->file;
        const fs::path dir = e->file.parent_path().lexically_relative(rootDir);
        const QString folder = dir.empty() || dir == "." ? QString() : QString::fromStdString(dir.generic_string());
        const FuzzyQuery::NameMatch name = query.matchName(e->name, folder);
        std::vector<char> inText(terms.size(), 0);
        std::vector<Unit> units;  // with hits
        const QString* snippetText = nullptr;
        const auto ink = inkOf(*e);
        auto examine = [&](int index, std::initializer_list<QStringView> texts, const ink::PageText* handwriting = nullptr) {
            const auto unit = static_cast<size_t>(index);
            words::Terms::Found f =
                    prepared.examine(texts, vocab && unit < vocab->units.size() ? &vocab->units[unit] : nullptr);
            if (handwriting) {
                // (the handwriting of the page: its hits, and which terms are in it, InkText.h)
                for (const ink::Hit& hit: ink::find(*handwriting, prepared.counted())) {
                    ++f.count;
                    f.exact = f.exact || hit.exact;
                    h.inkScore += hit.p;
                }
                for (size_t t = 0; t < terms.size(); ++t) {
                    f.on[t] = f.on[t] || ink::contains(*handwriting, prepared.all()[t]);
                }
            }
            for (size_t t = 0; t < terms.size(); ++t) {
                inText[t] |= f.on[t];
            }
            return Unit{index, f.count, f.exact, f.on};
        };
        for (qsizetype b = 0; b < e->blockText.size(); ++b) {
            if (Unit u = examine(static_cast<int>(b), {e->blockText[b]}); u.count > 0) {
                if (!snippetText) {
                    snippetText = &e->blockText[b];
                }
                units.push_back(std::move(u));
            }
        }
        const bool pagesOf = e->blockText.isEmpty();
        for (int p = 0; pagesOf && p < e->pageCount(); ++p) {
            const QString* pdfText = nullptr;
            if (const int pdfNr = e->pdfPage[static_cast<size_t>(p)]; pdfNr >= 0) {
                if (auto it = e->pdfText.find(pdfNr); it != e->pdfText.end()) {
                    pdfText = &it->second;
                }
            }
            const QString& elements = e->elementText[p];
            const auto mark = e->bookmarks.find(p);
            const QStringView label = mark != e->bookmarks.end() ? QStringView(mark->second) : QStringView();
            const ink::PageText* handwriting =
                    ink && static_cast<size_t>(p) < ink->texts.size() ? ink->texts[static_cast<size_t>(p)].get() : nullptr;
            if (Unit u = examine(p, {pdfText ? QStringView(*pdfText) : QStringView(), elements, label}, handwriting);
                u.count > 0) {
                if (!snippetText) {
                    // (from the PDF text if that has hits, else from the text elements)
                    snippetText = pdfText && !textmatch::find(*pdfText, marks).empty() ? pdfText : &elements;
                }
                units.push_back(std::move(u));
            }
        }
        // A tag term holds for the document when it has the tag (in its text or its PDF's keywords), on all its pages
        std::vector<char> tagged(terms.size(), 0);
        bool anyTag = false;
        for (size_t t = 0; t < terms.size(); ++t) {
            if (terms[t].isTag()) {
                anyTag = true;
            }
        }
        if (anyTag) {
            const QStringList docTags = e->tags();
            for (size_t t = 0; t < terms.size(); ++t) {
                if (terms[t].isTag()) {
                    tagged[t] = tags::anyMatches(docTags, terms[t].text) ? 1 : 0;
                    inText[t] = tagged[t];
                }
            }
        }
        const bool matches = query.evaluate([&](size_t t) { return name.found[t] || inText[t]; });
        if (!matches) {
            continue;
        }
        h.nameScore = name.score;
        h.nameMarks = name.positions;
        h.inName = name.score > 0;
        // The pages on which the expression holds (else all with hits)
        std::vector<const Unit*> listed;
        bool exact = false;
        for (const Unit& u: units) {
            h.count += u.count;
            exact = exact || u.exact;
            if (query.evaluate([&](size_t t) { return name.found[t] || tagged[t] || (!terms[t].isTag() && u.on[t]); })) {
                listed.push_back(&u);
            }
        }
        if (listed.empty()) {
            for (const Unit& u: units) {
                listed.push_back(&u);
            }
        }
        h.fuzzyOnly = h.count > 0 && !exact;
        if (snippetText) {
            const auto found = textmatch::find(*snippetText, marks);
            if (!found.empty()) {
                const qsizetype from = found.front().start;
                const qsizetype start = std::max<qsizetype>(0, from - 40);
                const qsizetype length = found.front().end - start + 60;
                h.snippet = (start > 0 ? QStringLiteral("…") : QString()) + snippetText->mid(start, length) +
                            (start + length < snippetText->size() ? QStringLiteral("…") : QString());
            }
        }
        if (h.snippet.isEmpty() && ink && pagesOf) {
            // (found in handwriting only: the words read around the first hit)
            for (const Unit& u: units) {
                const auto page = static_cast<size_t>(u.index);
                if (page < ink->texts.size() && ink->texts[page]) {
                    if (const auto found = ink::find(*ink->texts[page], marks); !found.empty()) {
                        h.snippet = inkSnippet(*ink->texts[page], found.front());
                        break;
                    }
                }
            }
        }
        if (e->kind == QLatin1String("md")) {
            // Each passage with the headings above it
            std::vector<std::pair<int, QString>> headings;
            size_t next = 0;
            for (qsizetype b = 0; b < e->blockText.size() && next < listed.size(); ++b) {
                if (listed[next]->index == b) {
                    QStringList path;
                    for (const auto& [l, text]: headings) {
                        path << (text.size() > 40 ? text.left(39) + QStringLiteral("…") : text);
                    }
                    h.blockHits.push_back({static_cast<int>(b), listed[next]->count, path.join(QStringLiteral(" › "))});
                    ++next;
                }
                if (const int level = e->blockLevel[static_cast<size_t>(b)]; level > 0) {
                    while (!headings.empty() && headings.back().first >= level) {
                        headings.pop_back();
                    }
                    headings.emplace_back(level, e->blockText[b]);
                }
            }
            h.pages = static_cast<int>(h.blockHits.size());
        } else if (pagesOf) {
            for (const Unit* u: listed) {
                h.pageHits.push_back({u->index, u->count, e->aspects[static_cast<size_t>(u->index)]});
            }
            h.pages = static_cast<int>(h.pageHits.size());
            h.firstPage = h.pageHits.empty() ? -1 : h.pageHits.front().page;
        }
        hits.push_back(std::move(h));
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.nameScore != b.nameScore) {
            return a.nameScore > b.nameScore;
        }
        if (a.fuzzyOnly != b.fuzzyOnly) {
            return b.fuzzyOnly;  // exact words before words that only match fuzzily
        }
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.inkScore > b.inkScore;
    });
    return hits;
}

std::vector<std::shared_ptr<const LibraryIndex::EntryWords>> LibraryIndex::wordsOf(
        const std::vector<EntryPtr>& entries) const {
    std::vector<std::shared_ptr<const EntryWords>> out(entries.size());
    std::vector<size_t> missing;
    {
        std::lock_guard lock(wordsMtx);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (auto it = wordCache.find(entries[i].get()); it != wordCache.end() && it->second.first == entries[i]) {
                out[i] = it->second.second;
            } else {
                missing.push_back(i);
            }
        }
    }
    for (const size_t i: missing) {
        const Entry& e = *entries[i];
        auto w = std::make_shared<EntryWords>();
        if (!e.blockText.isEmpty()) {
            w->units.reserve(static_cast<size_t>(e.blockText.size()));
            for (const QString& b: e.blockText) {
                w->units.emplace_back(std::initializer_list<QStringView>{b});
            }
        } else {
            w->units.reserve(static_cast<size_t>(e.pageCount()));
            for (int p = 0; p < e.pageCount(); ++p) {
                QStringView pdfText;
                if (const int nr = e.pdfPage[static_cast<size_t>(p)]; nr >= 0) {
                    if (auto it = e.pdfText.find(nr); it != e.pdfText.end()) {
                        pdfText = it->second;
                    }
                }
                w->units.emplace_back(std::initializer_list<QStringView>{pdfText, e.elementText[p]});
            }
        }
        out[i] = std::move(w);
    }
    // Kept for the documents searched now (the entries of documents changed or gone are forgotten)
    std::lock_guard lock(wordsMtx);
    std::unordered_map<const Entry*, std::pair<EntryPtr, std::shared_ptr<const EntryWords>>> kept;
    kept.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        kept.emplace(entries[i].get(), std::pair{entries[i], out[i]});
    }
    wordCache = std::move(kept);
    return out;
}

void LibraryIndex::prepareWords() {
    if (discarded || wordsQueued.exchange(true)) {
        return;
    }
    pool->start([this] {
        wordsQueued = false;
        std::vector<EntryPtr> snapshot;
        {
            std::lock_guard lock(mtx);
            for (const auto& [folder, f]: folders) {
                for (const auto& [name, e]: f.docs) {
                    snapshot.push_back(e);
                }
            }
        }
        wordsOf(snapshot);
    });
}

size_t LibraryIndex::vocabularyBytes() const {
    std::lock_guard lock(wordsMtx);
    size_t bytes = 0;
    for (const auto& [e, w]: wordCache) {
        for (const words::Vocabulary& v: w.second->units) {
            bytes += v.bytes();
        }
    }
    return bytes;
}

std::vector<LibraryIndex::TitleHit> LibraryIndex::findTitle(const QString& title, const QString& entry, int typos,
                                                             double minScore, size_t max,
                                                             const std::set<fs::path>& exclude) const {
    return titleSearch(title, entry, typos, minScore, max, exclude)();
}

LibraryIndex::TitleSearch LibraryIndex::titleSearch(const QString& title, const QString& entry, int typos,
                                                    double minScore, size_t max,
                                                    const std::set<fs::path>& exclude) const {
    std::vector<EntryPtr> entries;
    {
        std::lock_guard lock(mtx);
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                if (!exclude.count(e->file) && !(e->pdf.empty() ? false : exclude.count(e->pdf))) {
                    entries.push_back(e);
                }
            }
        }
    }
    return [entries = std::move(entries), title, entry, typos, minScore, max] {
        return matchTitles(entries, title, entry, typos, minScore, max);
    };
}

std::vector<LibraryIndex::TitleHit> LibraryIndex::matchTitles(const std::vector<EntryPtr>& entries,
                                                               const QString& title, const QString& entry, int typos,
                                                               double minScore, size_t max) {
    const QStringList query = cite::titleWords(title);
    const QStringList entryWordList = cite::titleWords(entry);
    const QSet<QString> entryWords(entryWordList.begin(), entryWordList.end());
    constexpr qsizetype FIRST_PAGE_CHARS = 400;  // (where a title is; a reference list further down is not)
    std::vector<TitleHit> hits;
    for (const EntryPtr& e: entries) {
        // A Markdown file's title: its first heading
        QString mdTitle;
        for (size_t i = 0; i < e->blockLevel.size() && mdTitle.isEmpty(); ++i) {
            if (e->blockLevel[i] > 0) {
                mdTitle = e->blockText[static_cast<qsizetype>(i)];
            }
        }
        // The start of its first page's text
        QString firstPage;
        if (const int page = firstPdfPage(e->pdfPage); page >= 0 && e->pdfText.count(page)) {
            firstPage = e->pdfText.at(page).left(FIRST_PAGE_CHARS);
        } else if (!e->blockText.isEmpty()) {
            firstPage = e->blockText.join(QLatin1Char(' ')).left(FIRST_PAGE_CHARS);
        } else if (!e->elementText.isEmpty()) {
            firstPage = e->elementText.front().left(FIRST_PAGE_CHARS);
        }
        const QString name = QString::fromStdString(e->file.stem().string());
        struct Candidate {
            QString text;
            const char* what;
            bool titleLike;
        };
        const Candidate candidates[] = {{e->title, "title", true},
                                        {e->heading, "heading", true},
                                        {mdTitle, "heading", true},
                                        {name, "name", true},
                                        {firstPage, "text", false}};
        TitleHit hit;
        for (const Candidate& c: candidates) {
            if (c.text.isEmpty()) {
                continue;
            }
            const QStringList words = cite::titleWords(c.text);
            double score = cite::titleMatch(query, words, c.titleLike, typos);
            if (c.titleLike && !entryWords.isEmpty()) {
                score = std::max(score, 0.95 * cite::titleInText(words, entryWords, typos));
            }
            if (score > hit.score) {
                hit.score = score;
                hit.matched = QString::fromLatin1(c.what);
            }
        }
        if (hit.score < minScore) {
            continue;
        }
        hit.file = e->file;
        hit.title = !e->title.isEmpty() ? e->title : !e->heading.isEmpty() ? e->heading : !mdTitle.isEmpty() ? mdTitle : name;
        hits.push_back(std::move(hit));
    }
    std::sort(hits.begin(), hits.end(), [](const TitleHit& a, const TitleHit& b) {
        return a.score != b.score ? a.score > b.score : a.file < b.file;
    });
    if (hits.size() > max) {
        hits.resize(max);
    }
    return hits;
}

QString LibraryIndex::simplified(const QString& text) { return text.simplified(); }

}  // namespace xqt
