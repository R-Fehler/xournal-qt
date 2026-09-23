#include "LibraryModel.h"

#include "DocumentPlaces.h"

#include <QDateTime>

#include <algorithm>
#include <set>

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QThreadPool>

#include "HitPages.h"
#include "Previews.h"

namespace xqt {

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
fs::path toPath(const QString& s) { return fs::path(s.toStdString()); }

QDateTime modifiedOf(const DocumentItem& item) {
    QDateTime t;
    for (const fs::path& f: {item.xopp, item.pdf}) {
        if (!f.empty()) {
            t = std::max(t, QFileInfo(qstr(f)).lastModified());
        }
    }
    return t;
}
}  // namespace

LibraryModel::LibraryModel(QObject* parent): QAbstractListModel(parent) {
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(400);
    connect(&refreshTimer, &QTimer::timeout, this, &LibraryModel::refresh);
    searchTimer.setSingleShot(true);
    searchTimer.setInterval(700);
    connect(&searchTimer, &QTimer::timeout, this, &LibraryModel::updateSearch);
}

LibraryModel::~LibraryModel() {
    if (lib) {
        PreviewCache::setLibrary({});  // (writes what is not written yet)
    }
}

void LibraryModel::setLibrary(std::unique_ptr<Library> library) {
    beginResetModel();
    rows.clear();
    idx.reset();
    watcher.reset();
    lib = std::move(library);
    currentFolder.clear();
    query.clear();
    if (lib) {
        const CacheLocation where(lib->root());
        PreviewCache::setLibrary(where);
        DocumentPlaces::setLibrary(lib->root(), lib->placesFile());
        idx = std::make_unique<LibraryIndex>(lib->root(), where);
        connect(idx.get(), &LibraryIndex::progress, this, [this] {
            Q_EMIT indexChanged();
            if (!query.isEmpty() && !searchTimer.isActive()) {
                searchTimer.start();  // new text: search again (not on every document)
            }
            if (!rows.empty()) {
                Q_EMIT dataChanged(index(0), index(static_cast<int>(rows.size()) - 1), {PageCountRole});
            }
        });
        watcher = std::make_unique<QFileSystemWatcher>();
        connect(watcher.get(), &QFileSystemWatcher::directoryChanged, this, [this] { refreshTimer.start(); });
    } else {
        PreviewCache::setLibrary({});
        DocumentPlaces::setLibrary({}, {});
    }
    endResetModel();
    Q_EMIT libraryChanged();
    Q_EMIT folderChanged();
    Q_EMIT searchChanged();
    refresh();
}

QString LibraryModel::name() const { return lib ? lib->name() : QString(); }

bool LibraryModel::isTemporaryFolder(const QString& path) const {
    return !path.isEmpty() && Library(toPath(path)).isTemporary();
}
QString LibraryModel::rootPath() const { return lib ? qstr(lib->root()) : QString(); }

fs::path LibraryModel::dirOf(const QString& relative) const {
    return relative.isEmpty() ? lib->root() : lib->root() / toPath(relative);
}
fs::path LibraryModel::currentDir() const { return dirOf(currentFolder); }

void LibraryModel::setFolder(const QString& folder) {
    if (!lib || folder == currentFolder) {
        return;
    }
    std::error_code ec;
    if (!fs::is_directory(dirOf(folder), ec)) {
        return;
    }
    currentFolder = folder;
    Q_EMIT folderChanged();
    if (flatView) {
        flatView = false;  // entering a folder shows it
        Q_EMIT flatChanged();
    }
    rebuild();
}

void LibraryModel::goUp() {
    if (!currentFolder.isEmpty()) {
        const int slash = currentFolder.lastIndexOf('/');
        setFolder(slash < 0 ? QString() : currentFolder.left(slash));
    }
}

QVariantList LibraryModel::breadcrumbs() const {
    QVariantList list;
    if (!lib) {
        return list;
    }
    list.append(QVariantMap{{"name", lib->name()}, {"folder", QString()}});
    QString path;
    for (const QString& part: currentFolder.split('/', Qt::SkipEmptyParts)) {
        path = path.isEmpty() ? part : path + '/' + part;
        list.append(QVariantMap{{"name", part}, {"folder", path}});
    }
    return list;
}

