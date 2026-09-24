#include "DocumentSearch.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include <QTimer>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "DocumentSession.h"
#include "MdBox.h"
#include "TextMatch.h"

namespace xqt {

namespace {
const std::vector<DocumentSearch::Place> NO_PLACES;
}

DocumentSearch::DocumentSearch(DocumentSession& session): session(session), index(session) {
    connect(&index, &DocumentTextIndex::textChanged, this, [this](const std::vector<size_t>& pages) { recount(pages); });
    connect(&index, &DocumentTextIndex::pageMoved, this, &DocumentSearch::pageMoved);
    connect(&index, &DocumentTextIndex::reset, this, [this] {
        places.clear();
        waiting.clear();
        if (!text.isEmpty()) {
            recountAll();
        }
    });
    connect(&index, &DocumentTextIndex::layoutReady, this, [this](int pdfPage) {
        bool placed = false;
        for (const size_t page: std::vector<size_t>(waiting.begin(), waiting.end())) {
            if (index.pdfPageOf(page) == pdfPage) {
                place(page);
                placed = true;
            }
        }
        if (placed) {
            ++rev;
            scrollToCurrent();
            Q_EMIT changed();
        }
    });
    connect(&index, &DocumentTextIndex::completed, this, [this] {
        if (!text.isEmpty()) {
            tryPendingJump();
            ++rev;
            Q_EMIT changed();
            Q_EMIT finished();
        }
    });
    // Undoable changes of a page (the index also hears the document's own events)
    connect(&session, &DocumentSession::pageContentChanged, this, [this](qulonglong page) { index.pageChanged(page); });
    connect(&session, &DocumentSession::currentPageChanged, this, [this](qulonglong page) { index.setFocusPage(page); });
}

DocumentSearch::~DocumentSearch() = default;

void DocumentSearch::setQuery(const QString& query, bool jump) {
    if (query == text) {
        if (jump && curIndex < 0) {
            jumpToFirstFromCurrentPage();
        }
        return;
    }
    text = query;
    prepared = textmatch::prepare(query);
    ++generation;
    places.clear();
    waiting.clear();
    {
        std::lock_guard lock(wantedMtx);
        wanted.clear();
    }
    curIndex = -1;
    scrollPending = false;
    pendingJump = jump && !prepared.isEmpty();
    jumpScrolls = true;
    startPage = session.getCurrentPageNo();
    if (prepared.isEmpty()) {
        index.release();
    } else {
        index.setFocusPage(startPage);
        index.start();
    }
    recountAll();
    if (!isRunning()) {
        // (after the caller got its answer, as when the counts come in later)
        QTimer::singleShot(0, this, [this, gen = generation] {
            if (gen == generation) {
                Q_EMIT finished();
            }
        });
    }
}

void DocumentSearch::recountAll() {
    counts.assign(index.pageCount(), 0);
    if (!prepared.isEmpty()) {
        for (size_t i = 0; i < counts.size(); ++i) {
            counts[i] = index.count(i, prepared);
        }
    }
    rebuildHitPages();
    ++rev;
    tryPendingJump();
    Q_EMIT changed();
}

void DocumentSearch::recount(const std::vector<size_t>& pages) {
    if (prepared.isEmpty()) {
        return;
    }
    counts.resize(index.pageCount(), 0);
    std::vector<size_t> shown;
    for (const size_t page: pages) {
        if (page >= counts.size()) {
            continue;
        }
        counts[page] = index.count(page, prepared);
        if (places.erase(page) > 0) {
            shown.push_back(page);  // (placed again right away: its marks do not blink)
        }
    }
    for (const size_t page: shown) {
        place(page);
    }
    rebuildHitPages();
    ++rev;
    if (!tryPendingJump()) {
        scrollToCurrent();
    }
    Q_EMIT changed();
}

void DocumentSearch::rebuildHitPages() {
    withHits.clear();
    total = 0;
    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] > 0) {
            withHits.push_back({i, counts[i]});
            total += counts[i];
        }
    }
    // The current hit stays where it was, if that is still a hit; else the next one from there (without scrolling:
    // the text is being edited)
    if (curIndex >= 0) {
        if (countOn(curPage) > 0) {
            curIndex = std::min(curIndex, countOn(curPage) - 1);
        } else if (withHits.empty()) {
            curIndex = -1;
        } else {
            auto it = std::find_if(withHits.begin(), withHits.end(), [&](const PageHits& h) { return h.page >= curPage; });
            curPage = (it == withHits.end() ? withHits.front() : *it).page;
            curIndex = 0;
        }
    }
}

