#include "PagesModel.h"

#include <algorithm>
#include <cmath>

#include <shared_mutex>

#include <QSizeF>

#include "model/Document.h"
#include "model/DocumentChangeType.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

#include "Thumbnails.h"

namespace xqt {

PagesModel::PagesModel(QObject* parent): QAbstractListModel(parent) {
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(400);
    connect(&refreshTimer, &QTimer::timeout, this, &PagesModel::flushChanges);
}

PagesModel::~PagesModel() { unregisterListener(); }

void PagesModel::setSession(DocumentSession* s) {
    if (s == session) {
        return;
    }
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    unregisterListener();
    session = s;
    sessionId = s ? ThumbnailProvider::registerSession(s) : 0;
    if (s) {
        registerListener(s);
        connections.push_back(connect(s, &DocumentSession::pageContentChanged, this,
                                      [this](qulonglong page) { markChanged(page); }));
        connections.push_back(connect(s, &DocumentSession::currentPageChanged, this, [this](qulonglong page) {
            pageSelected(page);
        }));
        connections.push_back(connect(&s->search(), &DocumentSearch::changed, this, [this] {
            if (rowCount() > 0) {
                Q_EMIT dataChanged(index(0), index(rowCount() - 1),
                                   {SearchHitsRole, CurrentSearchHitRole, SearchHitCountRole});
            }
        }));
    }
    reset();
}

void PagesModel::reset() {
    beginResetModel();
    sizes.clear();
    revisions.clear();
    selected.clear();
    anchor = -1;
    changed.clear();
    if (session) {
        Document* doc = session->getDocument();
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            auto p = doc->getPage(i);
            sizes.emplace_back(p->getWidth(), p->getHeight());
            revisions.push_back(nextRevision++);
            selected.push_back(0);
        }
        current = static_cast<int>(session->getCurrentPageNo());
    }
    endResetModel();
    updateTypicalAspect();
    Q_EMIT selectionChanged();
    Q_EMIT countChanged();
    Q_EMIT currentPageChanged();
}

int PagesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(sizes.size());
}

int PagesModel::currentPage() const { return current; }

QVariant PagesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto row = static_cast<size_t>(index.row());
    switch (role) {
        case PageNumberRole:
            return index.row() + 1;
        case AspectRole:
            return sizes[row].width() > 0 ? sizes[row].height() / sizes[row].width() : 1.414;
        case ThumbnailRole:
            return QString("image://thumbnail/%1/%2/%3").arg(sessionId).arg(row).arg(revisions[row]);
        case CurrentRole:
            return index.row() == current;
        case PageIndexRole:
            return index.row();
        case SelectedRole:
            return row < selected.size() && selected[row];
        case SearchHitCountRole: {
            if (!session) {
                return 0;
            }
            const auto& hits = session->search().hits();
            auto [a, b] = std::equal_range(hits.begin(), hits.end(), DocumentSearch::Hit{row, {}},
                                           [](const DocumentSearch::Hit& x, const DocumentSearch::Hit& y) {
                                               return x.page < y.page;
                                           });
            return static_cast<int>(b - a);
        }
        case SearchHitsRole:
        case CurrentSearchHitRole: {
            if (!session) {
                return role == SearchHitsRole ? QVariant(QVariantList()) : QVariant(-1);
            }
            const auto& search = session->search();
            const auto& hits = search.hits();
            auto it = std::lower_bound(hits.begin(), hits.end(), row,
                                       [](const DocumentSearch::Hit& h, size_t p) { return h.page < p; });
            QVariantList rects;
            int currentOnPage = -1;
            const QSizeF size = sizes[row];
            for (; it != hits.end() && it->page == row; ++it) {
                if (static_cast<int>(it - hits.begin()) == search.currentHit()) {
                    currentOnPage = static_cast<int>(rects.size());
                }
                rects.append(QRectF(it->rect.x() / size.width(), it->rect.y() / size.height(),
                                    it->rect.width() / size.width(), it->rect.height() / size.height()));
            }
            return role == SearchHitsRole ? QVariant(rects) : QVariant(currentOnPage);
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> PagesModel::roleNames() const {
    return {{PageNumberRole, "pageNumber"},     {AspectRole, "aspect"},
            {ThumbnailRole, "thumbnail"},       {CurrentRole, "current"},
            {SearchHitsRole, "searchHits"},     {CurrentSearchHitRole, "currentSearchHit"},
            {SearchHitCountRole, "searchHitCount"}, {PageIndexRole, "pageIndex"},
            {SelectedRole, "selected"}};
}

void PagesModel::select(int page, int modifiers) {
    if (page < 0 || page >= rowCount()) {
        return;
    }
    const auto m = static_cast<Qt::KeyboardModifiers>(modifiers);
    std::vector<char> next = selected;
    if (m & Qt::ShiftModifier) {
        const int from = anchor >= 0 ? anchor : (current >= 0 && current < rowCount() ? current : page);
        if (!(m & Qt::ControlModifier)) {
            std::fill(next.begin(), next.end(), 0);
        }
        for (int i = std::min(from, page); i <= std::max(from, page); ++i) {
            next[static_cast<size_t>(i)] = 1;
        }
    } else if (m & Qt::ControlModifier) {
        next[static_cast<size_t>(page)] = !next[static_cast<size_t>(page)];
        anchor = page;
    } else {
        std::fill(next.begin(), next.end(), 0);
        next[static_cast<size_t>(page)] = 1;
        anchor = page;
    }
    setSelection(std::move(next));
}

void PagesModel::toggleSelected(int page) { select(page, Qt::ControlModifier); }

void PagesModel::selectPages(const QList<int>& pages) {
    std::vector<char> next(selected.size(), 0);
    for (int p: pages) {
        if (p >= 0 && p < rowCount()) {
            next[static_cast<size_t>(p)] = 1;
        }
    }
    if (!pages.isEmpty()) {
        anchor = pages.first();
    }
    setSelection(std::move(next));
}

void PagesModel::selectAll() { setSelection(std::vector<char>(selected.size(), 1)); }

void PagesModel::clearSelection() { setSelection(std::vector<char>(selected.size(), 0)); }

bool PagesModel::isSelected(int page) const {
    return page >= 0 && page < rowCount() && selected[static_cast<size_t>(page)];
}

QList<int> PagesModel::selectedPages() const {
    QList<int> pages;
    for (size_t i = 0; i < selected.size(); ++i) {
        if (selected[i]) {
            pages.append(static_cast<int>(i));
        }
    }
    return pages;
}

int PagesModel::selectionCount() const { return static_cast<int>(std::count(selected.begin(), selected.end(), 1)); }

void PagesModel::setSelection(std::vector<char> next) {
    if (next == selected) {
        return;
    }
    for (size_t i = 0; i < next.size(); ++i) {
        if (next[i] != selected[i]) {
            selected[i] = next[i];
            Q_EMIT dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {SelectedRole});
        }
    }
    Q_EMIT selectionChanged();
}

void PagesModel::updateTypicalAspect() {
    std::vector<qreal> aspects;
    aspects.reserve(sizes.size());
    for (const QSizeF& s: sizes) {
        if (s.width() > 0) {
            aspects.push_back(s.height() / s.width());
        }
    }
    qreal median = 1.414;
    if (!aspects.empty()) {
        std::nth_element(aspects.begin(), aspects.begin() + static_cast<std::ptrdiff_t>(aspects.size() / 2),
                         aspects.end());
        median = aspects[aspects.size() / 2];
    }
    if (std::abs(median - aspect) > 1e-6) {
        aspect = median;
        Q_EMIT typicalAspectChanged();
    }
}

void PagesModel::markChanged(size_t page) {
    if (page < revisions.size()) {
        changed.insert(page);
        refreshTimer.start();
    }
}

void PagesModel::flushChanges() {
    for (size_t page: changed) {
        if (page < revisions.size()) {
            revisions[page] = nextRevision++;
            Q_EMIT dataChanged(index(static_cast<int>(page)), index(static_cast<int>(page)), {ThumbnailRole});
        }
    }
    changed.clear();
}

void PagesModel::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE) {
        reset();
    }
}