void LibraryModel::setFlat(bool flat) {
    if (flat != flatView) {
        flatView = flat;
        Q_EMIT flatChanged();
        rebuild();
    }
}

void LibraryModel::placesChanged() {
    if (sortKey == "read") {
        rebuild();  // (the order may be another one now)
    } else if (!rows.empty()) {
        Q_EMIT dataChanged(index(0), index(static_cast<int>(rows.size()) - 1), {LastReadRole, LastPageRole});
    }
}

void LibraryModel::setNamesOnly(bool namesOnly) {
    if (namesOnly != onlyNames) {
        onlyNames = namesOnly;
        Q_EMIT namesOnlyChanged();
        if (!query.isEmpty()) {
            rebuild();
        }
    }
}

void LibraryModel::setSortBy(const QString& key) {
    if (key != sortKey && (key == "name" || key == "modified" || key == "read")) {
        sortKey = key;
        Q_EMIT sortByChanged();
        rebuild();
    }
}

void LibraryModel::setSearchQuery(const QString& q) {
    if (q != query) {
        query = q;
        Q_EMIT searchChanged();
        rebuild();
    }
}

bool LibraryModel::indexing() const { return idx && idx->busy(); }
int LibraryModel::indexed() const { return idx ? idx->indexed() : 0; }
int LibraryModel::indexTotal() const { return idx ? idx->total() : 0; }

void LibraryModel::refresh() {
    if (!lib) {
        return;
    }
    std::error_code ec;
    if (!fs::is_directory(currentDir(), ec)) {
        currentFolder.clear();  // the folder is gone
        Q_EMIT folderChanged();
    }
    // The whole library: search index, previews, folders to watch.
    auto all = DocumentFiles::scanRecursive(lib->root());
    idx->update(all);
    QThreadPool::globalInstance()->start([all] { PreviewCache::prune(all); });
    auto folders = DocumentFiles::foldersRecursive(lib->root());
    folders.push_back(lib->root());
    watchFolders(folders);
    rebuild();
}

void LibraryModel::watchFolders(const std::vector<fs::path>& folders) {
    if (!watcher) {
        return;
    }
    QStringList wanted;
    for (const auto& f: folders) {
        wanted << qstr(f);
    }
    const QStringList current = watcher->directories();
    QStringList gone;
    for (const QString& d: current) {
        if (!wanted.contains(d)) {
            gone << d;
        }
    }
    if (!gone.isEmpty()) {
        watcher->removePaths(gone);
    }
    QStringList added;
    for (const QString& d: wanted) {
        if (!current.contains(d)) {
            added << d;
        }
    }
    if (!added.isEmpty()) {
        watcher->addPaths(added);
    }
}

