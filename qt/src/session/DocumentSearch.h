/*
 * xournal-qt: text search in one document (the PDF text of its pages and its text elements).
 *
 * In two steps, over the document's text index (DocumentTextIndex, kept up to date with every edit):
 *  1. a string scan counts the hits of every page (milliseconds, also for a manual of 1,300 pages): the count,
 *     "n pages with hits", the marks in the sidebar and the page grid, and the counts in the tab overview are there at
 *     once. While the index still reads PDF text, the counts grow (isRunning()).
 *  2. where the hits are drawn is computed only for the pages that are shown (the canvas, the thumbnails in view) and
 *     for the page of the current hit: rectsOn() asks for them, the current page first, and keeps them per page until
 *     the query or the page changes. Their text is the index's, matched the same way (TextMatch), so the count and
 *     the marks agree.
 * The current hit is a page and a hit on it; stepping to a page whose hits are not placed yet counts on the page's
 * count and scrolls there once they are.
 *
 * A query of the fuzzy search (FuzzyQuery.h; the search bar's Fuzzy toggle, or handed over from the library or the
 * tab overview with it on)
 * counts and marks the hits of all its terms that are not negated. Whether the document matches the whole
 * expression (with its name) and on which pages it holds, the tab overview asks with matches() and matchingPages().
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <mutex>
#include <set>
#include <vector>

#include <QObject>
#include <QRectF>
#include <QString>

#include "DocumentTextIndex.h"
#include "FuzzyQuery.h"

class Document;

namespace xqt {

class DocumentSession;

class DocumentSearch final: public QObject {
    Q_OBJECT
public:
    /// A page with hits.
    struct PageHits {
        size_t page = 0;
        int count = 0;
    };
    /// Where a hit is drawn (page points): on its first line, and the rest when it goes on in the next line.
    struct Place {
        QRectF rect;
        QRectF more;  ///< null: all on one line
    };

    explicit DocumentSearch(DocumentSession& session);
    ~DocumentSearch() override;

    /// Search for `text` (see TextMatch; empty: clear). With `jump`, the first hit from the current page on
    /// becomes current (and is scrolled to). With `fuzzy`, the text is read with the fuzzy search's syntax
    /// (FuzzyQuery.h; an expression that is not valid: as plain text).
    void setQuery(const QString& text, bool jump = true, bool fuzzy = false);
    const QString& query() const { return text; }
    /// The query is read with the fuzzy search's syntax.
    bool fuzzy() const { return fuzzyMode; }
    /// Why a fuzzy query is searched as plain text ("": it is not, or the search is not fuzzy).
    QString hint() const { return fuzzyMode ? parsed.hint() : QString(); }
    void clear() { setQuery({}); }

    /// The document matches the search: it has hits; a fuzzy query: its expression holds with the terms found in the
    /// document's text or in `name` (its title).
    bool matches(QStringView name) const;
    /// The pages with hits on which a fuzzy query's expression holds (a term counts as found on a page when the page
    /// or `name` has it); if it holds on none of them, all pages with hits. Not fuzzy: pages().
    std::vector<PageHits> matchingPages(QStringView name) const;

    /// Not all counts are known yet: PDF text is still being read.
    bool isRunning() const { return !text.isEmpty() && !index.complete(); }
    /// The pages with hits, in order.
    const std::vector<PageHits>& pages() const { return withHits; }
    int hitCount() const { return total; }
    int countOn(size_t page) const { return page < counts.size() ? counts[page] : 0; }
    /// Where the hits of a page are drawn, in reading order; nullptr while that is not known (with `ask`, it is asked
    /// for: the search changes when it is there). May be called from the scene graph's thread while the UI thread
    /// waits.
    const std::vector<Place>* placesOn(size_t page, bool ask = true) const;
    /// The current hit: its number among all (0-based; -1: none), its page, its number on that page.
    int currentHit() const;
    size_t currentPage() const { return curPage; }
    int currentOnPage() const { return curIndex; }
    /// Increased with every change of the hits or the current hit (for views).
    quint64 revision() const { return rev; }

    DocumentTextIndex& textIndex() { return index; }
    /// Hits placed differently than counted so far (tests: the two steps match text the same way).
    int countCorrections() const { return corrections; }

    /// The hits on one page of a document (page points, reading order): PDF text and visible text elements, with
    /// poppler's own search. Takes a shared document lock; usable on any thread for a document no session changes
    /// meanwhile (the pages with hits of the library's search).
    static std::vector<QRectF> findOnPage(Document& doc, size_t page, const std::string& utf8);

    void next();
    void previous();
    /// The first hit at or after the current page becomes current (wrapping around).
    void jumpToFirstFromCurrentPage();
    /// Hit `index` of `page` (0-based, in the order of the page's text) becomes current, once the counts are known;
    /// if the page has fewer, the first hit after it (e.g. a hit in a place of a text that is known to be on that
    /// page or after it).
    void jumpToHit(size_t page, int index);

Q_SIGNALS:
    /// Hits or the current hit changed.
    void changed();
    /// All counts are known.
    void finished();

private:
    void prepareTerms();
    void recount(const std::vector<size_t>& pages);
    void recountAll();
    void rebuildHitPages();
    void place(size_t page);
    void placeWanted();
    void setCurrent(size_t page, int index, bool scroll);
    void scrollToCurrent();
    bool tryPendingJump();
    void finish();
    void pageMoved(size_t page, int delta);
    /// A fuzzy query: which of its terms are on the page
    std::vector<char> termsOn(size_t page);
    bool expressionOn(size_t page, const std::vector<char>& inName) const;

    DocumentSession& session;
    DocumentTextIndex index;
    QString text;
    bool fuzzyMode = false;
    FuzzyQuery parsed;                    ///< the query, when fuzzy
    std::vector<textmatch::Term> terms;   ///< what is counted and marked (empty: nothing searched)
    words::Terms counted;                 ///< `terms`, prepared for counting
    words::Terms queryTerms;              ///< a valid fuzzy query: all its terms (textTerm()), prepared
    std::vector<int> counts;        ///< per page
    std::vector<std::vector<char>> found;  ///< a valid fuzzy query: per page, which of its terms are on it
    std::vector<PageHits> withHits;
    int total = 0;
    std::map<size_t, std::vector<Place>> places;  ///< per page, for this query
    std::set<size_t> waiting;                     ///< pages waiting for their PDF layout
    mutable std::mutex wantedMtx;
    mutable std::set<size_t> wanted;              ///< pages asked for by placesOn()
    size_t curPage = 0;
    int curIndex = -1;                            ///< -1: no current hit
    bool scrollPending = false;
    bool pendingJump = false;
    bool jumpScrolls = true;
    size_t startPage = 0;
    int startIndex = 0;                           ///< the hit on the start page to jump to
    quint64 rev = 0;
    quint64 generation = 0;
    int corrections = 0;
};

}  // namespace xqt
