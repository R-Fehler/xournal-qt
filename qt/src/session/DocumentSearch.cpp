#include "DocumentSearch.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "DocumentSession.h"
#include "MdBox.h"

namespace xqt {

namespace {
constexpr int STEP_BUDGET_MS = 8;  ///< search time per event loop iteration
}

DocumentSearch::DocumentSearch(DocumentSession& session): session(session) {
    stepTimer.setSingleShot(true);
    stepTimer.setInterval(0);
    connect(&stepTimer, &QTimer::timeout, this, &DocumentSearch::step);
    // Edited text elements: search again once the editing pauses.
    restartTimer.setSingleShot(true);
    restartTimer.setInterval(300);
    connect(&restartTimer, &QTimer::timeout, this, &DocumentSearch::restart);
    connect(&session, &DocumentSession::pageContentChanged, this, [this] {
        if (!text.isEmpty()) {
            restartTimer.start();
        }
    });
    registerListener(&session);
}

DocumentSearch::~DocumentSearch() { unregisterListener(); }

void DocumentSearch::setQuery(const QString& query, bool jump) {
    if (query == text) {
        if (jump && current < 0) {
            pendingJump = true;
            jumpToFirstFromCurrentPage();
        }
        return;
    }
    text = query;
    utf8 = query.toStdString();
    pendingJump = jump;
    jumpScrolls = true;
    startPage = session.getCurrentPageNo();
    current = -1;
    restart();
}

void DocumentSearch::restart() {
    restartTimer.stop();
    if (current >= 0) {
        // The document changed: keep the place (the first hit from the page of the old current hit), but do not
        // scroll while the user edits.
        startPage = found[static_cast<size_t>(current)].page;
        pendingJump = true;
        jumpScrolls = false;
        current = -1;
    }
    found.clear();
    nextPage = 0;
    {
        std::shared_lock lock(*session.getDocument());
        pageCount = text.isEmpty() ? 0 : session.getDocument()->getPageCount();
    }
    ++rev;
    Q_EMIT changed();
    if (isRunning()) {
        stepTimer.start();
    } else {
        Q_EMIT finished();
    }
}

void DocumentSearch::step() {
    QElapsedTimer t;
    t.start();
    const size_t before = found.size();
    while (nextPage < pageCount && t.elapsed() < STEP_BUDGET_MS) {
        searchPage(nextPage++);
    }
    if (found.size() != before) {
        ++rev;
        // The first hit at or after the start page (found in page order, so this is final once one is found).
        if (pendingJump) {
            for (size_t i = before; i < found.size(); ++i) {
                if (found[i].page >= startPage) {
                    pendingJump = false;
                    setCurrent(static_cast<int>(i), jumpScrolls);
                    break;
                }
            }
        }
        Q_EMIT changed();
    }
    if (nextPage < pageCount) {
        stepTimer.start();
        return;
    }
    if (pendingJump && !found.empty()) {  // wrap around: hits only before the start page
        pendingJump = false;
        setCurrent(0, jumpScrolls);
        Q_EMIT changed();
    }
    pendingJump = false;
    Q_EMIT finished();
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
            const bool markdown = md::isMarkdownLayer(*l);
            for (auto&& e: l->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT && markdown) {
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

void DocumentSearch::searchPage(size_t pageNo) {
    std::vector<Hit> hits;
    for (const QRectF& r: findOnPage(*session.getDocument(), pageNo, utf8)) {
        hits.push_back({pageNo, r});
    }
    // Reading order on the page (lines in 4 pt bands, then left to right).
    auto key = [](const Hit& h) { return std::pair{static_cast<long>(h.rect.top() / 4), h.rect.left()}; };
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) { return key(a) < key(b); });
    found.insert(found.end(), hits.begin(), hits.end());
}

void DocumentSearch::setCurrent(int index, bool scroll) {
    if (index < 0 || index >= static_cast<int>(found.size())) {
        return;
    }
    current = index;
    ++rev;
    if (scroll) {
        const Hit& h = found[static_cast<size_t>(index)];
        session.setCurrentPageNo(h.page);
        Q_EMIT session.scrollToRectRequested(h.page, h.rect);
    }
}

void DocumentSearch::next() {
    if (found.empty()) {
        return;
    }
    setCurrent(current < 0 ? 0 : (current + 1) % static_cast<int>(found.size()), true);
    Q_EMIT changed();
}

void DocumentSearch::previous() {
    if (found.empty()) {
        return;
    }
    const int n = static_cast<int>(found.size());
    setCurrent(current < 0 ? n - 1 : (current + n - 1) % n, true);
    Q_EMIT changed();
}

void DocumentSearch::jumpToFirstFromCurrentPage() {
    const size_t page = session.getCurrentPageNo();
    for (size_t i = 0; i < found.size(); ++i) {
        if (found[i].page >= page) {
            setCurrent(static_cast<int>(i), true);
            Q_EMIT changed();
            return;
        }
    }
    if (!found.empty() && !isRunning()) {
        setCurrent(0, true);
        Q_EMIT changed();
        return;
    }
    // Not found yet: take the first one found from the current page on.
    startPage = page;
    pendingJump = true;
    jumpScrolls = true;
}

void DocumentSearch::pageInserted(size_t) {
    if (!text.isEmpty()) {
        restartTimer.start();
    }
}

void DocumentSearch::pageDeleted(size_t) {
    if (!text.isEmpty()) {
        restartTimer.start();
    }
}

}  // namespace xqt
