#include "LibraryModel.h"
#include "SyncConflicts.h"
#include "ContentFiles.h"

#include "DocumentPlaces.h"

#include <QDateTime>

#include <algorithm>
#include <map>
#include <set>

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMimeDatabase>
#include <QPointer>
#include <QTemporaryDir>
#include <QThreadPool>

#include "HitPages.h"
#include "session/FuzzyQuery.h"
#include "MdSnippets.h"
#include "Previews.h"
#include "SystemApps.h"

namespace xqt {

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
fs::path toPath(const QString& s) { return fs::path(s.toStdString()); }

QDateTime modifiedOf(const DocumentItem& item) {
    QDateTime t;
    for (const fs::path& f: {item.xopp, item.pdf, item.md, item.image, item.other}) {
        if (!f.empty()) {
            t = std::max(t, QFileInfo(qstr(f)).lastModified());
        }
    }
    return t;
}
bool isOtherKind(const DocumentItem& item) {
    const auto k = item.kind();
    return k == DocumentItem::Kind::Text || k == DocumentItem::Kind::Other;
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
    cacheUsage = {-1, 0};
    cachesRemoved = false;
    filter = lib ? lib->showFilter() : ShowFilter();
    if (lib) {
        DocumentPlaces::setLibrary(lib->root(), lib->placesFile());
        adoptFolderCaches();
        openCache();
        // A cache of the layout before the packs (in the library, or in the app cache for a library that could
        // not be written): converted first
        const CacheLocation& where = idx->location();
        for (const fs::path& old: {where.inFolder(lib->root()), where.appCacheDir()}) {
            if (LibraryIndex::hasOldLayout(old)) {
                idx->convertOldLayout(old);
            }
        }
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
    Q_EMIT cacheChanged();
    Q_EMIT showChanged();
    refresh();
}

void LibraryModel::adoptFolderCaches() {
    // A library without a cache setting that goes into the app cache by default (Android), but has cache folders
    // of its own (from before that default, or from a desktop): they are moved there once, so nothing is read again
    // and the library's folders are left clean. The setting is written, so this happens only once.
    if (lib->hasCacheSetting() || lib->cacheMode() != CacheLocation::Mode::AppCache) {
        return;
    }
    const CacheLocation folders(lib->root(), CacheLocation::Mode::Folders);
    const auto all = allFolders();
    std::error_code ec;
    if (std::none_of(all.begin(), all.end(), [&](const fs::path& f) { return fs::is_directory(folders.inFolder(f), ec); })) {
        return;
    }
    lib->setCacheMode(CacheLocation::Mode::AppCache);
    CacheFolders::move(folders, lib->cacheLocation(), all);
}

void LibraryModel::openCache() {
    const CacheLocation where = lib->cacheLocation();
    PreviewCache::setLibrary(where);
    idx = std::make_unique<LibraryIndex>(lib->root(), where);
    connect(idx.get(), &LibraryIndex::progress, this, [this] {
        Q_EMIT indexChanged();
        if (fuzzy && !idx->busy()) {
            idx->prepareWords();  // (the words of the text, for fuzzy terms)
        }
        if (!query.isEmpty() && !searchTimer.isActive()) {
            searchTimer.start();  // new text: search again (not on every document)
        }
        if (!rows.empty()) {
            Q_EMIT dataChanged(index(0), index(static_cast<int>(rows.size()) - 1), {PageCountRole});
        }
    });
}

std::vector<fs::path> LibraryModel::allFolders() const {
    auto folders = DocumentFiles::foldersRecursive(lib->root());
    folders.insert(folders.begin(), lib->root());
    return folders;
}

bool LibraryModel::cacheInAppCache() const {
    return lib && lib->cacheMode() == CacheLocation::Mode::AppCache;
}

void LibraryModel::setCacheInAppCache(bool inAppCache) {
    if (!lib || cachesRemoved || inAppCache == cacheInAppCache()) {
        return;
    }
    // Everything written where it is now, then moved
    const CacheLocation from = idx->location();
    idx.reset();
    PreviewCache::setLibrary({});
    lib->setCacheMode(inAppCache ? CacheLocation::Mode::AppCache : CacheLocation::Mode::Folders);
    CacheFolders::move(from, lib->cacheLocation(), allFolders());
    openCache();
    Q_EMIT cacheChanged();
    refresh();
    measureCache();
}

QString LibraryModel::appCachePath() const { return lib ? qstr(lib->cacheLocation().appCacheDir()) : QString(); }

void LibraryModel::measureCache() {
    if (!lib) {
        return;
    }
    QPointer<LibraryModel> self(this);
    const quint64 generation = ++cacheCounts;
    QThreadPool::globalInstance()->start([self, generation, location = lib->cacheLocation(), folders = allFolders()] {
        const auto usage = CacheFolders::usage(location, folders);
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, generation, usage] {
                    if (self && generation == self->cacheCounts) {
                        self->cacheUsage = usage;
                        Q_EMIT self->cacheChanged();
                    }
                },
                Qt::QueuedConnection);
    });
}

