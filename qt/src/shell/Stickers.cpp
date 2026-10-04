#include "Stickers.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "DocumentFiles.h"
#include "session/StickerFile.h"
#include "Previews.h"

namespace xqt::stickers {

namespace {
std::mutex appSetMutex;
fs::path appSetOverride;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool hidden(const fs::path& p) {
    const std::string name = p.filename().string();
    return name.empty() || name.front() == '.';
}

qint64 addedTime(const fs::path& file) {
    const QFileInfo info(QString::fromStdString(file.string()));
    const QDateTime born = info.birthTime();
    return (born.isValid() ? born : info.lastModified()).toMSecsSinceEpoch();
}

QJsonObject readJson(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeJson(const fs::path& file, const QJsonObject& object) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    QSaveFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return f.commit();
}

/// The order file names one sticker by another name now (or no more: `to` empty)
void renameInOrder(const fs::path& folder, const std::string& from, const std::string& to) {
    std::vector<std::string> names = readOrder(folder);
    const auto at = std::find(names.begin(), names.end(), from);
    if (at == names.end()) {
        return;
    }
    if (to.empty()) {
        names.erase(at);
    } else {
        *at = to;
    }
    writeOrder(folder, names);
}
}  // namespace

fs::path librarySet(const fs::path& libraryRoot) { return libraryRoot / FOLDER; }

fs::path appSet() {
    {
        std::lock_guard lock(appSetMutex);
        if (!appSetOverride.empty()) {
            return appSetOverride;
        }
    }
    return fs::path(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString()) / "stickers";
}

void setAppSet(const fs::path& folder) {
    std::lock_guard lock(appSetMutex);
    appSetOverride = folder;
}

bool isPicture(const fs::path& file) {
    const std::string ext = lower(file.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp";
}

bool isStickerFile(const fs::path& file) {
    if (hidden(file)) {
        return false;
    }
    const std::string name = file.filename().string();
    if (!name.empty() && name.back() == '~') {
        return false;
    }
    return lower(file.extension().string()) == ".xopp" || isPicture(file);
}

std::vector<Entry> list(const fs::path& set) {
    std::vector<Entry> entries;
    std::error_code ec;
    if (!fs::is_directory(set, ec)) {
        return entries;
    }
    fs::recursive_directory_iterator it(set, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path& p = it->path();
        if (it->is_directory(ec)) {
            if (hidden(p)) {
                it.disable_recursion_pending();  // (.xournal_library, .git, ...)
            }
            continue;
        }
        if (!isStickerFile(p) || !it->is_regular_file(ec)) {
            continue;
        }
        if (isPicture(p)) {
            // (the picture a .xopp of its name annotates is part of that sticker)
            fs::path xopp = p;
            xopp.replace_extension(".xopp");
            if (fs::exists(xopp, ec)) {
                continue;
            }
        }
        Entry e;
        e.path = p;
        e.name = p.stem().string();
        e.picture = isPicture(p);
        e.added = addedTime(p);
        const fs::path rel = p.parent_path().lexically_relative(set);
        e.folder = rel == "." ? std::string() : rel.generic_string();
        entries.push_back(std::move(e));
    }
    return entries;
}

QStringList folders(const fs::path& set) {
    QStringList names;
    std::error_code ec;
    if (!fs::is_directory(set, ec)) {
        return names;
    }
    fs::recursive_directory_iterator it(set, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }
        if (hidden(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        names << QString::fromStdString(it->path().lexically_relative(set).generic_string());
    }
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return DocumentFiles::compareNames(a, b) < 0;
    });
    return names;
}

fs::path uniqueTarget(const fs::path& folder, const std::string& name, const std::string& ext) {
    const auto taken = [&](const std::string& stem) {
        std::error_code ec;
        for (const char* e: {".xopp", ".png", ".jpg", ".jpeg", ".webp"}) {
            if (fs::exists(folder / (stem + e), ec)) {
                return true;
            }
        }
        return fs::exists(folder / (stem + ext), ec);
    };
    std::string stem = name.empty() ? std::string("Sticker") : name;
    for (int i = 2; taken(stem) && i < 10000; ++i) {
        stem = (name.empty() ? std::string("Sticker") : name) + " (" + std::to_string(i) + ")";
    }
    return folder / (stem + ext);
}

// --- the own order ------------------------------------------------------------------------------------------------

std::vector<std::string> readOrder(const fs::path& folder) {
    std::vector<std::string> names;
    for (const QJsonValue& v: readJson(folder / ORDER_FILE).value(QStringLiteral("order")).toArray()) {
        if (v.isString()) {
            names.push_back(v.toString().toStdString());
        }
    }
    return names;
}

bool writeOrder(const fs::path& folder, const std::vector<std::string>& names) {
    QJsonArray order;
    for (const std::string& n: names) {
        order.append(QString::fromStdString(n));
    }
    QJsonObject root;
    root.insert(QStringLiteral("order"), order);
    return writeJson(folder / ORDER_FILE, root);
}

std::vector<Entry> ordered(std::vector<Entry> entries) {
    // Per folder: the names of its order file, each with its place
    std::map<std::string, std::map<std::string, size_t>> places;
    for (const Entry& e: entries) {
        const std::string folder = e.path.parent_path().string();
        if (!places.count(folder)) {
            auto& p = places[folder];
            const auto names = readOrder(e.path.parent_path());
            for (size_t i = 0; i < names.size(); ++i) {
                p.emplace(names[i], i);
            }
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) {
            if (a.folder.empty() || b.folder.empty()) {
                return a.folder.empty();  // (the set's own stickers first)
            }
            return DocumentFiles::compareNames(QString::fromStdString(a.folder), QString::fromStdString(b.folder)) < 0;
        }
        const auto& p = places[a.path.parent_path().string()];
        const auto pa = p.find(a.path.filename().string());
        const auto pb = p.find(b.path.filename().string());
        if ((pa != p.end()) != (pb != p.end())) {
            return pa != p.end();
        }
        if (pa != p.end()) {
            return pa->second < pb->second;
        }
        return DocumentFiles::compareNames(QString::fromStdString(a.name), QString::fromStdString(b.name)) < 0;
    });
    return entries;
}

