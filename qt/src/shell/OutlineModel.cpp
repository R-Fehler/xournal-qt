#include "OutlineModel.h"

#include <cstdint>
#include <functional>
#include <map>
#include <shared_mutex>

#include "model/Document.h"
#include "DocumentChapters.h"
#include "model/DocumentOutline.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

namespace xqt {

OutlineModel::OutlineModel(QObject* parent): QAbstractListModel(parent) {}

OutlineModel::~OutlineModel() { setSession(nullptr); }

void OutlineModel::setSession(DocumentSession* s) {
    if (s == session) {
        return;
    }
    disconnect(pageConnection);
    disconnect(contentConnection);
    unregisterListener();
    session = s;
    if (session) {
        registerListener(session);
        pageConnection = connect(session, &DocumentSession::currentPageChanged, this, [this] { updateCurrent(); });
        // Chapters that the document carries itself change with it (a heading written, undone, erased)
        contentConnection = connect(session, &DocumentSession::pageContentChanged, this, [this](qulonglong) {
            if (ownChapters || all.empty()) {
                rebuild();
            }
        });
    }
    rebuild();
}

void OutlineModel::rebuild() {
    std::vector<Entry> next;
    if (session) {
        Document* doc = session->getDocument();
        std::shared_lock lock(*doc);
        std::function<void(const DocumentOutline&, int)> walk = [&](const DocumentOutline& entries, int level) {
            for (const auto& e: entries) {
                Entry x;
                x.title = QString::fromStdString(e.title);
                x.level = level;
                x.pdfPage = e.dest.getPdfPage();
                x.hasChildren = !e.children.empty();
                x.expanded = e.dest.getExpand();
                next.push_back(std::move(x));
                walk(e.children, level + 1);
            }
        };
        walk(doc->getOutline(), 0);
        if (next.empty()) {
            // No table of contents in a PDF (or no PDF at all): the chapters written in the document itself
            lock.unlock();
            for (const auto& chapter: DocumentChapters::find(*doc)) {
                Entry x;
                x.title = QString::fromStdString(chapter.title);
                x.level = chapter.level;
                x.pdfPage = npos;
                x.page = static_cast<int>(chapter.page);
                x.expanded = true;
                next.push_back(std::move(x));
            }
            ownChapters = !next.empty();
        } else {
            ownChapters = false;
        }
    }
    // The same outline (e.g. after pages changed): keep what was collapsed.
    if (next.size() == all.size()) {
        for (size_t i = 0; i < next.size(); ++i) {
            if (next[i].title == all[i].title && next[i].level == all[i].level) {
                next[i].expanded = all[i].expanded;
            }
        }
    }
    beginResetModel();
    all = std::move(next);
    updatePages();
    relayout();
    endResetModel();
    Q_EMIT countChanged();
    updateCurrent();
}

void OutlineModel::updatePages() {
    if (!session || ownChapters) {
        return;  // the chapters of the document know their page already
    }
    Document* doc = session->getDocument();
    std::shared_lock lock(*doc);
    std::map<size_t, int> firstPageOf;  // PDF page -> first document page showing it
    for (size_t i = 0; i < doc->getPageCount(); ++i) {
        const PageRef p = doc->getPage(i);
        if (p->getBackgroundType().isPdfPage()) {
            firstPageOf.emplace(p->getPdfPageNr(), static_cast<int>(i));
        }
    }
    for (auto& e: all) {
        auto it = firstPageOf.find(e.pdfPage);
        e.page = it == firstPageOf.end() ? -1 : it->second;
    }
}

void OutlineModel::relayout() {
    visible.clear();
    ends.clear();
    if (all.empty() || !session) {
        return;
    }
    int collapsedLevel = -1;
    for (size_t i = 0; i < all.size(); ++i) {
        if (collapsedLevel >= 0 && all[i].level > collapsedLevel) {
            continue;  // inside a collapsed entry
        }
        collapsedLevel = -1;
        visible.push_back(i);
        if (all[i].hasChildren && !all[i].expanded) {
            collapsedLevel = all[i].level;
        }
    }
    const int pageCount = static_cast<int>(session->getDocument()->getPageCount());
    auto pageOf = [&](size_t row) { return visible[row] == SIZE_MAX ? 0 : all[visible[row]].page; };
    // Pages before the first entry
    for (size_t row = 0; row < visible.size(); ++row) {
        if (const int p = pageOf(row); p >= 0) {
            if (p > 0) {
                visible.insert(visible.begin(), SIZE_MAX);
            }
            break;
        }
    }
    ends.resize(visible.size(), -1);
    for (size_t row = 0; row < visible.size(); ++row) {
        const int start = pageOf(row);
        if (start < 0) {
            continue;
        }
        int end = pageCount;
        for (size_t next = row + 1; next < visible.size(); ++next) {
            if (const int p = pageOf(next); p >= 0) {
                end = std::max(start, p);
                break;
            }
        }
        ends[row] = end;
    }
}

void OutlineModel::updateCurrent() {
    int row = -1;
    if (session) {
        const int page = static_cast<int>(session->getCurrentPageNo());
        for (size_t r = 0; r < visible.size(); ++r) {
            const int start = visible[r] == SIZE_MAX ? 0 : all[visible[r]].page;
            if (start >= 0 && start <= page) {
                row = static_cast<int>(r);
            }
        }
    }
    if (row != current) {
        current = row;
        Q_EMIT currentRowChanged();
    }
}

void OutlineModel::toggle(int row) {
    if (row < 0 || row >= count() || visible[static_cast<size_t>(row)] == SIZE_MAX) {
        return;
    }
    Entry& e = all[visible[static_cast<size_t>(row)]];
    if (!e.hasChildren) {
        return;
    }
    const size_t before = visible.size();
    e.expanded = !e.expanded;
    // The rows below it come or go; the rest of the list stays where it is.
    std::vector<size_t> oldVisible = visible;
    relayout();
    const int changedRows = static_cast<int>(visible.size()) - static_cast<int>(before);
    if (changedRows > 0) {
        visible.swap(oldVisible);
        beginInsertRows({}, row + 1, row + changedRows);
        visible.swap(oldVisible);
        endInsertRows();
    } else if (changedRows < 0) {
        visible.swap(oldVisible);
        beginRemoveRows({}, row + 1, row - changedRows);
        visible.swap(oldVisible);
        endRemoveRows();
    }
    if (count() > 0) {
        Q_EMIT dataChanged(index(0), index(count() - 1), {ExpandedRole, PageEndRole});
    }
    Q_EMIT countChanged();
    updateCurrent();
}

void OutlineModel::expandAll(bool expand) {
    for (auto& e: all) {
        e.expanded = expand;
    }
    beginResetModel();
    relayout();
    endResetModel();
    Q_EMIT countChanged();
    updateCurrent();
}

void OutlineModel::documentChanged(DocumentChangeType) { rebuild(); }

void OutlineModel::pageInserted(size_t) {
    beginResetModel();
    updatePages();
    relayout();
    endResetModel();
    updateCurrent();
}

void OutlineModel::pageDeleted(size_t page) { pageInserted(page); }

int OutlineModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant OutlineModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() >= count()) {
        return {};
    }
    const size_t row = static_cast<size_t>(i.row());
    const size_t idx = visible[row];
    if (idx == SIZE_MAX) {  // "Beginning"
        switch (role) {
            case TitleRole:
                return tr("Beginning");
            case LevelRole:
                return 0;
            case PageRole:
                return 0;
            case PageEndRole:
                return ends[row];
            case HasChildrenRole:
            case ExpandedRole:
                return false;
            default:
                return {};
        }
    }
    const Entry& e = all[idx];
    switch (role) {
        case TitleRole:
            return e.title;
        case LevelRole:
            return e.level;
        case PageRole:
            return e.page;
        case PageEndRole:
            return ends[row];
        case HasChildrenRole:
            return e.hasChildren;
        case ExpandedRole:
            return e.expanded;
        default:
            return {};
    }
}

QHash<int, QByteArray> OutlineModel::roleNames() const {
    return {{TitleRole, "title"},           {LevelRole, "level"},       {PageRole, "page"},
            {PageEndRole, "pageEnd"},       {HasChildrenRole, "hasChildren"}, {ExpandedRole, "expanded"}};
}

}  // namespace xqt
