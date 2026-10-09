#include "Stickers.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>

#include "control/settings/Settings.h"

#include "DocumentFiles.h"
#include "session/AppContext.h"
#include "session/FileIo.h"

#include "JsonFile.h"
#include "session/StickerFile.h"
#include "DocumentCovers.h"

namespace xqt::stickers {

namespace {
std::mutex appSetMutex;
std::map<Kind, fs::path> appSetOverride;
fs::path builtinOverride;

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


bool writeJson(const fs::path& file, const QJsonObject& object) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return fileio::writeFileAtomically(QString::fromStdString(file.string()),
                                       QJsonDocument(object).toJson(QJsonDocument::Indented));
}

/// The order file names one sticker by another name now (or no more: `to` empty)
void renameInOrder(const fs::path& folder, const std::string& from, const std::string& to, Kind kind) {
    std::vector<std::string> names = readOrder(folder, kind);
    const auto at = std::find(names.begin(), names.end(), from);
    if (at == names.end()) {
        return;
    }
    if (to.empty()) {
        names.erase(at);
    } else {
        *at = to;
    }
    writeOrder(folder, names, kind);
}

/// The files of a .xopp follow it to `target` (moved, or copied)
void carryCompanions(const fs::path& from, const fs::path& target, bool copy) {
    for (const auto& [file, suffix]: companionsOf(from)) {
        fs::path to = target.parent_path() / (target.filename().string() + suffix);
        std::error_code ec;
        if (!copy) {
            fs::rename(file, to, ec);
            if (!ec) {
                continue;
            }
            ec.clear();
        }
        fs::copy_file(file, to, fs::copy_options::overwrite_existing, ec);
        if (!copy && !ec) {
            fs::remove(file, ec);
        }
    }
}
}  // namespace

const char* orderFile(Kind kind) { return kind == Kind::Templates ? TEMPLATE_ORDER_FILE : ORDER_FILE; }

fs::path librarySet(const fs::path& libraryRoot, Kind kind) {
    return libraryRoot / (kind == Kind::Templates ? TEMPLATES_FOLDER : FOLDER);
}