bool moveInOrder(const fs::path& sticker, int delta) {
    const fs::path folder = sticker.parent_path();
    // The folder's stickers (not its subfolders') in their order now
    std::vector<Entry> here;
    std::error_code ec;
    for (const auto& entry: fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && isStickerFile(entry.path())) {
            Entry e;
            e.path = entry.path();
            e.name = entry.path().stem().string();
            here.push_back(std::move(e));
        }
    }
    here = ordered(std::move(here));
    const auto at = std::find_if(here.begin(), here.end(), [&](const Entry& e) { return e.path == sticker; });
    if (at == here.end()) {
        return false;
    }
    const auto index = static_cast<long>(at - here.begin());
    const long to = index + delta;
    if (to < 0 || to >= static_cast<long>(here.size())) {
        return false;
    }
    std::swap(here[static_cast<size_t>(index)], here[static_cast<size_t>(to)]);
    std::vector<std::string> names;
    for (const Entry& e: here) {
        names.push_back(e.path.filename().string());
    }
    return writeOrder(folder, names);
}

// --- last used ------------------------------------------------------------------------------------------------

LastUsed::LastUsed(fs::path file): path(std::move(file)) { load(); }

void LastUsed::load() {
    used.clear();
    if (path.empty()) {
        return;
    }
    const QJsonObject times = readJson(path).value(QStringLiteral("lastUsed")).toObject();
    for (auto it = times.begin(); it != times.end(); ++it) {
        used[it.key().toStdString()] = static_cast<qint64>(it.value().toDouble());
    }
}

