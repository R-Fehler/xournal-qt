#include "VersionCompare.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <shared_mutex>

#include <QElapsedTimer>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "ReferenceMode.h"
#include "ScrollLock.h"
#include "TabManager.h"
#include "ViewController.h"

namespace xqt {

// --- the marks -----------------------------------------------------------------------------------------------------

CompareMarks& CompareMarks::instance() {
    static CompareMarks marks;
    return marks;
}

bool CompareMarks::differs(const DocumentSession* session, size_t page) const {
    const auto it = marks.find(session);
    return it != marks.end() && page < it->second.size() && it->second[page];
}

void CompareMarks::set(const DocumentSession* session, std::vector<char> pages) {
    if (!session) {
        return;
    }
    const auto it = marks.find(session);
    if (pages.empty()) {
        if (it == marks.end()) {
            return;
        }
        marks.erase(it);
    } else {
        if (it != marks.end() && it->second == pages) {
            return;
        }
        marks[session] = std::move(pages);
    }
    Q_EMIT changed(session);
}

// --- the comparison ------------------------------------------------------------------------------------------------

VersionCompare::VersionCompare(TabManager& tabs, ReferenceMode& reference, QObject* parent):
        QObject(parent), tabs(tabs), reference(reference) {
    slice.setSingleShot(true);
    slice.setInterval(0);
    connect(&slice, &QTimer::timeout, this, &VersionCompare::step);
    again.setSingleShot(true);
    again.setInterval(300);  // (writing goes on: compared again when it pauses)
    connect(&again, &QTimer::timeout, this, &VersionCompare::compute);
}

VersionCompare::~VersionCompare() { stop(); }

DocumentSession* VersionCompare::newerSession() const { return newer.data(); }
DocumentSession* VersionCompare::olderSession() const { return older.data(); }

void VersionCompare::start(DocumentSession* n, DocumentSession* o, const QString& newerTitle, const QString& olderTitle) {
    stop();
    if (!n || !o || n == o) {
        return;
    }
    newer = n;
    older = o;
    newerName = newerTitle;
    olderName = olderTitle;
    for (DocumentSession* s: {n, o}) {
        connections.push_back(connect(s, &DocumentSession::pageRevisionsChanged, this, [this] {
            again.start();
            Q_EMIT changed();  // (busy)
        }));
        connections.push_back(connect(s, &QObject::destroyed, this, [this, s] {
            CompareMarks::instance().set(s, {});  // (its QPointer may be cleared already)
            stop();
        }));
    }
    // It ends when the newer one shows another reference, or none
    connections.push_back(connect(&tabs, &TabManager::referencesChanged, this, [this] {
        if (!newer || !older || tabs.referenceOf(tabs.indexOf(newer)) != tabs.indexOf(older)) {
            stop();
        }
    }));
    connections.push_back(connect(&tabs, &TabManager::currentTabChanged, this, &VersionCompare::changed));
    // Scrolled together (the pair stays locked for the session, as any pair locked by hand)
    if (tabs.currentSession() == n && reference.canvas() && &reference.canvas()->getSession() == o) {
        reference.setScrollLocked(true);
    }
    current = -1;
    Q_EMIT positionChanged();
    compute();
}

void VersionCompare::clearMarks() {
    CompareMarks::instance().set(newer, {});
    CompareMarks::instance().set(older, {});
}

void VersionCompare::stop() {
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    slice.stop();
    again.stop();
    pending.clear();
    const bool was = active() || !newer.isNull() || !older.isNull();
    clearMarks();
    newer.clear();
    older.clear();
    signatures.clear();
    newerRevisions.clear();
    olderRevisions.clear();
    result = {};
    current = -1;
    if (was) {
        Q_EMIT changed();
        Q_EMIT positionChanged();
    }
}

void VersionCompare::close() {
    const bool onScreen = shown();
    stop();
    if (onScreen) {
        reference.close();
    }
}

bool VersionCompare::shown() const {
    return active() && tabs.currentSession() == newer && tabs.referenceOf(tabs.currentIndex()) == tabs.indexOf(older);
}

void VersionCompare::compute() {
    if (!active()) {
        return;
    }
    slice.stop();
    pending.clear();
    std::set<quint64> wanted;
    auto collect = [&](DocumentSession* s, bool newerSide, std::vector<quint64>& revisions) {
        revisions.clear();
        const auto stamps = s->pageStamps();
        for (size_t i = 0; i < stamps.size(); ++i) {
            revisions.push_back(stamps[i].revision);
            wanted.insert(stamps[i].revision);
            if (!signatures.count(stamps[i].revision)) {
                pending.push_back({newerSide, i, stamps[i].revision, stamps[i].page});
            }
        }
    };
    collect(newer, true, newerRevisions);
    collect(older, false, olderRevisions);
    // (only the pages of the two documents as they are now are kept)
    for (auto it = signatures.begin(); it != signatures.end();) {
        it = wanted.count(it->first) ? std::next(it) : signatures.erase(it);
    }
    if (pending.empty()) {
        finish();
    } else {
        slice.start();
        Q_EMIT changed();  // (busy)
    }
}

void VersionCompare::step() {
    if (!active()) {
        return;
    }
    QElapsedTimer clock;
    clock.start();
    {
        std::shared_lock lockNewer(*newer->getDocument());
        std::shared_lock lockOlder(*older->getDocument());
        while (!pending.empty() && clock.elapsed() < sliceMs) {
            const Job job = std::move(pending.back());
            pending.pop_back();
            if (job.page) {
                signatures[job.revision] = versiondiff::sigOf(*job.page).whole;
            }
        }
    }
    if (pending.empty()) {
        finish();
    } else {
        slice.start();
    }
}

void VersionCompare::finish() {
    auto wholeOf = [this](const std::vector<quint64>& revisions, std::vector<uint64_t>& out) {
        out.clear();
        for (quint64 r: revisions) {
            const auto it = signatures.find(r);
            if (it == signatures.end()) {
                return false;
            }
            out.push_back(it->second);
        }
        return true;
    };
    std::vector<uint64_t> n, o;
    if (!wholeOf(newerRevisions, n) || !wholeOf(olderRevisions, o)) {
        again.start();  // (a page changed meanwhile)
        return;
    }
    result = versiondiff::compare(o, n);
    CompareMarks::instance().set(newer, result.newerChanged);
    CompareMarks::instance().set(older, result.olderChanged);
    if (current >= changeCount()) {
        current = changeCount() - 1;
        Q_EMIT positionChanged();
    }
    Q_EMIT changed();
    Q_EMIT compared();
}

QString VersionCompare::summary() const {
    if (!active()) {
        return {};
    }
    if (busy() && result.changes.empty()) {
        return tr("Comparing…");
    }
    const auto added = static_cast<int>(result.added());
    const auto removed = static_cast<int>(result.removed());
    const int changedPages = changeCount() - added - removed;
    if (changeCount() == 0) {
        return tr("No page changed");
    }
    QStringList parts;
    if (changedPages > 0) {
        parts << (changedPages == 1 ? tr("1 page changed") : tr("%1 pages changed").arg(changedPages));
    }
    if (added > 0) {
        parts << (added == 1 ? tr("1 added", "a page") : tr("%1 added", "pages").arg(added));
    }
    if (removed > 0) {
        parts << (removed == 1 ? tr("1 removed", "a page") : tr("%1 removed", "pages").arg(removed));
    }
    return parts.join(QStringLiteral(", "));
}

size_t VersionCompare::pageInView() const {
    const int index = newer ? tabs.indexOf(newer) : -1;
    const CanvasView* v = index >= 0 ? tabs.view(index) : nullptr;
    return v ? v->currentPageNo() : 0;
}

void VersionCompare::showChange(int index) {
    if (!shown() || index < 0 || index >= changeCount()) {
        return;
    }
    current = index;
    const auto& c = result.changes[static_cast<size_t>(index)];
    CanvasView* main = tabs.currentView();
    CanvasView* ref = reference.canvas();
    if (main && ref && c.newerPage < main->pageCount() && c.olderPage < ref->pageCount()) {
        if (reference.scrollLock().locked()) {
            reference.scrollLock().showPages(c.newerPage, c.olderPage);
        } else {
            ScrollLock::showPages(main->getViewController(), c.newerPage, ref->getViewController(), c.olderPage);
        }
    }
    Q_EMIT positionChanged();
}

void VersionCompare::nextChange() {
    if (changeCount() == 0) {
        return;
    }
    const size_t here = pageInView();
    const auto& changes = result.changes;
    if (current >= 0 && changes[static_cast<size_t>(current)].newerPage == here) {
        showChange(std::min(current + 1, changeCount() - 1));
        return;
    }
    // The first change from the page in view on (the one on it too, unless it was shown)
    for (size_t k = 0; k < changes.size(); ++k) {
        if (changes[k].newerPage > here || (changes[k].newerPage == here && static_cast<int>(k) != current)) {
            showChange(static_cast<int>(k));
            return;
        }
    }
    showChange(changeCount() - 1);
}

void VersionCompare::previousChange() {
    if (changeCount() == 0) {
        return;
    }
    const size_t here = pageInView();
    const auto& changes = result.changes;
    if (current >= 0 && changes[static_cast<size_t>(current)].newerPage == here) {
        showChange(std::max(current - 1, 0));
        return;
    }
    for (size_t k = changes.size(); k-- > 0;) {
        if (changes[k].newerPage < here || (changes[k].newerPage == here && static_cast<int>(k) != current)) {
            showChange(static_cast<int>(k));
            return;
        }
    }
    showChange(0);
}

}  // namespace xqt