bool DocumentSearch::tryPendingJump() {
    if (!pendingJump) {
        return false;
    }
    // The first hit from the start page on; the pages before it must be known (they are read from the current page
    // outwards), else a nearer one may still come
    for (size_t p = startPage; p < counts.size(); ++p) {
        if (!index.known(p) && !index.complete()) {
            return false;
        }
        if (counts[p] > 0) {
            pendingJump = false;
            setCurrent(p, 0, jumpScrolls);
            return true;
        }
    }
    if (index.complete()) {
        pendingJump = false;
        if (!withHits.empty()) {  // wrap around: hits only before the start page
            setCurrent(withHits.front().page, 0, jumpScrolls);
            return true;
        }
    }
    return false;
}

const std::vector<DocumentSearch::Place>* DocumentSearch::placesOn(size_t page, bool ask) const {
    if (auto it = places.find(page); it != places.end()) {
        return &it->second;
    }
    if (prepared.isEmpty() || page >= counts.size() || (counts[page] == 0 && index.known(page))) {
        return &NO_PLACES;
    }
    if (!ask) {
        return nullptr;
    }
    std::lock_guard lock(wantedMtx);
    if (wanted.insert(page).second && wanted.size() == 1) {
        auto* self = const_cast<DocumentSearch*>(this);
        QMetaObject::invokeMethod(self, [self] { self->placeWanted(); }, Qt::QueuedConnection);
    }
    return nullptr;
}

void DocumentSearch::placeWanted() {
    std::set<size_t> pages;
    {
        std::lock_guard lock(wantedMtx);
        pages.swap(wanted);
    }
    bool placed = false;
    for (const size_t page: pages) {
        if (!places.count(page) && !waiting.count(page)) {
            place(page);
            placed = places.count(page) > 0 || placed;
        }
    }
    if (placed) {
        ++rev;
        Q_EMIT changed();
    }
}

void DocumentSearch::place(size_t page) {
    if (prepared.isEmpty() || page >= counts.size()) {
        return;
    }
    std::vector<Place> found;
    auto add = [&](std::vector<QRectF> rects) {
        Place p;
        if (!rects.empty()) {
            p.rect = rects.front();
            for (size_t i = 1; i < rects.size(); ++i) {
                p.more |= rects[i];
            }
        }
        found.push_back(p);
    };
    if (const int pdfPage = index.pdfPageOf(page); pdfPage >= 0) {
        const PdfPageLayout* layout = index.layout(pdfPage, page == curPage);
        if (!layout) {
            waiting.insert(page);  // (placed when it is read)
            return;
        }
        for (const auto& m: textmatch::find(layout->text, prepared)) {
            add(layout->rects(m.start, m.end));
        }
    }
    {
        Document* doc = session.getDocument();
        std::shared_lock lock(*doc);
        if (page < doc->getPageCount()) {
            for (const ElementText& piece: elementTexts(*doc->getPage(page))) {
                const auto s = textmatch::simplify(piece.shown);
                for (const auto& m: textmatch::find(s.text, prepared)) {
                    add(elementRects(piece, s.origin[static_cast<size_t>(m.start)],
                                     s.origin[static_cast<size_t>(m.end - 1)] + 1));
                }
            }
        }
    }
    // Reading order on the page (lines in 4 pt bands, then left to right)
    auto key = [](const Place& h) { return std::pair{static_cast<long>(h.rect.top() / 4), h.rect.left()}; };
    std::stable_sort(found.begin(), found.end(), [&](const Place& a, const Place& b) { return key(a) < key(b); });
    waiting.erase(page);
    if (static_cast<int>(found.size()) != counts[page]) {
        // Counted in text that changed meanwhile (an edit not read again yet): what is on the page counts
        ++corrections;
        counts[page] = static_cast<int>(found.size());
        rebuildHitPages();
    }
    places[page] = std::move(found);
    if (page == curPage) {
        scrollToCurrent();
    }
}

int DocumentSearch::currentHit() const {
    if (curIndex < 0) {
        return -1;
    }
    int before = 0;
    for (const PageHits& h: withHits) {
        if (h.page >= curPage) {
            break;
        }
        before += h.count;
    }
    return before + curIndex;
}

void DocumentSearch::setCurrent(size_t page, int index, bool scroll) {
    curPage = page;
    curIndex = index;
    ++rev;
    if (scroll) {
        session.setCurrentPageNo(page);
        scrollPending = true;
        scrollToCurrent();
    }
}

void DocumentSearch::scrollToCurrent() {
    if (!scrollPending || curIndex < 0) {
        return;
    }
    auto it = places.find(curPage);
    if (it == places.end()) {
        if (!waiting.count(curPage)) {
            place(curPage);  // (calls this again when placed)
        }
        return;
    }
    scrollPending = false;
    if (it->second.empty()) {
        return;
    }
    const Place& p = it->second[static_cast<size_t>(std::min<int>(curIndex, static_cast<int>(it->second.size()) - 1))];
    Q_EMIT session.scrollToRectRequested(curPage, p.more.isNull() ? p.rect : p.rect | p.more);
}