void LastUsed::save() const {
    if (path.empty()) {
        return;
    }
    QJsonObject times;
    for (const auto& [file, when]: used) {
        times.insert(QString::fromStdString(file), static_cast<double>(when));
    }
    QJsonObject root;
    root.insert(QStringLiteral("lastUsed"), times);
    writeJson(path, root);
}

qint64 LastUsed::when(const fs::path& sticker) const {
    const auto it = used.find(sticker.lexically_normal().string());
    return it == used.end() ? 0 : it->second;
}

void LastUsed::use(const fs::path& sticker, qint64 msecs) {
    used[sticker.lexically_normal().string()] = msecs;
    // (the stickers that are gone go: the file stays small)
    std::error_code ec;
    for (auto it = used.begin(); it != used.end();) {
        it = fs::exists(it->first, ec) ? std::next(it) : used.erase(it);
    }
    save();
}

void LastUsed::moved(const fs::path& from, const fs::path& to) {
    const auto it = used.find(from.lexically_normal().string());
    if (it == used.end()) {
        return;
    }
    const qint64 when = it->second;
    used.erase(it);
    if (!to.empty()) {
        used[to.lexically_normal().string()] = when;
    }
    save();
}

// --- changes ------------------------------------------------------------------------------------------------------

std::optional<fs::path> rename(const fs::path& sticker, const std::string& name) {
    if (DocumentFiles::nameProblem(name) != DocumentFiles::RenameProblem::None || name.front() == '.') {
        return std::nullopt;
    }
    const fs::path target = sticker.parent_path() / (name + sticker.extension().string());
    if (target == sticker) {
        return sticker;
    }
    std::error_code ec;
    // (taken by any sticker of that name; another case of its own name is a rename)
    if (lower(target.filename().string()) != lower(sticker.filename().string()) &&
        uniqueTarget(sticker.parent_path(), name, sticker.extension().string()) != target) {
        return std::nullopt;
    }
    fs::rename(sticker, target, ec);
    if (ec) {
        return std::nullopt;
    }
    renameInOrder(sticker.parent_path(), sticker.filename().string(), target.filename().string());
    PreviewCache::moved({{sticker, target}});
    return target;
}

std::optional<fs::path> moveTo(const fs::path& sticker, const fs::path& folder) {
    if (sticker.parent_path() == folder) {
        return sticker;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path target = uniqueTarget(folder, sticker.stem().string(), sticker.extension().string());
    fs::rename(sticker, target, ec);
    if (ec) {
        // (another disk: copied, then removed)
        ec.clear();
        if (!fs::copy_file(sticker, target, ec) || ec) {
            return std::nullopt;
        }
        fs::remove(sticker, ec);
    }
    renameInOrder(sticker.parent_path(), sticker.filename().string(), {});
    PreviewCache::moved({{sticker, target}});
    return target;
}

std::optional<fs::path> copyTo(const fs::path& sticker, const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path target = uniqueTarget(folder, sticker.stem().string(), sticker.extension().string());
    if (!fs::copy_file(sticker, target, ec) || ec) {
        return std::nullopt;
    }
    return target;
}

bool trash(const fs::path& sticker) {
    const DocumentItem item = DocumentFiles::itemOf(sticker);
    bool ok = item.valid() && DocumentFiles::trash(item).ok;
    std::error_code ec;
    if (!ok && fs::exists(sticker, ec)) {
        ok = fs::remove(sticker, ec);
    }
    if (ok) {
        renameInOrder(sticker.parent_path(), sticker.filename().string(), {});
    }
    return ok;
}

}  // namespace xqt::stickers