void LibraryModel::rebuild() {
    std::vector<Row> newRows;
    if (lib) {
        auto itemRow = [this](const DocumentItem& item) {
            Row r;
            r.path = item.main();
            r.item = item;
            r.name = QString::fromStdString(item.name());
            r.modified = modifiedOf(item);
            return r;
        };
        auto folderRow = [](const fs::path& f) {
            Row r;
            r.isFolder = true;
            r.path = f;
            r.name = QString::fromStdString(f.filename().string());
            r.modified = QFileInfo(qstr(f)).lastModified();
            const auto inside = DocumentFiles::scan(f);
            r.itemCount = static_cast<int>(inside.folders.size() + inside.items.size());
            return r;
        };
        if (const QString q = LibraryIndex::simplified(query).trimmed(); !q.isEmpty() && onlyNames) {
            // Names only: the folders (not in the flat list, which shows no folders), then the documents
            if (!flatView) {
                for (const auto& f: DocumentFiles::foldersRecursive(lib->root())) {
                    if (LibraryIndex::simplified(QString::fromStdString(f.filename().string())).contains(q, Qt::CaseInsensitive)) {
                        Row r = folderRow(f);
                        r.hit.inName = true;
                        newRows.push_back(std::move(r));
                    }
                }
            }
            std::vector<Row> docRows;
            for (const auto& item: DocumentFiles::scanRecursive(lib->root())) {
                Row r = itemRow(item);
                if (LibraryIndex::simplified(r.name).contains(q, Qt::CaseInsensitive)) {
                    r.hit.inName = true;
                    docRows.push_back(std::move(r));
                }
            }
            std::stable_sort(docRows.begin(), docRows.end(), [](const Row& a, const Row& b) {
                return QString::localeAwareCompare(a.name, b.name) < 0;
            });
            std::move(docRows.begin(), docRows.end(), std::back_inserter(newRows));
        } else if (!q.isEmpty()) {
            // Folders whose name matches, then the documents (text and names)
            for (const auto& f: DocumentFiles::foldersRecursive(lib->root())) {
                if (QString::fromStdString(f.filename().string()).contains(q, Qt::CaseInsensitive)) {
                    Row r = folderRow(f);
                    r.hit.inName = true;
                    newRows.push_back(std::move(r));
                }
            }
            for (auto& hit: idx->search(query)) {
                const DocumentItem item = DocumentFiles::itemOf(hit.file);
                if (item.valid()) {
                    Row r = itemRow(item);
                    r.hit = std::move(hit);
                    newRows.push_back(std::move(r));
                }
            }
        } else {
            std::vector<Row> folderRows, docRows;
            if (flatView) {
                for (const auto& item: DocumentFiles::scanRecursive(lib->root())) {
                    docRows.push_back(itemRow(item));
                }
            } else {
                const auto listing = DocumentFiles::scan(currentDir());
                for (const auto& f: listing.folders) {
                    folderRows.push_back(folderRow(f));
                }
                for (const auto& item: listing.items) {
                    docRows.push_back(itemRow(item));
                }
            }
            if (sortKey == "read") {
                // Last read in this app first; never read after them, by name; folders by name
                std::vector<std::pair<qint64, Row>> keyed;
                keyed.reserve(docRows.size());
                for (auto& r: docRows) {
                    keyed.emplace_back(DocumentPlaces::lastRead(DocumentPlaces::keyOf(r.item)), std::move(r));
                }
                std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
                    if (a.first != b.first) {
                        return a.first > b.first;
                    }
                    return QString::localeAwareCompare(a.second.name, b.second.name) < 0;
                });
                docRows.clear();
                for (auto& [read, r]: keyed) {
                    docRows.push_back(std::move(r));
                }
            } else if (sortKey == "modified") {
                auto newer = [](const Row& a, const Row& b) { return a.modified > b.modified; };
                std::stable_sort(folderRows.begin(), folderRows.end(), newer);
                std::stable_sort(docRows.begin(), docRows.end(), newer);
            } else if (flatView) {
                std::stable_sort(docRows.begin(), docRows.end(), [](const Row& a, const Row& b) {
                    return QString::localeAwareCompare(a.name, b.name) < 0;
                });
            }
            newRows = std::move(folderRows);
            std::move(docRows.begin(), docRows.end(), std::back_inserter(newRows));
        }
    }
    setRows(std::move(newRows));
}

void LibraryModel::setRows(std::vector<Row> newRows) {
    std::set<fs::path> shown;
    for (const auto& r: newRows) {
        shown.insert(r.path);
    }
    if (selection.keepOnly(shown)) {
        Q_EMIT selectionChanged();
    }
    const bool sameRows = newRows.size() == rows.size() &&
                          std::equal(newRows.begin(), newRows.end(), rows.begin(),
                                     [](const Row& a, const Row& b) { return a.path == b.path; });
    if (sameRows) {
        // Keeps the grid where it is (a reset would scroll it to the top).
        rows = std::move(newRows);
        if (!rows.empty()) {
            Q_EMIT dataChanged(index(0), index(static_cast<int>(rows.size()) - 1));
        }
        return;
    }
    beginResetModel();
    rows = std::move(newRows);
    endResetModel();
    Q_EMIT countChanged();
}

void LibraryModel::updateSearch() {
    if (!query.isEmpty()) {
        rebuild();
    }
}

int LibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant LibraryModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() >= static_cast<int>(rows.size())) {
        return {};
    }
    const Row& r = rows[static_cast<size_t>(i.row())];
    switch (role) {
        case NameRole:
            return r.name;
        case IsFolderRole:
            return r.isFolder;
        case PathRole:
            return qstr(r.path);
        case LocationRole: {
            const std::string rel = lib->relative(r.path.parent_path());
            return QString::fromStdString(rel);
        }
        case PreviewRole:
            return r.isFolder ? QString() : PreviewCache::url(r.item);
        case ModifiedRole:
            return r.modified;
        case HasPdfRole:
            return !r.item.pdf.empty();
        case HasXoppRole:
            return !r.item.xopp.empty();
        case PageCountRole:
            return r.isFolder || !idx ? -1 : idx->pageCount(r.path);
        case HitsRole:
            return r.hit.count;
        case HitPagesRole:
            return r.hit.pages;
        case FirstHitPageRole:
            return r.hit.firstPage;
        case NameMatchRole:
            return r.hit.inName;
        case LastReadRole: {
            if (r.isFolder) {
                return {};
            }
            const qint64 read = DocumentPlaces::lastRead(DocumentPlaces::keyOf(r.item));
            return read < 0 ? QVariant() : QVariant(QDateTime::fromSecsSinceEpoch(read));
        }
        case LastPageRole:
            return r.isFolder ? -1 : DocumentPlaces::lastPage(DocumentPlaces::keyOf(r.item));
        case SnippetRole:
            return r.hit.snippet;
        case ItemCountRole:
            return r.itemCount;
        case SelectedRole:
            return selection.contains(r.path);
        case HitPageListRole: {
            QVariantList pages;
            pages.reserve(static_cast<qsizetype>(r.hit.pageHits.size()));
            for (const auto& h: r.hit.pageHits) {
                pages.append(QVariantMap{{"page", h.page}, {"count", h.count}, {"aspect", h.aspect}});
            }
            return pages;
        }
        case HitPageBaseRole:
            return r.isFolder || r.hit.pageHits.empty() ? QString() : HitPageProvider::baseUrl(r.item, query);
        default:
            return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{NameRole, "name"},
            {IsFolderRole, "isFolder"},
            {PathRole, "path"},
            {LocationRole, "location"},
            {PreviewRole, "preview"},
            {ModifiedRole, "modified"},
            {HasPdfRole, "hasPdf"},
            {HasXoppRole, "hasXopp"},
            {PageCountRole, "pageCount"},
            {HitsRole, "hits"},
            {HitPagesRole, "hitPages"},
            {FirstHitPageRole, "firstHitPage"},
            {NameMatchRole, "nameMatch"},
            {SnippetRole, "snippet"},
            {ItemCountRole, "itemCount"},
            {SelectedRole, "selected"},
            {LastReadRole, "lastRead"},
            {LastPageRole, "lastPage"},
            {HitPageListRole, "hitPageList"},
            {HitPageBaseRole, "hitPageBase"}};
}

void LibraryModel::filesMoved(const DocumentFiles::Result& r) {
    if (idx && !r.moved.empty()) {
        followMoves(r.moved);
        refresh();
    }
}

void LibraryModel::followMoves(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    if (idx && !moves.empty()) {
        idx->moved(moves);
        DocumentPlaces::moved(moves);  // before the refresh: the entries are not read again
        PreviewCache::moved(moves);
    }
}

void LibraryModel::applyResult(const DocumentFiles::Result& r) {
    if (!r.ok) {
        Q_EMIT error(QString::fromStdString(r.error));
        return;
    }
    followMoves(r.moved);
    if (onFilesChanged) {
        onFilesChanged(r);
    }
    refresh();
}

bool LibraryModel::createFolder(const QString& name) {
    if (!lib) {
        return false;
    }
    const auto r = DocumentFiles::createFolder(currentDir(), name.trimmed().toStdString());
    applyResult(r);
    return r.ok;
}

bool LibraryModel::rename(int row, const QString& name) {
    if (!lib || row < 0 || row >= count()) {
        return false;
    }
    const Row& r = rows[static_cast<size_t>(row)];
    const auto result = r.isFolder ? DocumentFiles::renameFolder(r.path, name.trimmed().toStdString())
                                   : DocumentFiles::rename(r.item, name.trimmed().toStdString());
    applyResult(result);
    return result.ok;
}

