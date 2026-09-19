#include "PagesModel.h"

#include <shared_mutex>

#include <QSizeF>

#include "model/Document.h"
#include "model/DocumentChangeType.h"
#include "model/XojPage.h"
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
    }
    reset();
}

void PagesModel::reset() {
    beginResetModel();
    sizes.clear();
    revisions.clear();
    changed.clear();
    if (session) {
        Document* doc = session->getDocument();
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            auto p = doc->getPage(i);
            sizes.emplace_back(p->getWidth(), p->getHeight());
            revisions.push_back(nextRevision++);
        }
        current = static_cast<int>(session->getCurrentPageNo());
    }
    endResetModel();
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
        default:
            return {};
    }
}

QHash<int, QByteArray> PagesModel::roleNames() const {
    return {{PageNumberRole, "pageNumber"}, {AspectRole, "aspect"}, {ThumbnailRole, "thumbnail"},
            {CurrentRole, "current"}};
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
    endInsertRows();
    // Page numbers after the insertion changed.
    Q_EMIT dataChanged(index(static_cast<int>(page)), index(rowCount() - 1), {PageNumberRole});
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
    endRemoveRows();
    if (static_cast<int>(page) < rowCount()) {
        Q_EMIT dataChanged(index(static_cast<int>(page)), index(rowCount() - 1), {PageNumberRole});
    }
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