qint64 LibraryModel::removeCaches() {
    if (!lib) {
        return 0;
    }
    // Nothing is written or indexed any more (the app closes after it, so the caches are not built again at once)
    idx->discard();
    PreviewCache::discard();
    cachesRemoved = true;
    ++cacheCounts;
    const qint64 removed = CacheFolders::removeAll(idx->location(), allFolders());
    cacheUsage = CacheFolders::usage(idx->location(), allFolders());
    Q_EMIT cacheChanged();
    return removed;
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

void LibraryModel::setFuzzySearch(bool on) {
    if (on != fuzzy) {
        fuzzy = on;
        Q_EMIT fuzzySearchChanged();
        if (fuzzy && idx) {
            idx->prepareWords();
        }
        if (!query.isEmpty()) {
            Q_EMIT searchChanged();  // (its hint)
            rebuild();
        }
    }
}

QString LibraryModel::searchHint() const {
    return fuzzy && !query.trimmed().isEmpty() ? FuzzyQuery(query).hint() : QString();
}

LibraryModel::Row LibraryModel::itemRow(const DocumentItem& item) {
    Row r;
    r.path = item.main();
    r.item = item;
    r.name = QString::fromStdString(item.name());
    r.modified = modifiedOf(item);
    if (isOtherKind(item)) {
        std::error_code ec;
        const auto size = fs::file_size(item.other, ec);
        r.size = ec ? -1 : static_cast<qint64>(size);
        r.icon = fileIconOf(item.other);
    }
    return r;
}

LibraryModel::Row LibraryModel::folderRow(const fs::path& f) const {
    Row r;
    r.isFolder = true;
    r.path = f;
    r.name = QString::fromStdString(f.filename().string());
    r.modified = QFileInfo(qstr(f)).lastModified();
    const auto inside = DocumentFiles::scan(f, filter.include());
    r.itemCount = static_cast<int>(inside.folders.size() +
                                   std::count_if(inside.items.begin(), inside.items.end(),
                                                 [this](const DocumentItem& i) { return filter.shows(i); }));
    return r;
}

std::vector<LibraryModel::Row> LibraryModel::fuzzyRows(const FuzzyQuery& parsed) {
    std::vector<Row> folderRows, docRows;
    const unsigned include = filter.include();
    auto folderOf = [this](const fs::path& p) { return QString::fromStdString(lib->relative(p.parent_path())); };
    auto named = [&](Row& r, const FuzzyQuery::NameMatch& m) {
        r.hit.file = r.path;
        r.hit.inName = m.score > 0;
        r.hit.nameScore = m.score;
        r.hit.nameMarks = m.positions;
    };
    // Folders by their names and paths (not in the flat list when only names are searched: it shows no folders)
    if (!(onlyNames && flatView)) {
        for (const auto& f: DocumentFiles::foldersRecursive(lib->root())) {
            const QString name = QString::fromStdString(f.filename().string());
            const FuzzyQuery::NameMatch m = parsed.matchName(name, folderOf(f));
            if (parsed.evaluate([&](size_t t) { return m.found[t] != 0; })) {
                Row r = folderRow(f);
                named(r, m);
                folderRows.push_back(std::move(r));
            }
        }
    }
    auto byName = [&](const DocumentItem& item) {
        Row r = itemRow(item);
        const FuzzyQuery::NameMatch m = parsed.matchName(r.name, folderOf(item.main()));
        if (!parsed.evaluate([&](size_t t) { return m.found[t] != 0; })) {
            return false;
        }
        named(r, m);
        docRows.push_back(std::move(r));
        return true;
    };
    if (onlyNames) {
        auto all = DocumentFiles::scanRecursive(lib->root(), include);
        for (const auto& item: all) {
            if (filter.shows(item)) {
                byName(item);
            }
        }
    } else {
        for (auto& hit: idx->search(parsed)) {
            const DocumentItem item = DocumentFiles::itemOf(hit.file, include);
            if (item.valid() && filter.shows(item)) {
                Row r = itemRow(item);
                r.hit = std::move(hit);
                docRows.push_back(std::move(r));
            }
        }
        // (other files are not in the index: by their names)
        if (filter.other) {
            for (const auto& item: DocumentFiles::scanRecursive(lib->root(), DocumentFiles::OtherFiles)) {
                if (item.kind() == DocumentItem::Kind::Other) {
                    byName(item);
                }
            }
        }
    }
    // fzf's score of the names first, then exact hits in the text before fuzzy ones, then the hits, then the newest
    auto ranked = [](const Row& a, const Row& b) {
        if (a.hit.nameScore != b.hit.nameScore) {
            return a.hit.nameScore > b.hit.nameScore;
        }
        if (a.hit.fuzzyOnly != b.hit.fuzzyOnly) {
            return b.hit.fuzzyOnly;  // exact words in the text before words that only match fuzzily
        }
        if (a.hit.count != b.hit.count) {
            return a.hit.count > b.hit.count;
        }
        return a.modified > b.modified;
    };
    std::stable_sort(folderRows.begin(), folderRows.end(), ranked);
    std::stable_sort(docRows.begin(), docRows.end(), ranked);
    std::move(docRows.begin(), docRows.end(), std::back_inserter(folderRows));
    return folderRows;
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
    // The whole library: search index (with the text files when they are shown; other files are found by their
    // names), previews, folders to watch.
    auto all = DocumentFiles::scanRecursive(lib->root(), filter.text ? DocumentFiles::TextFiles : DocumentFiles::Documents);
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
    marks = query;
    if (lib && fuzzy && !query.trimmed().isEmpty()) {
        if (const FuzzyQuery parsed(query); parsed.isValid()) {
            marks = HitPageProvider::marksOf(parsed.markTerms());
            setRows(fuzzyRows(parsed));
            return;
        }
        // (not valid: searched as plain text, with a hint)
    }
    if (lib) {
        const unsigned include = filter.include();
        // The documents of the library that are shown
        auto allShown = [this, include] {
            auto all = DocumentFiles::scanRecursive(lib->root(), include);
            std::erase_if(all, [this](const DocumentItem& i) { return !filter.shows(i); });
            return all;
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
            for (const auto& item: allShown()) {
                Row r = itemRow(item);
                if (LibraryIndex::simplified(r.name).contains(q, Qt::CaseInsensitive)) {
                    r.hit.inName = true;
                    docRows.push_back(std::move(r));
                }
            }
            std::stable_sort(docRows.begin(), docRows.end(), [](const Row& a, const Row& b) {
                return DocumentFiles::namesLess(a.name, b.name);
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
            // (other files are not in the index: found by their names, after the documents found by theirs)
            std::vector<Row> otherRows;
            if (filter.other) {
                for (const auto& item: DocumentFiles::scanRecursive(lib->root(), DocumentFiles::OtherFiles)) {
                    if (item.kind() == DocumentItem::Kind::Other &&
                        LibraryIndex::simplified(QString::fromStdString(item.name())).contains(q, Qt::CaseInsensitive)) {
                        Row r = itemRow(item);
                        r.hit.file = item.main();
                        r.hit.inName = true;
                        otherRows.push_back(std::move(r));
                    }
                }
            }
            for (auto& hit: idx->search(query)) {
                const DocumentItem item = DocumentFiles::itemOf(hit.file, include);
                if (item.valid() && filter.shows(item)) {
                    if (!hit.inName && !otherRows.empty()) {
                        std::move(otherRows.begin(), otherRows.end(), std::back_inserter(newRows));
                        otherRows.clear();
                    }
                    Row r = itemRow(item);
                    r.hit = std::move(hit);
                    newRows.push_back(std::move(r));
                }
            }
            std::move(otherRows.begin(), otherRows.end(), std::back_inserter(newRows));
        } else {
            std::vector<Row> folderRows, docRows;
            if (flatView) {
                for (const auto& item: allShown()) {
                    docRows.push_back(itemRow(item));
                }
            } else {
                const auto listing = DocumentFiles::scan(currentDir(), include);
                for (const auto& f: listing.folders) {
                    folderRows.push_back(folderRow(f));
                }
                for (const auto& item: listing.items) {
                    if (filter.shows(item)) {
                        docRows.push_back(itemRow(item));
                    }
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
                    return DocumentFiles::namesLess(a.second.name, b.second.name);
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
                    return DocumentFiles::namesLess(a.name, b.name);
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
            // (other files have none: an icon of their type)
            return r.isFolder || r.item.kind() == DocumentItem::Kind::Other ? QString() : PreviewCache::url(r.item);
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
            return r.isFolder || r.hit.pageHits.empty() ? QString() : HitPageProvider::baseUrl(r.item, marks);
        case KindRole:
            return r.isFolder ? QString() : QString::fromLatin1(r.item.kindName());
        case HybridRole:
            return !r.isFolder && r.item.hybrid;
        case HitPassageListRole: {
            QVariantList passages;
            passages.reserve(static_cast<qsizetype>(r.hit.blockHits.size()));
            for (const auto& h: r.hit.blockHits) {
                passages.append(QVariantMap{{"passage", h.block}, {"count", h.count}, {"headings", h.headings}});
            }
            return passages;
        }
        case HitPassageBaseRole:
            return r.isFolder || r.hit.blockHits.empty() ? QString() : MdSnippetProvider::baseUrl(r.item, marks);
        case SizeRole:
            if (r.isFolder) {
                return -1;
            }
            if (r.size < 0) {
                std::error_code ec;
                const auto size = fs::file_size(r.path, ec);
                return ec ? qint64(-1) : static_cast<qint64>(size);
            }
            return r.size;
        case FileIconRole:
            return r.icon;
        case NameMarksRole: {
            QVariantList list;
            for (const int p: r.hit.nameMarks) {
                list.append(p);
            }
            return list;
        }
        case ConflictsRole: {
            QStringList list;
            for (const fs::path& c: r.item.conflicts) {
                list << qstr(c);
            }
            return list;
        }
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
            {HitPageBaseRole, "hitPageBase"},
            {KindRole, "kind"},
            {HybridRole, "hybrid"},
            {HitPassageListRole, "hitPassageList"},
            {HitPassageBaseRole, "hitPassageBase"},
            {SizeRole, "size"},
            {FileIconRole, "fileIcon"},
            {NameMarksRole, "nameMarks"},
            {ConflictsRole, "conflicts"}};
}

void LibraryModel::setShowFilter(const ShowFilter& f) {
    if (f == filter) {
        return;
    }
    const bool indexChanges = f.text != filter.text;
    filter = f;
    if (lib) {
        lib->setShowFilter(filter);
    }
    Q_EMIT showChanged();
    if (indexChanges) {
        refresh();  // text files come into the index, or go
    } else {
        rebuild();
    }
}

QVariantMap LibraryModel::show() const {
    return {{"notes", filter.notes},   {"pdfs", filter.pdfs}, {"onlyPdfsWithNotes", filter.onlyPdfsWithNotes},
            {"markdown", filter.markdown}, {"images", filter.images}, {"text", filter.text},
            {"other", filter.other}};
}

void LibraryModel::setShown(const QString& key, bool shown) {
    ShowFilter f = filter;
    bool* flags[] = {&f.notes, &f.pdfs, &f.onlyPdfsWithNotes, &f.markdown, &f.images, &f.text, &f.other};
    const char* keys[] = {"notes", "pdfs", "onlyPdfsWithNotes", "markdown", "images", "text", "other"};
    for (size_t i = 0; i < std::size(keys); ++i) {
        if (key == QLatin1String(keys[i])) {
            *flags[i] = shown;
            setShowFilter(f);
            return;
        }
    }
}

void LibraryModel::resetShown() { setShowFilter(ShowFilter()); }

QString LibraryModel::fileIconOf(const fs::path& file) {
    if (DocumentFiles::isTextFile(file)) {
        return QStringLiteral("xqt-file-code");
    }
    // The common ones by their extension (the MIME database of a system can be changed by an office suite)
    static const std::map<QString, QString> known{
            {"doc", "xqt-file-doc"},           {"docx", "xqt-file-doc"},         {"odt", "xqt-file-doc"},
            {"rtf", "xqt-file-doc"},           {"pages", "xqt-file-doc"},        {"epub", "xqt-file-doc"},
            {"xls", "xqt-file-spreadsheet"},   {"xlsx", "xqt-file-spreadsheet"}, {"ods", "xqt-file-spreadsheet"},
            {"numbers", "xqt-file-spreadsheet"}, {"ppt", "xqt-file-slides"},     {"pptx", "xqt-file-slides"},
            {"odp", "xqt-file-slides"},        {"key", "xqt-file-slides"},       {"zip", "xqt-file-archive"},
            {"7z", "xqt-file-archive"},        {"rar", "xqt-file-archive"},      {"tar", "xqt-file-archive"},
            {"gz", "xqt-file-archive"},        {"tgz", "xqt-file-archive"},      {"bz2", "xqt-file-archive"},
            {"xz", "xqt-file-archive"}};
    if (auto it = known.find(QString::fromStdString(file.extension().string()).mid(1).toLower()); it != known.end()) {
        return it->second;
    }
    static const QMimeDatabase db;
    const QMimeType type = db.mimeTypeForFile(qstr(file), QMimeDatabase::MatchExtension);
    const QString name = type.name(), generic = type.genericIconName();
    if (generic == QLatin1String("x-office-spreadsheet") || name.contains(QLatin1String("spreadsheet")) ||
        name == QLatin1String("text/csv")) {
        return QStringLiteral("xqt-file-spreadsheet");
    }
    if (generic == QLatin1String("x-office-presentation") || name.contains(QLatin1String("presentation"))) {
        return QStringLiteral("xqt-file-slides");
    }
    if (generic == QLatin1String("x-office-document") || name.contains(QLatin1String("wordprocessing")) ||
        name.contains(QLatin1String("msword")) || name.contains(QLatin1String("opendocument.text")) ||
        name == QLatin1String("application/rtf") || name == QLatin1String("application/epub+zip")) {
        return QStringLiteral("xqt-file-doc");
    }
    if (generic == QLatin1String("package-x-generic") || name.contains(QLatin1String("zip")) ||
        name.contains(QLatin1String("compressed")) || name.contains(QLatin1String("-tar"))) {
        return QStringLiteral("xqt-file-archive");
    }
    if (name.startsWith(QLatin1String("audio/"))) {
        return QStringLiteral("xqt-file-audio");
    }
    if (name.startsWith(QLatin1String("video/"))) {
        return QStringLiteral("xqt-file-video");
    }
    if (name.startsWith(QLatin1String("image/"))) {
        return QStringLiteral("xqt-file-image");
    }
    if (name.startsWith(QLatin1String("text/"))) {
        return QStringLiteral("xqt-file-code");
    }
    return QStringLiteral("xqt-file");
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
    QStringList foreign;
    const fs::path target = dirOf(folder);
    for (const QUrl& u: urls) {
        if (ContentFiles::isForeign(u)) {
            foreign << ContentFiles::sourceOf(u);
            continue;
        }
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
    copyInBackground(std::move(files), target, foreign);
}

void LibraryModel::copyInBackground(std::vector<fs::path> files, fs::path target, QStringList foreign) {
    if (files.empty() && foreign.isEmpty()) {
        return;
    }
    ++importJobs;
    Q_EMIT importingChanged();
    QPointer<LibraryModel> self(this);
    // Copying (large PDFs) and rewriting .xopp files in the background. Text and other files too when they are
    // shown.
    QThreadPool::globalInstance()->start([self, files, target, foreign, include = filter.include()]() mutable {
        int imported = 0;
        QStringList errors;
        // Other apps' files first into a folder of our own (their names made safe), then imported from there
        QTemporaryDir staging(QDir::tempPath() + "/xqt-import-XXXXXX");
        for (const QString& source: foreign) {
            std::string error;
            const fs::path copy = ContentFiles::copyInto(source, fs::path(staging.path().toStdString()), error);
            if (!copy.empty()) {
                files.push_back(copy);
            }
            if (!error.empty()) {
                errors << QString::fromStdString(error);
            }
        }
        for (const auto& f: files) {
            const auto r = DocumentFiles::import(f, target, include);
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

QString LibraryModel::newTextFilePath(const QString& name, const QString& extension) const {
    if (!lib) {
        return {};
    }
    const std::string base = name.trimmed().isEmpty() || !DocumentFiles::validName(name.trimmed().toStdString())
                                     ? std::string("Untitled")
                                     : name.trimmed().toStdString();
    const fs::path dir = currentDir();
    const std::string ext = extension.toStdString();
    std::error_code ec;
    // (a .md takes a name like every document: not next to a .xopp or PDF of that name)
    std::string stem = ext == ".md" ? DocumentFiles::uniqueName(dir, base) : base;
    for (int i = 2; fs::exists(dir / (stem + ext), ec); ++i) {
        stem = base + " (" + std::to_string(i) + ")";
    }
    return qstr(dir / (stem + ext));
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
                                               : DocumentFiles::move(DocumentFiles::itemOf(f, DocumentFiles::AllFiles), target);
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
                                               : DocumentFiles::trash(DocumentFiles::itemOf(f, DocumentFiles::AllFiles));
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

bool LibraryModel::canTrash() { return SystemApps::canTrash(); }

QVariantList LibraryModel::conflictsOf(const QString& path) const {
    const fs::path file = toPath(path);
    const auto listing = DocumentFiles::scan(file.parent_path(), DocumentFiles::AllFiles);
    auto describe = [](const fs::path& f) {
        const QFileInfo info(qstr(f));
        return QVariantMap{{"path", qstr(f)},
                           {"name", QString::fromStdString(f.filename().string())},
                           {"modified", info.lastModified()},
                           {"size", info.size()}};
    };
    QVariantList list;
    for (const DocumentItem& item: listing.items) {
        if (!item.has(file) || item.conflicts.empty()) {
            continue;
        }
        QVariantMap own = describe(item.main());
        own["original"] = true;
        list.append(own);
        for (const fs::path& c: item.conflicts) {
            QVariantMap m = describe(c);
            if (const auto conflict = SyncConflicts::parse(c.filename().string())) {
                m["app"] = QString::fromStdString(conflict->app);
                m["when"] = QString::fromStdString(conflict->when);
                m["original"] = false;
                m["of"] = QString::fromStdString(conflict->original);
            }
            list.append(m);
        }
    }
    return list;
}

bool LibraryModel::resolveConflict(const QString& conflictPath, bool keepCopy) {
    const fs::path copy = toPath(conflictPath);
    const auto conflict = SyncConflicts::parse(copy.filename().string());
    std::error_code ec;
    if (!conflict || !fs::exists(copy, ec)) {
        Q_EMIT error(tr("%1 is not there any more.").arg(qstr(copy.filename())));
        return false;
    }
    const fs::path original = copy.parent_path() / conflict->original;
    // What goes: to the trash, or deleted where there is none (the window asked)
    auto remove = [this](const fs::path& f) {
        if (SystemApps::canTrash()) {
            if (!SystemApps::instance().moveToTrash(qstr(f))) {
                Q_EMIT error(tr("Could not move %1 to the trash.").arg(qstr(f.filename())));
                return false;
            }
            return true;
        }
        std::error_code rec;
        if (!fs::remove(f, rec) || rec) {
            Q_EMIT error(tr("Could not delete %1.").arg(qstr(f.filename())));
            return false;
        }
        return true;
    };
    DocumentFiles::Result r;
    if (!keepCopy) {
        if (!remove(copy)) {
            return false;
        }
    } else {
        if (fs::exists(original, ec) && !remove(original)) {
            return false;
        }
        fs::rename(copy, original, ec);
        if (ec) {
            Q_EMIT error(tr("Could not rename %1: %2").arg(qstr(copy.filename()), QString::fromStdString(ec.message())));
            return false;
        }
        r.ok = true;
        r.moved.emplace_back(copy, original);
        if (onFilesChanged) {
            onFilesChanged(r);  // (a tab of the copy follows it; one of the document reads the file again)
        }
    }
    refresh();
    return true;
}

int LibraryModel::rowOf(const QString& path) const {
    const fs::path p = toPath(path);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].path == p || rows[i].item.has(p)) {
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
