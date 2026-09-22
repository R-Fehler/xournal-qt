#include "TabManager.h"

#include <algorithm>
#include <shared_mutex>

#include <QFileInfo>
#include <QRectF>

#include "CanvasPage.h"
#include "CanvasView.h"
#include "DocumentFiles.h"
#include "DocumentPlaces.h"
#include "Previews.h"
#include "PageSketches.h"
#include "Thumbnails.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

namespace xqt {

TabManager::TabManager(AppContext& app, QObject* parent): QAbstractListModel(parent), app(app) {
    connect(&PageSketches::instance(), &PageSketches::changed, this, [this](qulonglong id) {
        for (const auto& t: tabs) {
            if (ThumbnailProvider::idOf(t.session.get()) == id) {
                tabDataChanged(t.session.get(), {SketchRole});
            }
        }
    });
}

namespace {
/// Where a document was left, saved or not (to open it there again, if wanted - see DocumentPlaces); a PDF without a
/// .xopp as well. A new document has no file yet: nothing to keep.
void rememberPlace(const DocumentSession* s) {
    if (const fs::path file = s ? s->documentFile() : fs::path(); !file.empty()) {
        const fs::path key = DocumentPlaces::keyOf(file);
        DocumentPlaces::setLastPage(key, static_cast<int>(s->getCurrentPageNo()));
        DocumentPlaces::setRead(key);
    }
}
}  // namespace

TabManager::~TabManager() {
    beginResetModel();
    for (auto& t: tabs) {
        rememberPlace(t.session.get());
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
            // On its title page and saved as it is: the stored preview of the library (nothing to draw)
            // (also a PDF that has no .xopp yet)
            if (const fs::path file = s->documentFile(); !file.empty() && !s->isModified()) {
                const DocumentItem item = DocumentFiles::itemOf(file);
                if (item.valid() && s->getCurrentPageNo() ==
                                            static_cast<size_t>(DocumentPlaces::titlePage(DocumentPlaces::keyOf(item)))) {
                    return PreviewCache::url(item);
                }
            }
            // Else the tab's current page as it is now (for the tab overview), see ThumbnailProvider.
            return QString("image://thumbnail/%1/%2/%3")
                    .arg(ThumbnailProvider::idOf(s))
                    .arg(s->getCurrentPageNo())
                    .arg(s->pageRevision(s->getCurrentPageNo()));
        case PageCountRole:
            return static_cast<int>(s->getDocument()->getPageCount());
        case SketchRole:
            return PageSketches::instance().url(ThumbnailProvider::idOf(s), s->pageId(s->getCurrentPageNo()));
        case SearchHitsRole:
            return static_cast<int>(s->search().hits().size());
        case SearchRunningRole:
            return s->search().isRunning();
        case HitPagesRole: {
            // As the page grid marks them (PagesModel), grouped by page
            QVariantList pages;
            const auto& hits = s->search().hits();
            Document* doc = s->getDocument();
            std::shared_lock lock(*doc);
            for (auto it = hits.begin(); it != hits.end();) {
                const size_t page = it->page;
                const auto end = std::find_if(it, hits.end(), [page](const DocumentSearch::Hit& h) { return h.page != page; });
                const PageRef p = page < doc->getPageCount() ? doc->getPage(page) : PageRef();
                const double w = p ? p->getWidth() : 1, h = p ? p->getHeight() : 1.414;
                const auto step = std::max<std::ptrdiff_t>(1, ((end - it) + MAX_PAGE_HITS - 1) / MAX_PAGE_HITS);
                QVariantList rects;
                for (auto r = it; r < end; r += step) {
                    rects.append(QRectF(r->rect.x() / w, r->rect.y() / h, r->rect.width() / w, r->rect.height() / h));
                }
                pages.append(QVariantMap{
                        {"page", static_cast<int>(page)},
                        {"count", static_cast<int>(end - it)},
                        {"aspect", h / w},
                        {"thumbnail", QString("image://thumbnail/%1/%2/%3")
                                              .arg(ThumbnailProvider::idOf(s))
                                              .arg(page)
                                              .arg(s->pageRevision(page))},
                        {"rects", rects}});
                it = end;
            }
            return pages;
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> TabManager::roleNames() const {
    return {{TitleRole, "title"},         {ModifiedRole, "modified"},     {FilePathRole, "filePath"},
            {CurrentRole, "current"},     {ThumbnailRole, "thumbnail"}, {PageCountRole, "pageCount"},
            {SearchHitsRole, "searchHits"}, {SearchRunningRole, "searchRunning"}, {HitPagesRole, "hitPages"},
            {SketchRole, "sketch"}};
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
    Tab tab;
    tab.view = std::make_unique<CanvasView>(*session);
    tab.session = std::move(session);
    return insertTab(std::move(tab));
}

void TabManager::listenTo(Tab& tab) {
    DocumentSession* s = tab.session.get();
    connect(s, &DocumentSession::modifiedChanged, this, [this, s] { tabDataChanged(s, {ModifiedRole, ThumbnailRole}); });
    connect(s, &DocumentSession::filePathChanged, this,
            [this, s] { tabDataChanged(s, {TitleRole, FilePathRole, ThumbnailRole}); });
    ThumbnailProvider::registerSession(s);
    auto thumbnailChanged = [this, s] { tabDataChanged(s, {ThumbnailRole, PageCountRole, SketchRole}); };
    connect(s, &DocumentSession::pageRevisionsChanged, this, thumbnailChanged);
    auto searchChanged = [this, s] { tabDataChanged(s, {SearchHitsRole, SearchRunningRole, HitPagesRole}); };
    connect(&s->search(), &DocumentSearch::changed, this, searchChanged);
    connect(&s->search(), &DocumentSearch::finished, this, searchChanged);
    connect(s, &DocumentSession::currentPageChanged, this, thumbnailChanged);
}

int TabManager::insertTab(Tab tab) {
    const int row = tabs.empty() ? 0 : current + 1;
    listenTo(tab);
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

std::unique_ptr<TabManager::Tab> TabManager::takeTab(int index) {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    // It reports to its new list from now on
    DocumentSession* s = tabs[static_cast<size_t>(index)].session.get();
    disconnect(s, nullptr, this, nullptr);
    disconnect(&s->search(), nullptr, this, nullptr);

    std::unique_ptr<Tab> tab;
    beginRemoveRows(QModelIndex(), index, index);
    tab = std::make_unique<Tab>(std::move(tabs[static_cast<size_t>(index)]));
    tabs.erase(tabs.begin() + index);
    endRemoveRows();
    Q_EMIT countChanged();
    const int old = current;
    if (current > index || current >= count()) {
        current = std::min(current - (current > index ? 1 : 0), count() - 1);
    }
    backgroundChanged(old);
    Q_EMIT currentIndexChanged();
    Q_EMIT currentTabChanged();
    return tab;
}

int TabManager::adoptTab(std::unique_ptr<Tab> tab) {
    if (!tab || !tab->session) {
        return -1;
    }
    return insertTab(std::move(*tab));
}

void TabManager::closeTab(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    const bool wasCurrent = index == current;
    rememberPlace(tabs[static_cast<size_t>(index)].session.get());
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
    // (what the tab in the background keeps of its rendered pages: CanvasMemory)
    if (oldCurrent >= 0 && oldCurrent < count() && oldCurrent != current) {
        Q_EMIT dataChanged(index(oldCurrent), index(oldCurrent), {CurrentRole});
    }
    if (current >= 0) {
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
