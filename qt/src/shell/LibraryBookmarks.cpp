#include "LibraryBookmarks.h"

#include <algorithm>
#include <map>

#include "session/PageBookmarks.h"

#include "DocumentPlaces.h"
#include "HitPages.h"
#include "Library.h"
#include "LibraryModel.h"

namespace xqt {

LibraryBookmarksModel::LibraryBookmarksModel(LibraryModel* library, QObject* parent):
        QAbstractListModel(parent), library(library) {
    poll.setInterval(1000);
    connect(&poll, &QTimer::timeout, this, &LibraryBookmarksModel::changedMaybe);
    if (library) {
        connect(library, &LibraryModel::libraryChanged, this, [this] {
            seen = ~quint64(0);
            changedMaybe();
        });
        connect(library, &LibraryModel::indexChanged, this, &LibraryBookmarksModel::changedMaybe);
        for (auto signal: {&LibraryModel::showChanged, &LibraryModel::searchChanged,
                           &LibraryModel::favouritesOnlyChanged}) {
            connect(library, signal, this, [this] {
                if (isActive) {
                    refresh();
                }
            });
        }
        connect(library, &LibraryModel::favouriteToggled, this, [this] {
            if (isActive) {
                refresh();
            }
        });
    }
}

void LibraryBookmarksModel::setActive(bool on) {
    if (on == isActive) {
        return;
    }
    isActive = on;
    Q_EMIT activeChanged();
    if (on) {
        poll.start();
        refresh();
    } else {
        poll.stop();
    }
}

void LibraryBookmarksModel::changedMaybe() {
    if (!isActive || !library) {
        return;
    }
    const LibraryIndex* idx = library->searchIndex();
    if (!idx || idx->bookmarkChanges() != seen) {
        refresh();
    }
}

void LibraryBookmarksModel::refresh() {
    std::vector<Row> next;
    const LibraryIndex* idx = library ? library->searchIndex() : nullptr;
    seen = idx ? idx->bookmarkChanges() : 0;
    if (idx && library->library()) {
        const ShowFilter& filter = library->showFilter();
        const QString query = LibraryIndex::simplified(library->searchQuery()).trimmed();
        std::map<fs::path, Row> byFile;
        for (const auto& b: idx->bookmarks()) {
            auto it = byFile.find(b.file);
            if (it == byFile.end()) {
                Row r;
                r.item = DocumentFiles::itemOf(b.file, filter.include());
                if (!r.item.valid()) {
                    r.item = DocumentFiles::itemOf(b.file);  // (the index knows it, the filter decides below)
                }
                const bool askIndex = filter.kindMatters() && r.item.kind() == DocumentItem::Kind::Pdf;
                if (!r.item.valid() || !filter.shows(r.item, askIndex ? idx->pdfKind(r.item.main()) : PdfKind::Unknown) ||
                    (library->favouritesOnly() && !DocumentPlaces::favourite(DocumentPlaces::keyOf(r.item)))) {
                    r.item = DocumentItem();  // (left out)
                }
                r.name = QString::fromStdString(r.item.valid() ? r.item.name() : b.file.stem().string());
                r.folder = QString::fromStdString(library->library()->relative(b.file.parent_path()));
                it = byFile.emplace(b.file, std::move(r)).first;
            }
            if (!it->second.item.valid()) {
                continue;
            }
            Mark m{b.page, PageBookmarks::displayLabel(b.label.toStdString(), static_cast<size_t>(b.page)), b.aspect};
            if (!query.isEmpty() && !m.label.contains(query, Qt::CaseInsensitive) &&
                !it->second.name.contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            it->second.marks.push_back(std::move(m));
        }
        for (auto& [file, r]: byFile) {
            if (!r.marks.empty()) {
                std::sort(r.marks.begin(), r.marks.end(), [](const Mark& a, const Mark& b) { return a.page < b.page; });
                next.push_back(std::move(r));
            }
        }
        std::stable_sort(next.begin(), next.end(),
                         [](const Row& a, const Row& b) { return DocumentFiles::namesLess(a.name, b.name); });
    }
    beginResetModel();
    rows = std::move(next);
    endResetModel();
    Q_EMIT countChanged();
}

int LibraryBookmarksModel::total() const {
    int n = 0;
    for (const auto& r: rows) {
        n += static_cast<int>(r.marks.size());
    }
    return n;
}

int LibraryBookmarksModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant LibraryBookmarksModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows.size())) {
        return {};
    }
    const Row& r = rows[static_cast<size_t>(index.row())];
    switch (role) {
        case NameRole:
            return r.name;
        case PathRole:
            return QString::fromStdString(r.item.main().string());
        case FolderRole:
            return r.folder;
        case MarksRole: {
            QVariantList marks;
            for (const auto& m: r.marks) {
                marks.append(QVariantMap{{"page", m.page}, {"label", m.label}, {"aspect", m.aspect}});
            }
            return marks;
        }
        case PageBaseRole:
            return HitPageProvider::baseUrl(r.item, QString());
        case FavouriteRole:
            return DocumentPlaces::favourite(DocumentPlaces::keyOf(r.item));
        default:
            return {};
    }
}

QHash<int, QByteArray> LibraryBookmarksModel::roleNames() const {
    return {{NameRole, "name"},   {PathRole, "path"},         {FolderRole, "folder"},
            {MarksRole, "marks"}, {PageBaseRole, "pageBase"}, {FavouriteRole, "favourite"}};
}

}  // namespace xqt