bool LibraryModel::moveTo(int row, const QString& folder) {
    if (!lib || row < 0 || row >= count()) {
        return false;
    }
    const Row& r = rows[static_cast<size_t>(row)];
    if (selection.contains(r.path) && selection.paths.size() > 1) {
        return transfer(selectedPaths(), folder, false);  // the whole selection was dragged
    }
    const fs::path target = dirOf(folder);
    const auto result = r.isFolder ? DocumentFiles::moveFolder(r.path, target) : DocumentFiles::move(r.item, target);
    applyResult(result);
    return result.ok;
}

bool LibraryModel::trash(int row) {
    if (!lib || row < 0 || row >= count()) {
        return false;
    }
    const Row& r = rows[static_cast<size_t>(row)];
    const auto result = r.isFolder ? DocumentFiles::trashFolder(r.path) : DocumentFiles::trash(r.item);
    applyResult(result);
    return result.ok;
}

void LibraryModel::importUrls(const QList<QUrl>& urls, const QString& folder) {
    if (!lib || urls.isEmpty()) {
        return;
    }
    std::vector<fs::path> files;
    const fs::path target = dirOf(folder);
    for (const QUrl& u: urls) {
        if (!u.isLocalFile()) {
            continue;
        }
        const fs::path f = toPath(u.toLocalFile());
        std::error_code ec;
        if (fs::equivalent(f.parent_path(), target, ec)) {
            continue;  // dropped where it already is
        }
        files.push_back(f);
    }
    copyInBackground(std::move(files), target);
}

void LibraryModel::copyInBackground(std::vector<fs::path> files, fs::path target) {
    if (files.empty()) {
        return;
    }
    ++importJobs;
    Q_EMIT importingChanged();
    QPointer<LibraryModel> self(this);
    // Copying (large PDFs) and rewriting .xopp files in the background.
    QThreadPool::globalInstance()->start([self, files, target] {
        int imported = 0;
        QStringList errors;
        for (const auto& f: files) {
            const auto r = DocumentFiles::import(f, target);
            imported += r.documents;
            if (!r.error.empty()) {
                errors << QString::fromStdString(r.error);
            }
        }
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, imported, errors] {
                    if (!self) {
                        return;
                    }
                    --self->importJobs;
                    Q_EMIT self->importingChanged();
                    self->refresh();
                    if (!errors.isEmpty()) {
                        Q_EMIT self->error(errors.join('\n'));
                    }
                    Q_EMIT self->imported(imported);
                },
                Qt::QueuedConnection);
    });
}

QVariantList LibraryModel::folderList() const {
    QVariantList list;
    if (!lib) {
        return list;
    }
    list.append(QVariantMap{{"folder", QString()}, {"name", lib->name()}, {"depth", 0}});
    for (const auto& f: DocumentFiles::foldersRecursive(lib->root())) {
        const QString rel = QString::fromStdString(lib->relative(f));
        list.append(QVariantMap{{"folder", rel},
                                {"name", QString::fromStdString(f.filename().string())},
                                {"depth", static_cast<int>(rel.count('/')) + 1}});
    }
    return list;
}

QString LibraryModel::newDocumentPath(const QString& name) const {
    if (!lib) {
        return {};
    }
    const std::string base = name.trimmed().isEmpty() || !DocumentFiles::validName(name.trimmed().toStdString())
                                     ? std::string("Untitled")
                                     : name.trimmed().toStdString();
    const fs::path dir = currentDir();
    return qstr(dir / (DocumentFiles::uniqueName(dir, base) + ".xopp"));
}

void LibraryModel::selectionUpdated() {
    if (!rows.empty()) {
        Q_EMIT dataChanged(index(0), index(count() - 1), {SelectedRole});
    }
    Q_EMIT selectionChanged();
}

void LibraryModel::select(int row, int modifiers) {
    selection.click(row, Qt::KeyboardModifiers(modifiers), count(),
                    [this](int i) { return rows[static_cast<size_t>(i)].path; });
    selectionUpdated();
}

void LibraryModel::toggleSelected(int row) {
    if (row >= 0 && row < count()) {
        selection.toggle(rows[static_cast<size_t>(row)].path);
        selectionUpdated();
    }
}

