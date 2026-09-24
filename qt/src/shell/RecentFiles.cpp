#include "RecentFiles.h"

#include <set>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "util/PathUtil.h"

#include "DocumentPlaces.h"
#include "Previews.h"

namespace xqt {

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
}  // namespace

RecentFiles::RecentFiles(fs::path file, QObject* parent): QAbstractListModel(parent), storeFile(std::move(file)) {
    refresh();
}

fs::path RecentFiles::defaultStoreFile() { return Util::getConfigFile("recent.json"); }

auto RecentFiles::load() const -> std::vector<Entry> {
    std::vector<Entry> entries;
    QFile f(qstr(storeFile));
    if (!f.open(QIODevice::ReadOnly)) {
        return entries;
    }
    for (const auto& v: QJsonDocument::fromJson(f.readAll()).object()["files"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString path = o["path"].toString();
        if (!path.isEmpty()) {
            entries.push_back({fs::path(path.toStdString()), QDateTime::fromString(o["opened"].toString(), Qt::ISODate)});
        }
    }
    return entries;
}

void RecentFiles::store(const std::vector<Entry>& entries) const {
    std::error_code ec;
    fs::create_directories(storeFile.parent_path(), ec);
    QJsonArray files;
    for (const auto& e: entries) {
        files.append(QJsonObject{{"path", qstr(e.path)}, {"opened", e.opened.toString(Qt::ISODate)}});
    }
    // Written next to it and renamed (no QSaveFile: its sync to disk can stall the UI for a moment).
    const fs::path tmp = fs::path(storeFile) += ".part";
    QFile f(qstr(tmp));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"files", files}}).toJson());
        f.close();
        fs::rename(tmp, storeFile, ec);
    }
}

void RecentFiles::add(const fs::path& file) {
    const fs::path p = fs::absolute(file).lexically_normal();
    auto entries = load();  // another window may have added files
    std::erase_if(entries, [&](const Entry& e) { return e.path == p; });
    entries.insert(entries.begin(), {p, QDateTime::currentDateTime()});
    if (entries.size() > MAX_ENTRIES) {
        entries.resize(MAX_ENTRIES);
    }
    store(entries);
    refresh();
}

void RecentFiles::remap(const fs::path& from, const fs::path& to) {
    auto entries = load();
    bool changed = false;
    for (auto& e: entries) {
        if (fs::path n = DocumentFiles::remap(e.path, from, to); n != e.path) {
            e.path = n;
            changed = true;
        }
    }
    if (changed) {
        store(entries);
        refresh();
    }
}

void RecentFiles::refresh() {
    std::vector<Row> newRows;
    std::set<fs::path> seen;  // a .xopp and its PDF are one document
    for (const auto& e: load()) {
        // (a text file opened in the app too; other files are opened with other apps and not listed)
        const DocumentItem item = DocumentFiles::itemOf(e.path, DocumentFiles::TextFiles);
        if (item.valid() && seen.insert(item.main()).second) {
            newRows.push_back({item, e.opened});
        }
    }
    std::set<fs::path> shown;
    for (const auto& r: newRows) {
        shown.insert(r.item.main());
    }
    const bool dropped = selection.keepOnly(shown);
    beginResetModel();
    rows = std::move(newRows);
    endResetModel();
    Q_EMIT countChanged();
    if (dropped) {
        Q_EMIT selectionChanged();
    }
}

void RecentFiles::selectionUpdated() {
    if (!rows.empty()) {
        Q_EMIT dataChanged(index(0), index(count() - 1), {SelectedRole});
    }
    Q_EMIT selectionChanged();
}

void RecentFiles::select(int row, int modifiers) {
    selection.click(row, Qt::KeyboardModifiers(modifiers), count(),
                    [this](int i) { return rows[static_cast<size_t>(i)].item.main(); });
    selectionUpdated();
}

void RecentFiles::toggleSelected(int row) {
    if (row >= 0 && row < count()) {
        selection.toggle(rows[static_cast<size_t>(row)].item.main());
        selectionUpdated();
    }
}

void RecentFiles::selectAll() {
    for (const auto& r: rows) {
        selection.paths.insert(r.item.main());
    }
    selectionUpdated();
}