void PagesModel::pageSizeChanged(size_t page) {
    if (page < sizes.size() && session) {
        std::shared_lock lock(*session->getDocument());
        auto p = session->getDocument()->getPage(page);
        sizes[page] = QSizeF(p->getWidth(), p->getHeight());
        revisions[page] = nextRevision++;
        Q_EMIT dataChanged(index(static_cast<int>(page)), index(static_cast<int>(page)), {AspectRole, ThumbnailRole});
        updateTypicalAspect();
    }
}

void PagesModel::pageChanged(size_t page) { markChanged(page); }

void PagesModel::pageInserted(size_t page) {
    if (!session || page > sizes.size()) {
        reset();
        return;
    }
    QSizeF size;
    {
        std::shared_lock lock(*session->getDocument());
        auto p = session->getDocument()->getPage(page);
        size = QSizeF(p->getWidth(), p->getHeight());
    }
    beginInsertRows(QModelIndex(), static_cast<int>(page), static_cast<int>(page));
    sizes.insert(sizes.begin() + static_cast<std::ptrdiff_t>(page), size);
    revisions.insert(revisions.begin() + static_cast<std::ptrdiff_t>(page), nextRevision++);
    selected.insert(selected.begin() + static_cast<std::ptrdiff_t>(page), 0);
    if (anchor >= static_cast<int>(page)) {
        ++anchor;
    }
    endInsertRows();
    // Page numbers after the insertion changed.
    Q_EMIT dataChanged(index(static_cast<int>(page)), index(rowCount() - 1), {PageNumberRole});
    updateTypicalAspect();
    Q_EMIT countChanged();
}

void PagesModel::pageDeleted(size_t page) {
    // Upstream fires this before the page is removed from the document: only use the own lists here.
    if (page >= sizes.size()) {
        return;
    }
    beginRemoveRows(QModelIndex(), static_cast<int>(page), static_cast<int>(page));
    sizes.erase(sizes.begin() + static_cast<std::ptrdiff_t>(page));
    revisions.erase(revisions.begin() + static_cast<std::ptrdiff_t>(page));
    const bool wasSelected = selected[page];
    selected.erase(selected.begin() + static_cast<std::ptrdiff_t>(page));
    if (anchor == static_cast<int>(page)) {
        anchor = -1;
    } else if (anchor > static_cast<int>(page)) {
        --anchor;
    }
    endRemoveRows();
    if (wasSelected) {
        Q_EMIT selectionChanged();
    }
    if (static_cast<int>(page) < rowCount()) {
        Q_EMIT dataChanged(index(static_cast<int>(page)), index(rowCount() - 1), {PageNumberRole});
    }
    updateTypicalAspect();
    Q_EMIT countChanged();
}

void PagesModel::pageSelected(size_t page) {
    const int old = current;
    current = static_cast<int>(page);
    if (old == current) {
        return;
    }
    if (old >= 0 && old < rowCount()) {
        Q_EMIT dataChanged(index(old), index(old), {CurrentRole});
    }
    if (current < rowCount()) {
        Q_EMIT dataChanged(index(current), index(current), {CurrentRole});
    }
    Q_EMIT currentPageChanged();
}

}  // namespace xqt