namespace xqt {

using namespace stickers;

StickersModel::StickersModel(QObject* parent): QAbstractListModel(parent) {}

void StickersModel::setLibrary(const fs::path& root, const fs::path& configDir) {
    libraryRoot = root;
    fs::path usedFile = configDir.empty() ? fs::path() : configDir / "stickers.json";
    if (usedFile.empty()) {
        usedFile = fs::path(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString()) /
                   "stickers.json";
    }
    lastUsed = LastUsed(usedFile);
    if (libraryRoot.empty() && scopeName == QLatin1String("library")) {
        scopeName = QStringLiteral("app");
    }
    Q_EMIT scopeChanged();
    refresh();
}

fs::path StickersModel::rootOf(const QString& scope) const {
    if (scope == QLatin1String("app")) {
        return appSet();
    }
    return libraryRoot.empty() ? fs::path() : librarySet(libraryRoot);
}

int StickersModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(shown.size());
}

QVariant StickersModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(shown.size())) {
        return {};
    }
    const Entry& e = shown[static_cast<size_t>(index.row())];
    switch (role) {
        case NameRole:
        case Qt::DisplayRole:
            return QString::fromStdString(e.name);
        case PathRole:
            return QString::fromStdString(e.path.string());
        case FolderRole:
            return QString::fromStdString(e.folder);
        case PreviewRole:
            return PreviewCache::url(DocumentFiles::itemOf(e.path));
        case PictureRole:
            return e.picture;
        default:
            return {};
    }
}

QHash<int, QByteArray> StickersModel::roleNames() const {
    return {{NameRole, "name"},
            {PathRole, "path"},
            {FolderRole, "folder"},
            {PreviewRole, "preview"},
            {PictureRole, "picture"}};
}

void StickersModel::setScope(const QString& scope) {
    const QString s = scope == QLatin1String("app") || libraryRoot.empty() ? QStringLiteral("app")
                                                                            : QStringLiteral("library");
    if (s == scopeName) {
        return;
    }
    scopeName = s;
    folderName.clear();
    Q_EMIT scopeChanged();
    Q_EMIT folderChanged();
    refresh();
}

void StickersModel::setFolder(const QString& folder) {
    if (folder == folderName) {
        return;
    }
    folderName = folder;
    Q_EMIT folderChanged();
    apply();
}

void StickersModel::setSearch(const QString& text) {
    if (text == searchText) {
        return;
    }
    searchText = text;
    Q_EMIT searchChanged();
    apply();
}

void StickersModel::setSort(const QString& sort) {
    if (sort == sortName || (sort != "used" && sort != "own" && sort != "name" && sort != "added")) {
        return;
    }
    sortName = sort;
    Q_EMIT sortChanged();
    apply();
}

void StickersModel::refresh() {
    const fs::path set = currentSet();
    all = set.empty() ? std::vector<Entry>() : list(set);
    allCount = all.size();
    folderNames = set.empty() ? QStringList() : folders(set);
    if (!folderName.isEmpty() && !folderNames.contains(folderName)) {
        folderName.clear();
        Q_EMIT folderChanged();
    }
    apply();
}

