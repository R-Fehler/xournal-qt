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
    /// becomes current (and is scrolled to).
    void setQuery(const QString& text, bool jump = true);
    const QString& query() const { return text; }
    void clear() { setQuery({}); }

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

Q_SIGNALS:
    /// Hits or the current hit changed.
    void changed();
    /// All counts are known.
    void finished();

private:
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

    DocumentSession& session;
    DocumentTextIndex index;
    QString text;
    QString prepared;
    std::vector<int> counts;        ///< per page
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
    quint64 rev = 0;
    quint64 generation = 0;
    int corrections = 0;
};

}  // namespace xqt