void DocumentSearch::next() {
    if (total == 0) {
        return;
    }
    if (curIndex < 0) {
        setCurrent(withHits.front().page, 0, true);
    } else if (curIndex + 1 < countOn(curPage)) {
        setCurrent(curPage, curIndex + 1, true);
    } else {
        auto it = std::upper_bound(withHits.begin(), withHits.end(), curPage,
                                   [](size_t p, const PageHits& h) { return p < h.page; });
        setCurrent((it == withHits.end() ? withHits.front() : *it).page, 0, true);
    }
    Q_EMIT changed();
}

void DocumentSearch::previous() {
    if (total == 0) {
        return;
    }
    if (curIndex < 0) {
        setCurrent(withHits.back().page, withHits.back().count - 1, true);
    } else if (curIndex > 0) {
        setCurrent(curPage, curIndex - 1, true);
    } else {
        auto it = std::lower_bound(withHits.begin(), withHits.end(), curPage,
                                   [](const PageHits& h, size_t p) { return h.page < p; });
        const PageHits& h = it == withHits.begin() ? withHits.back() : *std::prev(it);
        setCurrent(h.page, h.count - 1, true);
    }
    Q_EMIT changed();
}

void DocumentSearch::jumpToFirstFromCurrentPage() {
    startPage = session.getCurrentPageNo();
    pendingJump = true;
    jumpScrolls = true;
    if (tryPendingJump()) {
        Q_EMIT changed();
    }
}

void DocumentSearch::pageMoved(size_t page, int delta) {
    if (delta > 0) {
        counts.insert(counts.begin() + static_cast<std::ptrdiff_t>(std::min(page, counts.size())),
                      prepared.isEmpty() ? 0 : index.count(page, prepared));
    } else if (page < counts.size()) {
        counts.erase(counts.begin() + static_cast<std::ptrdiff_t>(page));
    }
    // The places of the pages behind it move along
    std::map<size_t, std::vector<Place>> moved;
    for (auto& [p, v]: places) {
        if (p < page) {
            moved.emplace(p, std::move(v));
        } else if (delta > 0 || p > page) {
            moved.emplace(p + static_cast<size_t>(delta > 0 ? 1 : 0) - static_cast<size_t>(delta < 0 ? 1 : 0),
                          std::move(v));
        }
    }
    places = std::move(moved);
    waiting.clear();
    if (curIndex >= 0) {
        if (delta > 0 && curPage >= page) {
            ++curPage;
        } else if (delta < 0 && curPage > page) {
            --curPage;
        }
    }
    if (pendingJump && delta > 0 && startPage >= page) {
        ++startPage;
    } else if (pendingJump && delta < 0 && startPage > page) {
        --startPage;
    }
    rebuildHitPages();
    ++rev;
    Q_EMIT changed();
}

std::vector<QRectF> DocumentSearch::findOnPage(Document& document, size_t pageNo, const std::string& utf8) {
    // Port of SearchControl::search (the search of one page)
    std::vector<XojPdfRectangle> results;
    Document* doc = &document;
    {
        std::shared_lock lock(*doc);
        if (pageNo >= doc->getPageCount()) {
            return {};
        }
        PageRef page = doc->getPage(pageNo);
        if (page->getBackgroundType().isPdfPage()) {
            if (auto pdf = doc->getPdfPage(page->getPdfPageNr())) {
                results = pdf->findText(utf8);
            }
        }
        for (Layer* l: page->getLayers()) {
            if (!l->isVisible()) {
                continue;
            }
            for (auto&& e: l->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT && static_cast<const Text*>(e)->isMarkdown()) {
                    // A Markdown box: where the text is drawn (not where it is in the source)
                    for (const md::Rect& r: md::findText(*static_cast<const Text*>(e), utf8)) {
                        results.push_back(XojPdfRectangle(r.x, r.y, r.x + r.width, r.y + r.height));
                    }
                } else if (e->getType() == ELEMENT_TEXT) {
                    const auto r = static_cast<const Text*>(e)->findText(utf8);
                    results.insert(results.end(), r.begin(), r.end());
                }
            }
        }
    }
    std::vector<QRectF> rects;
    rects.reserve(results.size());
    for (const auto& r: results) {
        rects.push_back(QRectF(QPointF(r.x1, r.y1), QPointF(r.x2, r.y2)).normalized());
    }
    return rects;
}

}  // namespace xqt