fs::path appSet(Kind kind) {
    {
        std::lock_guard lock(appSetMutex);
        if (const auto it = appSetOverride.find(kind); it != appSetOverride.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return fs::path(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString()) /
           (kind == Kind::Templates ? "templates" : "stickers");
}

void setAppSet(const fs::path& folder, Kind kind) {
    std::lock_guard lock(appSetMutex);
    appSetOverride[kind] = folder;
}

// --- the built-in set -----------------------------------------------------------------------------------------------

fs::path builtinSet() {
    {
        std::lock_guard lock(appSetMutex);
        if (!builtinOverride.empty()) {
            return builtinOverride;
        }
    }
    return AppContext::defaultResourceDir() / "stickers";
}

void setBuiltinSet(const fs::path& folder) {
    std::lock_guard lock(appSetMutex);
    builtinOverride = folder;
}

std::vector<Collection> collections(const fs::path& set) {
    std::vector<Collection> out;
    std::error_code ec;
    if (set.empty() || !fs::is_directory(set, ec)) {
        return out;
    }
    const auto strings = [](const QJsonValue& v) {
        std::vector<std::string> list;
        for (const QJsonValue& s: v.toArray()) {
            if (s.isString() && !s.toString().isEmpty()) {
                list.push_back(s.toString().toStdString());
            }
        }
        return list;
    };
    for (const auto& entry: fs::directory_iterator(set, ec)) {
        if (!entry.is_directory(ec) || hidden(entry.path())) {
            continue;
        }
        const QJsonObject names = readJsonObject(entry.path() / "names.json");
        if (names.isEmpty()) {
            continue;
        }
        Collection c;
        c.id = entry.path().filename().string();
        const QJsonObject title = names.value(QStringLiteral("title")).toObject();
        c.titleEn = title.value(QStringLiteral("en")).toString(QString::fromStdString(c.id)).toStdString();
        c.titleDe = title.value(QStringLiteral("de")).toString(QString::fromStdString(c.titleEn)).toStdString();
        for (const QJsonValue& v: names.value(QStringLiteral("stickers")).toArray()) {
            const QJsonObject o = v.toObject();
            BuiltinSticker b;
            b.file = o.value(QStringLiteral("file")).toString().toStdString();
            b.en = strings(o.value(QStringLiteral("en")));
            b.de = strings(o.value(QStringLiteral("de")));
            // (a file of the list that is not there, or not a sticker's, is left out)
            if (b.file.empty() || !isStickerFile(entry.path() / b.file) || !fs::is_regular_file(entry.path() / b.file, ec)) {
                continue;
            }
            if (b.en.empty()) {
                b.en.push_back(fs::path(b.file).stem().string());
            }
            if (b.de.empty()) {
                b.de.push_back(b.en.front());
            }
            c.stickers.push_back(std::move(b));
        }
        out.push_back(std::move(c));
    }
    // In the order of collections.json, the others after them by id
    std::vector<std::string> order;
    for (const QJsonValue& v: readJsonObject(set / "collections.json").value(QStringLiteral("order")).toArray()) {
        order.push_back(v.toString().toStdString());
    }
    const auto place = [&](const Collection& c) {
        const auto at = std::find(order.begin(), order.end(), c.id);
        return static_cast<size_t>(at - order.begin());
    };
    std::sort(out.begin(), out.end(), [&](const Collection& a, const Collection& b) {
        const size_t pa = place(a), pb = place(b);
        return pa != pb ? pa < pb : a.id < b.id;
    });
    return out;
}

std::string nameLanguage() { return QLocale().language() == QLocale::German ? "de" : "en"; }

QStringList hiddenCollections(Settings& settings) {
    std::string ids;
    settings.getCustomElement("xournalQt").getString("hiddenStickerCollections", ids);
    return QString::fromStdString(ids).split(u'+', Qt::SkipEmptyParts);
}

void setHiddenCollections(Settings& settings, const QStringList& ids) {
    settings.getCustomElement("xournalQt").setString("hiddenStickerCollections", ids.join(u'+').toStdString());
    settings.customSettingsChanged();
}

bool penColour(Settings& settings) {
    bool on = false;
    settings.getCustomElement("xournalQt").getBool("builtinStickersInPenColour", on);
    return on;
}

void setPenColour(Settings& settings, bool on) {
    settings.getCustomElement("xournalQt").setBool("builtinStickersInPenColour", on);
    settings.customSettingsChanged();
}

std::vector<std::pair<fs::path, std::string>> companionsOf(const fs::path& file) {
    std::vector<std::pair<fs::path, std::string>> out;
    if (lower(file.extension().string()) != ".xopp") {
        return out;
    }
    const std::string name = file.filename().string();
    std::error_code ec;
    if (fs::path pdf = file.parent_path() / (name + ".bg.pdf"); fs::exists(pdf, ec)) {
        out.emplace_back(pdf, ".bg.pdf");
    }
    for (const fs::path& img: DocumentFiles::imageAttachmentsOf(file)) {
        out.emplace_back(img, img.filename().string().substr(name.size()));
    }
    return out;
}

bool isPicture(const fs::path& file) {
    const std::string ext = lower(file.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp";
}

bool isStickerFile(const fs::path& file, Kind kind) {
    if (hidden(file)) {
        return false;
    }
    const std::string name = file.filename().string();
    if (!name.empty() && name.back() == '~') {
        return false;
    }
    return lower(file.extension().string()) == ".xopp" || (kind == Kind::Stickers && isPicture(file));
}

std::vector<Entry> list(const fs::path& set, Kind kind) {
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
        if (!isStickerFile(p, kind) || !it->is_regular_file(ec)) {
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
        return fs::exists(folder / (stem + ext), ec) || fs::exists(folder / (stem + ".xopp.bg.pdf"), ec);
    };
    std::string stem = name.empty() ? std::string("Sticker") : name;
    for (int i = 2; taken(stem) && i < 10000; ++i) {
        stem = (name.empty() ? std::string("Sticker") : name) + " (" + std::to_string(i) + ")";
    }
    return folder / (stem + ext);
}

// --- the own order ------------------------------------------------------------------------------------------------

std::vector<std::string> readOrder(const fs::path& folder, Kind kind) {
    std::vector<std::string> names;
    for (const QJsonValue& v: readJsonObject(folder / orderFile(kind)).value(QStringLiteral("order")).toArray()) {
        if (v.isString()) {
            names.push_back(v.toString().toStdString());
        }
    }
    return names;
}

bool writeOrder(const fs::path& folder, const std::vector<std::string>& names, Kind kind) {
    QJsonArray order;
    for (const std::string& n: names) {
        order.append(QString::fromStdString(n));
    }
    QJsonObject root;
    root.insert(QStringLiteral("order"), order);
    return writeJson(folder / orderFile(kind), root);
}

std::vector<Entry> ordered(std::vector<Entry> entries, Kind kind) {
    // Per folder: the names of its order file, each with its place
    std::map<std::string, std::map<std::string, size_t>> places;
    for (const Entry& e: entries) {
        const std::string folder = e.path.parent_path().string();
        if (!places.count(folder)) {
            auto& p = places[folder];
            const auto names = readOrder(e.path.parent_path(), kind);
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

bool moveInOrder(const fs::path& sticker, int delta, Kind kind) {
    const fs::path folder = sticker.parent_path();
    // The folder's stickers (not its subfolders') in their order now
    std::vector<Entry> here;
    std::error_code ec;
    for (const auto& entry: fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && isStickerFile(entry.path(), kind)) {
            Entry e;
            e.path = entry.path();
            e.name = entry.path().stem().string();
            here.push_back(std::move(e));
        }
    }
    here = ordered(std::move(here), kind);
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
    return writeOrder(folder, names, kind);
}

// --- last used ------------------------------------------------------------------------------------------------

LastUsed::LastUsed(fs::path file): path(std::move(file)) { load(); }

void LastUsed::load() {
    used.clear();
    if (path.empty()) {
        return;
    }
    const QJsonObject times = readJsonObject(path).value(QStringLiteral("lastUsed")).toObject();
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

std::optional<fs::path> rename(const fs::path& sticker, const std::string& name, Kind kind) {
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
    carryCompanions(sticker, target, false);
    renameInOrder(sticker.parent_path(), sticker.filename().string(), target.filename().string(), kind);
    DocumentCovers::moved({{sticker, target}});
    return target;
}

std::optional<fs::path> moveTo(const fs::path& sticker, const fs::path& folder, Kind kind) {
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
    carryCompanions(sticker, target, false);
    renameInOrder(sticker.parent_path(), sticker.filename().string(), {}, kind);
    DocumentCovers::moved({{sticker, target}});
    return target;
}

std::optional<fs::path> copyTo(const fs::path& sticker, const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path target = uniqueTarget(folder, sticker.stem().string(), sticker.extension().string());
    if (!fs::copy_file(sticker, target, ec) || ec) {
        return std::nullopt;
    }
    carryCompanions(sticker, target, true);
    return target;
}

bool trash(const fs::path& sticker, Kind kind) {
    const DocumentItem item = DocumentFiles::itemOf(sticker);
    bool ok = item.valid() && DocumentFiles::trash(item).ok;
    std::error_code ec;
    if (!ok && fs::exists(sticker, ec)) {
        for (const auto& c: companionsOf(sticker)) {
            fs::remove(c.first, ec);
        }
        ok = fs::remove(sticker, ec);
    }
    if (ok) {
        renameInOrder(sticker.parent_path(), sticker.filename().string(), {}, kind);
    }
    return ok;
}

}  // namespace xqt::stickers

namespace xqt {

using namespace stickers;

StickersModel::StickersModel(Kind kind, QObject* parent): QAbstractListModel(parent), setKind(kind) {}

void StickersModel::setLibrary(const fs::path& root, const fs::path& configDir) {
    libraryRoot = root;
    const char* usedName = setKind == Kind::Templates ? "templates.json" : "stickers.json";
    fs::path usedFile = configDir.empty() ? fs::path() : configDir / usedName;
    if (usedFile.empty()) {
        usedFile = fs::path(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString()) /
                   usedName;
    }
    lastUsed = LastUsed(usedFile);
    if (libraryRoot.empty() && scopeName == QLatin1String("library")) {
        scopeName = QStringLiteral("app");
    }
    Q_EMIT scopeChanged();
    refresh();
}

void StickersModel::setSettings(Settings* s) {
    settings = s;
    if (settings) {
        hidden = hiddenCollections(*settings);
        inPenColour = stickers::penColour(*settings);
    }
    Q_EMIT penColourChanged();
    refresh();
}

fs::path StickersModel::rootOf(const QString& scope) const {
    if (scope == QLatin1String("app")) {
        return appSet(setKind);
    }
    if (scope == QLatin1String("builtin")) {
        return setKind == Kind::Stickers ? builtinSet() : fs::path();
    }
    return libraryRoot.empty() ? fs::path() : librarySet(libraryRoot, setKind);
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
        case CoverRole:
            return DocumentCovers::url(DocumentFiles::itemOf(e.path));
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
            {CoverRole, "preview"},
            {PictureRole, "picture"}};
}

void StickersModel::setScope(const QString& scope) {
    const QString s = scope == QLatin1String("builtin") && setKind == Kind::Stickers ? QStringLiteral("builtin")
                      : scope == QLatin1String("app") || libraryRoot.empty()          ? QStringLiteral("app")
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

void StickersModel::listBuiltin() {
    builtins = setKind == Kind::Stickers ? collections(builtinSet()) : std::vector<Collection>();
    language = QString::fromStdString(nameLanguage());
    if (scopeName != QLatin1String("builtin")) {
        return;
    }
    all.clear();
    folderNames.clear();
    const fs::path set = builtinSet();
    int rank = 0;
    for (const Collection& c: builtins) {
        if (hidden.contains(QString::fromStdString(c.id))) {
            continue;
        }
        folderNames << QString::fromStdString(c.id);
        for (const BuiltinSticker& b: c.stickers) {
            Entry e;
            e.path = set / c.id / b.file;
            e.folder = c.id;
            e.name = language == QLatin1String("de") ? b.de.front() : b.en.front();
            e.builtin = true;
            e.rank = rank++;
            e.aliases = b.en;
            e.aliases.insert(e.aliases.end(), b.de.begin(), b.de.end());
            e.aliases.push_back(c.titleEn);
            e.aliases.push_back(c.titleDe);
            all.push_back(std::move(e));
        }
    }
}

void StickersModel::refresh() {
    const fs::path set = currentSet();
    listBuiltin();
    if (scopeName != QLatin1String("builtin")) {
        all = set.empty() ? std::vector<Entry>() : list(set, setKind);
        folderNames = set.empty() ? QStringList() : folders(set);
    }
    allCount = all.size();
    Q_EMIT collectionsChanged();
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
            !QString::fromStdString(e.folder).contains(needle, Qt::CaseInsensitive) &&
            std::none_of(e.aliases.begin(), e.aliases.end(), [&](const std::string& alias) {
                return QString::fromStdString(alias).contains(needle, Qt::CaseInsensitive);
            })) {
            continue;
        }
        shown.push_back(e);
    }
    const auto byName = [](const Entry& a, const Entry& b) {
        return DocumentFiles::compareNames(QString::fromStdString(a.name), QString::fromStdString(b.name)) < 0;
    };
    const bool builtin = scopeName == QLatin1String("builtin");
    if (sortName == QLatin1String("own")) {
        if (!builtin) {  // (the built-in set's own order: its lists, as read)
            shown = ordered(std::move(shown), setKind);
        }
    } else if (sortName == QLatin1String("name")) {
        std::stable_sort(shown.begin(), shown.end(), byName);
    } else if (sortName == QLatin1String("added") && builtin) {
        // (all came with the app: their own order)
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
            return builtin ? a.rank < b.rank : a.added > b.added;
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

QVariantList StickersModel::recent(int count) const {
    std::vector<std::pair<qint64, Entry>> used;
    for (const QString scope: {QStringLiteral("library"), QStringLiteral("app")}) {
        const fs::path set = rootOf(scope);
        if (set.empty()) {
            continue;
        }
        for (Entry& e: list(set, setKind)) {
            if (const qint64 when = lastUsed.when(e.path); when > 0) {
                used.emplace_back(when, std::move(e));
            }
        }
    }
    std::stable_sort(used.begin(), used.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QVariantList out;
    for (const auto& [when, e]: used) {
        if (out.size() >= count) {
            break;
        }
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(e.name)},
                               {QStringLiteral("path"), QString::fromStdString(e.path.string())}});
    }
    return out;
}

bool StickersModel::moveBy(const QString& path, int delta) {
    if (isBuiltin(path)) {
        return false;
    }
    if (!moveInOrder(fs::path(path.toStdString()), delta, setKind)) {
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
    if (isBuiltin(path)) {
        return false;
    }
    const fs::path from(path.toStdString());
    // (a name a file cannot have is refused, not changed: the user sees what it is called)
    const std::string wanted = name.trimmed().toStdString();
    if (wanted.empty() || stickers::fileNameOf(wanted) != wanted) {
        return false;
    }
    const auto to = stickers::rename(from, wanted, setKind);
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
    if (scope == QLatin1String("builtin")) {
        return false;
    }
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
    const auto to = moveTo(from, into, setKind);
    if (!to) {
        return false;
    }
    lastUsed.moved(from, *to);
    refresh();
    return true;
}

bool StickersModel::copyToOtherSet(const QString& path) {
    const QString scope = scopeOf(path);
    if (scope.isEmpty() || scope == QLatin1String("builtin") || libraryRoot.empty()) {
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
    return copyTo(fs::path(path.toStdString()), librarySet(fs::path(library.toStdString()), setKind)).has_value();
}

bool StickersModel::remove(const QString& path) {
    if (isBuiltin(path)) {
        return false;
    }
    const fs::path file(path.toStdString());
    if (!stickers::trash(file, setKind)) {
        return false;
    }
    lastUsed.moved(file, {});
    refresh();
    return true;
}

QString StickersModel::scopeOf(const QString& path) const {
    const fs::path file = fs::path(path.toStdString()).lexically_normal();
    for (const QString scope: {QStringLiteral("library"), QStringLiteral("app"), QStringLiteral("builtin")}) {
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

QString StickersModel::folderTitle(const QString& folder) const {
    if (scopeName == QLatin1String("builtin")) {
        for (const Collection& c: builtins) {
            if (QString::fromStdString(c.id) == folder) {
                return QString::fromStdString(language == QLatin1String("de") ? c.titleDe : c.titleEn);
            }
        }
    }
    return folder;
}

QString StickersModel::copyToMine(const QString& path) {
    const fs::path from(path.toStdString());
    const fs::path into = rootOf(hasLibrary() ? QStringLiteral("library") : QStringLiteral("app"));
    std::error_code ec;
    if (into.empty() || !fs::is_regular_file(from, ec)) {
        return {};
    }
    // Named as it is shown (a built-in one in the app's language)
    std::string name = from.stem().string();
    for (const Entry& e: all) {
        if (e.path == from) {
            name = e.name;
        }
    }
    name = stickers::fileNameOf(name);
    fs::create_directories(into, ec);
    const fs::path target = uniqueTarget(into, name, from.extension().string());
    if (!fs::copy_file(from, target, ec) || ec) {
        return {};
    }
    // (a copy of a read-only file is the user's: writable)
    fs::permissions(target, fs::perms::owner_write, fs::perm_options::add, ec);
    carryCompanions(from, target, true);
    refresh();
    return QString::fromStdString(target.string());
}

QVariantList StickersModel::collectionList() const {
    QVariantList out;
    for (const Collection& c: builtins) {
        const QString id = QString::fromStdString(c.id);
        out.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("title"),
                 QString::fromStdString(language == QLatin1String("de") ? c.titleDe : c.titleEn)},
                {QStringLiteral("hidden"), hidden.contains(id)},
                {QStringLiteral("count"), static_cast<int>(c.stickers.size())}});
    }
    return out;
}

int StickersModel::hiddenCount() const {
    int n = 0;
    for (const Collection& c: builtins) {
        n += hidden.contains(QString::fromStdString(c.id)) ? 1 : 0;
    }
    return n;
}

void StickersModel::setCollectionHidden(const QString& id, bool hide) {
    if (id.isEmpty() || hide == hidden.contains(id)) {
        return;
    }
    if (hide) {
        hidden << id;
    } else {
        hidden.removeAll(id);
    }
    storeHidden();
}

void StickersModel::restoreCollections() {
    if (hidden.isEmpty()) {
        return;
    }
    hidden.clear();
    storeHidden();
}

void StickersModel::storeHidden() {
    if (settings) {
        setHiddenCollections(*settings, hidden);
    }
    if (folderName == QLatin1String("") || !hidden.contains(folderName)) {
        refresh();
        return;
    }
    folderName.clear();  // (its collection is hidden: all of the set)
    Q_EMIT folderChanged();
    refresh();
}

void StickersModel::setPenColour(bool on) {
    if (on == inPenColour) {
        return;
    }
    inPenColour = on;
    if (settings) {
        stickers::setPenColour(*settings, on);
    }
    Q_EMIT penColourChanged();
}

}  // namespace xqt
