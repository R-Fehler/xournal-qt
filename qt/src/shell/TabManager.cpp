#include "TabManager.h"

#include <QFileInfo>

#include "CanvasPage.h"
#include "CanvasView.h"
#include "Thumbnails.h"
#include "model/Document.h"
#include "undo/UndoRedoHandler.h"
#include "session/DocumentSession.h"

namespace xqt {

TabManager::TabManager(AppContext& app, QObject* parent): QAbstractListModel(parent), app(app) {}

TabManager::~TabManager() {
    beginResetModel();
    for (auto& t: tabs) {
        ThumbnailProvider::unregisterSession(t.session.get());
        t.view.reset();  // the view refers to the session
        t.session.reset();
    }
    tabs.clear();
    endResetModel();
}

int TabManager::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant TabManager::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const DocumentSession* s = tabs[static_cast<size_t>(index.row())].session.get();
    switch (role) {
        case Qt::DisplayRole:
        case TitleRole:
            return QString::fromStdString(s->getDisplayName());
        case ModifiedRole:
            return s->isModified();
        case FilePathRole:
            return QString::fromStdString(s->getFilePath().string());
        case CurrentRole:
            return index.row() == current;
        case ThumbnailRole:
            // The tab's current page (for the tab overview), see ThumbnailProvider.
            return QString("image://thumbnail/%1/%2/%3")
                    .arg(ThumbnailProvider::idOf(s))
                    .arg(s->getCurrentPageNo())
                    .arg(tabs[static_cast<size_t>(index.row())].thumbnailRevision);
        case PageCountRole:
            return static_cast<int>(s->getDocument()->getPageCount());
        default:
            return {};
    }
}

QHash<int, QByteArray> TabManager::roleNames() const {
    return {{TitleRole, "title"},         {ModifiedRole, "modified"},     {FilePathRole, "filePath"},
            {CurrentRole, "current"},     {ThumbnailRole, "thumbnail"}, {PageCountRole, "pageCount"}};
}

int TabManager::rowOf(const DocumentSession* s) const {
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].session.get() == s) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TabManager::tabDataChanged(const DocumentSession* s, const QList<int>& roles) {
    if (int row = rowOf(s); row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), roles);
    }
}

int TabManager::addTab(std::unique_ptr<DocumentSession> session) {
    const int row = tabs.empty() ? 0 : current + 1;
    Tab tab;
    tab.view = std::make_unique<CanvasView>(*session);
    tab.session = std::move(session);
    tab.releaseTimer = std::make_unique<QTimer>();
    tab.releaseTimer->setSingleShot(true);
    DocumentSession* s = tab.session.get();
    CanvasView* v = tab.view.get();
    connect(tab.releaseTimer.get(), &QTimer::timeout, this, [v] {
        for (size_t i = 0; i < v->pageCount(); ++i) {
            v->getPage(i)->deleteViewBuffer();
        }
    });
    connect(s, &DocumentSession::modifiedChanged, this, [this, s] { tabDataChanged(s, {ModifiedRole}); });
    connect(s, &DocumentSession::filePathChanged, this,
            [this, s] { tabDataChanged(s, {TitleRole, FilePathRole}); });
    ThumbnailProvider::registerSession(s);
    auto thumbnailChanged = [this, s] {
        if (const int row = rowOf(s); row >= 0) {
            ++tabs[static_cast<size_t>(row)].thumbnailRevision;
            tabDataChanged(s, {ThumbnailRole, PageCountRole});
        }
    };
    connect(s, &DocumentSession::pageContentChanged, this, thumbnailChanged);
    connect(s, &DocumentSession::currentPageChanged, this, thumbnailChanged);

    beginInsertRows(QModelIndex(), row, row);
    tabs.insert(tabs.begin() + row, std::move(tab));
    endInsertRows();
    Q_EMIT countChanged();
    const int old = current;
    current = row;
    backgroundChanged(old >= row ? old + 1 : old);
    Q_EMIT currentIndexChanged();
    Q_EMIT currentTabChanged();
    return row;
}

void TabManager::closeTab(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    const bool wasCurrent = index == current;
    beginRemoveRows(QModelIndex(), index, index);
    Tab tab = std::move(tabs[static_cast<size_t>(index)]);
    tabs.erase(tabs.begin() + index);
    endRemoveRows();
    if (index < current || (wasCurrent && current == count())) {
        --current;
    }
    if (tabs.empty()) {
        current = -1;
    }
    Q_EMIT countChanged();
    Q_EMIT currentIndexChanged();
    if (wasCurrent) {
        backgroundChanged(-1);
        Q_EMIT currentTabChanged();
    }
    // Destroy after the UI switched away from it (and after running thumbnail renders of it finished).
    ThumbnailProvider::unregisterSession(tab.session.get());
    tab.session->deleteAutosaveFile();
    tab.view.reset();
    tab.session.reset();
}

void TabManager::moveTab(int from, int to) {
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) {
        return;
    }
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    Tab tab = std::move(tabs[static_cast<size_t>(from)]);
    tabs.erase(tabs.begin() + from);
    tabs.insert(tabs.begin() + to, std::move(tab));
    endMoveRows();
    if (current == from) {
        current = to;
    } else if (from < current && to >= current) {
        --current;
    } else if (from > current && to <= current) {
        ++current;
    }
    Q_EMIT currentIndexChanged();
}

void TabManager::setCurrentIndex(int index) {
    if (index < 0 || index >= count() || index == current) {
        return;
    }
    const int old = current;
    current = index;
    backgroundChanged(old);
    Q_EMIT currentIndexChanged();
    Q_EMIT currentTabChanged();
}

void TabManager::backgroundChanged(int oldCurrent) {
    if (oldCurrent >= 0 && oldCurrent < count() && oldCurrent != current) {
        tabs[static_cast<size_t>(oldCurrent)].releaseTimer->start(releaseDelayMs);
        Q_EMIT dataChanged(index(oldCurrent), index(oldCurrent), {CurrentRole});
    }
    if (current >= 0) {
        tabs[static_cast<size_t>(current)].releaseTimer->stop();
        Q_EMIT dataChanged(index(current), index(current), {CurrentRole});
    }
}

int TabManager::indexOfFile(const fs::path& path) const {
    for (size_t i = 0; i < tabs.size(); ++i) {
        const fs::path p = tabs[i].session->getFilePath();
        if (p.empty()) {
            continue;
        }
        std::error_code ec;
        if (fs::equivalent(p, path, ec)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool TabManager::isPristine(int index) const {
    const DocumentSession* s = session(index);
    return s && !s->hasFilePath() && !s->isModified() && !s->getUndoRedoHandler()->canUndo() &&
           !s->getUndoRedoHandler()->canRedo() && s->getDocument()->getPdfFilepath().empty();
}

DocumentSession* TabManager::session(int index) const {
    return index >= 0 && index < count() ? tabs[static_cast<size_t>(index)].session.get() : nullptr;
}

CanvasView* TabManager::view(int index) const {
    return index >= 0 && index < count() ? tabs[static_cast<size_t>(index)].view.get() : nullptr;
}

}  // namespace xqt