void LibraryModel::selectAll() {
    for (const auto& r: rows) {
        selection.paths.insert(r.path);
    }
    selectionUpdated();
}

void LibraryModel::clearSelection() {
    if (!selection.paths.empty()) {
        selection.paths.clear();
        selectionUpdated();
    }
}

QStringList LibraryModel::selectedPaths() const {
    QStringList list;
    for (const auto& r: rows) {
        if (selection.contains(r.path)) {
            list << qstr(r.path);
        }
    }
    return list;
}

QStringList LibraryModel::pathsFor(int row) const {
    if (row < 0 || row >= count()) {
        return {};
    }
    const Row& r = rows[static_cast<size_t>(row)];
    return selection.contains(r.path) ? selectedPaths() : QStringList{qstr(r.path)};
}

QStringList LibraryModel::documentsIn(const QStringList& paths) const {
    QStringList docs;
    for (const QString& p: paths) {
        std::error_code ec;
        if (!fs::is_directory(toPath(p), ec)) {
            docs << p;
        }
    }
    return docs;
}

bool LibraryModel::transfer(const QStringList& paths, const QString& folder, bool copy) {
    return lib && transferTo(paths, qstr(dirOf(folder)), copy);
}

QVariantList LibraryModel::foldersOf(const QString& rootPath) const {
    QVariantList list;
    if (rootPath.isEmpty()) {
        return list;
    }
    const Library other(toPath(rootPath));
    list.append(QVariantMap{{"folder", QString()}, {"name", other.name()}, {"depth", 0}, {"path", qstr(other.root())}});
    for (const auto& f: DocumentFiles::foldersRecursive(other.root())) {
        const QString rel = QString::fromStdString(other.relative(f));
        list.append(QVariantMap{{"folder", rel},
                                {"name", QString::fromStdString(f.filename().string())},
                                {"depth", static_cast<int>(rel.count('/')) + 1},
                                {"path", qstr(f)}});
    }
    return list;
}

bool LibraryModel::transferTo(const QStringList& paths, const QString& folder, bool copy) {
    if (paths.isEmpty() || folder.isEmpty()) {
        return false;
    }
    const fs::path target = toPath(folder);
    std::vector<fs::path> files;
    for (const QString& p: paths) {
        files.push_back(toPath(p));
    }
    clearSelection();
    if (copy) {
        copyInBackground(std::move(files), target);  // large PDFs
        return true;
    }
    QStringList errors;
    for (const auto& f: files) {
        std::error_code ec;
        const auto r = fs::is_directory(f, ec) ? DocumentFiles::moveFolder(f, target)
                                               : DocumentFiles::move(DocumentFiles::itemOf(f), target);
        if (!r.ok) {
            errors << QString::fromStdString(r.error);
            continue;
        }
        followMoves(r.moved);
        if (onFilesChanged) {
            onFilesChanged(r);
        }
    }
    refresh();
    if (!errors.isEmpty()) {
        Q_EMIT error(errors.join('\n'));
    }
    return errors.isEmpty();
}

bool LibraryModel::trashPaths(const QStringList& paths) {
    QStringList errors;
    for (const QString& p: paths) {
        const fs::path f = toPath(p);
        std::error_code ec;
        const auto r = fs::is_directory(f, ec) ? DocumentFiles::trashFolder(f)
                                               : DocumentFiles::trash(DocumentFiles::itemOf(f));
        if (!r.ok) {
            errors << QString::fromStdString(r.error);
        } else if (onFilesChanged) {
            onFilesChanged(r);
        }
    }
    clearSelection();
    refresh();
    if (!errors.isEmpty()) {
        Q_EMIT error(errors.join('\n'));
    }
    return errors.isEmpty();
}

int LibraryModel::rowOf(const QString& path) const {
    const fs::path p = toPath(path);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].path == p || rows[i].item.pdf == p || rows[i].item.xopp == p) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool LibraryModel::contains(const QUrl& url) const {
    return lib && url.isLocalFile() && lib->contains(toPath(url.toLocalFile()));
}

QString LibraryModel::relativeFolder(const QString& path) const {
    return lib ? QString::fromStdString(lib->relative(toPath(path))) : QString();
}

}  // namespace xqt
