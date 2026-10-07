/*
 * xournal-qt: test helpers for the search of a document (DocumentSearch): its counts come at once, where its hits
 * are drawn is placed on demand.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRectF>

#include "session/DocumentSearch.h"

namespace xqt::test {

struct SearchHit {
    size_t page = 0;
    QRectF rect;
};

/// Waits until all counts are known.
inline bool waitForCounts(DocumentSearch& search, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    while (search.isRunning() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QCoreApplication::processEvents();
    return !search.isRunning();
}

/// The places of the hits on a page (waits until they are placed; empty if they are not in time).
inline std::vector<DocumentSearch::Place> placesOn(DocumentSearch& search, size_t page, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    const std::vector<DocumentSearch::Place>* places = nullptr;
    while (!(places = search.placesOn(page)) && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return places ? *places : std::vector<DocumentSearch::Place>();
}

/// All hits, placed, in order (waits for the counts and the places).
inline std::vector<SearchHit> placedHits(DocumentSearch& search, int ms = 10000) {
    waitForCounts(search, ms);
    std::vector<SearchHit> out;
    const auto pages = search.pages();
    for (const auto& h: pages) {
        for (const auto& p: placesOn(search, h.page, ms)) {
            out.push_back({h.page, p.rect});
        }
    }
    return out;
}

}  // namespace xqt::test