void RecentFiles::clearSelection() {
    if (!selection.paths.empty()) {
        selection.paths.clear();
        selectionUpdated();
    }
}

QStringList RecentFiles::selectedPaths() const {
    QStringList list;
    for (const auto& r: rows) {
        if (selection.contains(r.item.main())) {
            list << qstr(r.item.main());
        }
    }
    return list;
}

QStringList RecentFiles::pathsFor(int row) const {
    if (row < 0 || row >= count()) {
        return {};
    }
    const fs::path p = rows[static_cast<size_t>(row)].item.main();
    return selection.contains(p) ? selectedPaths() : QStringList{qstr(p)};
}

void RecentFiles::removePaths(const QStringList& paths) {
    std::set<fs::path> gone;
    for (const QString& p: paths) {
        const DocumentItem item = DocumentFiles::itemOf(fs::path(p.toStdString()), DocumentFiles::TextFiles);
        gone.insert(fs::path(p.toStdString()));
        for (const fs::path& f: {item.xopp, item.pdf, item.md, item.image, item.other}) {
            gone.insert(f);
        }
    }
    auto entries = load();
    std::erase_if(entries, [&](const Entry& e) { return gone.count(e.path) > 0; });
    store(entries);
    clearSelection();
    refresh();
}

void RecentFiles::remove(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    const DocumentItem item = rows[static_cast<size_t>(row)].item;
    auto entries = load();
    std::erase_if(entries, [&](const Entry& e) { return item.has(e.path); });
    store(entries);
    refresh();
}

void RecentFiles::clear() {
    store({});
    refresh();
}

bool RecentFiles::rename(int row, const QString& name) {
    if (row < 0 || row >= count()) {
        return false;
    }
    const auto r = DocumentFiles::rename(rows[static_cast<size_t>(row)].item, name.trimmed().toStdString());
    if (!r.ok) {
        Q_EMIT error(QString::fromStdString(r.error));
        return false;
    }
    if (onFilesChanged) {
        onFilesChanged(r);  // also remaps this list
    } else {
        for (const auto& [from, to]: r.moved) {
            remap(from, to);
        }
    }
    return true;
}

bool RecentFiles::trash(int row) {
    if (row < 0 || row >= count()) {
        return false;
    }
    const auto r = DocumentFiles::trash(rows[static_cast<size_t>(row)].item);
    if (!r.ok) {
        Q_EMIT error(QString::fromStdString(r.error));
        return false;
    }
    if (onFilesChanged) {
        onFilesChanged(r);
    }
    refresh();
    return true;
}

int RecentFiles::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant RecentFiles::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() >= count()) {
        return {};
    }
    const Row& r = rows[static_cast<size_t>(i.row())];
    switch (role) {
        case NameRole:
            return QString::fromStdString(r.item.name());
        case PathRole:
            return qstr(r.item.main());
        case LocationRole: {
            QString folder = qstr(r.item.folder());
            const QString home = QDir::homePath();
            if (folder == home || folder.startsWith(home + '/')) {
                folder = '~' + folder.mid(home.size());
            }
            return folder;
        }
        case PreviewRole:
            return PreviewCache::url(r.item);
        case OpenedRole:
            return r.opened;
        case HasPdfRole:
            return !r.item.pdf.empty();
        case HasXoppRole:
            return !r.item.xopp.empty();
        case KindRole:
            return QString::fromLatin1(r.item.kindName());
        case SelectedRole:
            return selection.contains(r.item.main());
        case LastPageRole:
            return DocumentPlaces::lastPage(DocumentPlaces::keyOf(r.item));
        default:
            return {};
    }
}

QHash<int, QByteArray> RecentFiles::roleNames() const {
    return {{NameRole, "name"},     {PathRole, "path"},     {LocationRole, "location"}, {PreviewRole, "preview"},
            {OpenedRole, "opened"}, {HasPdfRole, "hasPdf"}, {HasXoppRole, "hasXopp"},   {SelectedRole, "selected"}, {LastPageRole, "lastPage"},
            {KindRole, "kind"}};
}

}  // namespace xqt