void StickersModel::apply() {
    beginResetModel();
    shown.clear();
    const std::string folder = folderName.toStdString();
    const QString needle = searchText.trimmed();
    for (const Entry& e: all) {
        if (!folder.empty() && e.folder != folder && e.folder.rfind(folder + "/", 0) != 0) {
            continue;
        }
        if (!needle.isEmpty() && !QString::fromStdString(e.name).contains(needle, Qt::CaseInsensitive) &&
            !QString::fromStdString(e.folder).contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        shown.push_back(e);
    }
    const auto byName = [](const Entry& a, const Entry& b) {
        return DocumentFiles::compareNames(QString::fromStdString(a.name), QString::fromStdString(b.name)) < 0;
    };
    if (sortName == QLatin1String("own")) {
        shown = ordered(std::move(shown));
    } else if (sortName == QLatin1String("name")) {
        std::stable_sort(shown.begin(), shown.end(), byName);
    } else if (sortName == QLatin1String("added")) {
        std::stable_sort(shown.begin(), shown.end(), [](const Entry& a, const Entry& b) { return a.added > b.added; });
    } else {
        // Last used first; those never used after them, the newest first
        std::stable_sort(shown.begin(), shown.end(), [&](const Entry& a, const Entry& b) {
            const qint64 ua = lastUsed.when(a.path);
            const qint64 ub = lastUsed.when(b.path);
            if (ua != ub) {
                return ua > ub;
            }
            return a.added > b.added;
        });
    }
    endResetModel();
    Q_EMIT listChanged();
}

QString StickersModel::pathAt(int row) const {
    return row >= 0 && row < static_cast<int>(shown.size()) ? QString::fromStdString(shown[row].path.string())
                                                            : QString();
}

void StickersModel::markUsed(const QString& path) {
    lastUsed.use(fs::path(path.toStdString()), QDateTime::currentMSecsSinceEpoch());
    if (sortName == QLatin1String("used")) {
        apply();
    }
}

bool StickersModel::moveBy(const QString& path, int delta) {
    if (!moveInOrder(fs::path(path.toStdString()), delta)) {
        return false;
    }
    if (sortName != QLatin1String("own")) {
        sortName = QStringLiteral("own");
        Q_EMIT sortChanged();
    }
    apply();
    return true;
}

bool StickersModel::rename(const QString& path, const QString& name) {
    const fs::path from(path.toStdString());
    // (a name a file cannot have is refused, not changed: the user sees what it is called)
    const std::string wanted = name.trimmed().toStdString();
    if (wanted.empty() || stickers::fileNameOf(wanted) != wanted) {
        return false;
    }
    const auto to = stickers::rename(from, wanted);
    if (!to) {
        return false;
    }
    lastUsed.moved(from, *to);
    refresh();
    return true;
}

bool StickersModel::moveToFolder(const QString& path, const QString& folder) {
    const fs::path from(path.toStdString());
    const QString scope = scopeOf(path);
    const fs::path set = rootOf(scope.isEmpty() ? scopeName : scope);
    if (set.empty() || folder.contains(QLatin1String(".."))) {
        return false;
    }
    fs::path into = set;
    for (const QString& part: folder.split('/', Qt::SkipEmptyParts)) {
        const std::string name = stickers::fileNameOf(part.toStdString());
        if (name.empty()) {
            return false;
        }
        into /= name;
    }
    const auto to = moveTo(from, into);
    if (!to) {
        return false;
    }
    lastUsed.moved(from, *to);
    refresh();
    return true;
}

bool StickersModel::copyToOtherSet(const QString& path) {
    const QString scope = scopeOf(path);
    if (scope.isEmpty() || libraryRoot.empty()) {
        return false;
    }
    const fs::path from(path.toStdString());
    const fs::path rel = from.parent_path().lexically_relative(rootOf(scope));
    const fs::path other = rootOf(scope == QLatin1String("app") ? QStringLiteral("library") : QStringLiteral("app"));
    const bool ok = copyTo(from, rel == "." ? other : other / rel).has_value();
    refresh();
    return ok;
}

bool StickersModel::copyToLibrary(const QString& path, const QString& library) {
    if (library.isEmpty()) {
        return false;
    }
    return copyTo(fs::path(path.toStdString()), librarySet(fs::path(library.toStdString()))).has_value();
}

bool StickersModel::remove(const QString& path) {
    const fs::path file(path.toStdString());
    if (!stickers::trash(file)) {
        return false;
    }
    lastUsed.moved(file, {});
    refresh();
    return true;
}

QString StickersModel::scopeOf(const QString& path) const {
    const fs::path file = fs::path(path.toStdString()).lexically_normal();
    for (const QString scope: {QStringLiteral("library"), QStringLiteral("app")}) {
        const fs::path set = rootOf(scope);
        if (set.empty()) {
            continue;
        }
        const fs::path rel = file.lexically_relative(set.lexically_normal());
        if (!rel.empty() && *rel.begin() != "..") {
            return scope;
        }
    }
    return {};
}

}  // namespace xqt
