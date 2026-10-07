#include "Library.h"

#include <array>
#include <functional>
#include <mutex>
#include <optional>

#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QFile>
#include <QStandardPaths>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "session/FileIo.h"
#include "util/PathUtil.h"

namespace xqt {

namespace {
QString hashOf(const std::string& s, int length) {
    return QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::fromStdString(s), QCryptographicHash::Sha1).toHex().left(length));
}
fs::path normalized(const fs::path& p) {
    std::error_code ec;
    fs::path n = fs::weakly_canonical(fs::absolute(p, ec), ec);
    if (ec) {
        std::error_code aec;  // (never throws, also not for an empty path)
        n = fs::absolute(p, aec).lexically_normal();
    }
    if (!n.has_filename() && n.has_parent_path() && n != n.root_path()) {
        n = n.parent_path();
    }
    return n;
}
}  // namespace

Library::Library(const fs::path& root): rootDir(normalized(root)) {}

namespace {
#ifdef Q_OS_ANDROID
/// A folder of android.os.Environment: getExternalStoragePublicDirectory(<type>), or ("") the storage itself.
fs::path environmentFolder(const char* type) {
    QJniObject dir;
    if (*type) {
        dir = QJniObject::callStaticObjectMethod("android/os/Environment", "getExternalStoragePublicDirectory",
                                                 "(Ljava/lang/String;)Ljava/io/File;",
                                                 QJniObject::fromString(QString::fromLatin1(type)).object<jstring>());
    } else {
        dir = QJniObject::callStaticObjectMethod("android/os/Environment", "getExternalStorageDirectory",
                                                 "()Ljava/io/File;");
    }
    if (!dir.isValid()) {
        return {};
    }
    return fs::path(dir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString().toStdString());
}
#endif

struct Platform {
    std::mutex mtx;
    std::optional<PlatformFolders> folders;
    Library::Home home = Library::Home::App;
};
Platform& platform() {
    static Platform p;
    return p;
}
}  // namespace

PlatformFolders PlatformFolders::detect() {
    PlatformFolders f;
    f.appDocuments = fs::path(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString());
    f.appDownloads = fs::path(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString());
#ifdef Q_OS_ANDROID
    // (the names of Environment.DIRECTORY_DOCUMENTS and DIRECTORY_DOWNLOADS; asked once)
    static const std::array<fs::path, 3> shared{environmentFolder("Documents"), environmentFolder("Download"),
                                                environmentFolder("")};
    f.sharedDocuments = shared[0];
    f.sharedDownloads = shared[1];
    f.sharedStorage = shared[2];
#endif
    return f;
}

PlatformFolders Library::platformFolders() {
    // (detected each time: the desktop's folders can change while the app runs, e.g. user-dirs.dirs)
    {
        auto& p = platform();
        std::lock_guard lock(p.mtx);
        if (p.folders) {
            return *p.folders;
        }
    }
    return PlatformFolders::detect();
}

void Library::setPlatformFolders(const PlatformFolders* folders) {
    auto& p = platform();
    std::lock_guard lock(p.mtx);
    if (folders) {
        p.folders = *folders;
    } else {
        p.folders.reset();
    }
    p.home = Home::App;
}

Library::Home Library::home() {
    const bool shared = !platformFolders().sharedDocuments.empty();
    auto& p = platform();
    std::lock_guard lock(p.mtx);
    return shared ? p.home : Home::App;
}

void Library::setHome(Home home) {
    auto& p = platform();
    std::lock_guard lock(p.mtx);
    p.home = home;
}

Library::Home Library::chooseHome(const PlatformFolders& folders, bool access, bool moved) {
    return !folders.sharedDocuments.empty() && access && moved ? Home::Shared : Home::App;
}

fs::path Library::librariesFolder() { return librariesFolder(home()); }

fs::path Library::librariesFolder(Home home) {
    const PlatformFolders f = platformFolders();
    const bool shared = home == Home::Shared && !f.sharedDocuments.empty();
    return (shared ? f.sharedDocuments : f.appDocuments) / "Xournal_Libraries";
}

fs::path Library::defaultRoot() { return librariesFolder() / "Default"; }

fs::path Library::downloadsFolder() {
    // Android: the phone's Download folder, where the browser and the other apps put downloads (the app's own
    // "Download" folder stays empty)
    const PlatformFolders f = platformFolders();
    return f.sharedDownloads.empty() ? f.appDownloads : f.sharedDownloads;
}

bool Library::isTemporary() const {
    const fs::path downloads = normalized(downloadsFolder());
    return !downloads.empty() && DocumentFiles::remap(rootDir, downloads, "/") != rootDir;
}

QString Library::name() const { return QString::fromStdString(rootDir.filename().string()); }

bool Library::isInLibrariesFolder() const {
    const fs::path folder = normalized(librariesFolder());
    return rootDir != folder && DocumentFiles::remap(rootDir, folder, "/") != rootDir;
}

bool Library::isDefault() const { return rootDir == normalized(defaultRoot()); }

std::string Library::key() const { return hashOf(rootDir.string(), 12).toStdString(); }

fs::path Library::configDir() const { return Util::getConfigSubfolder(fs::path("libraries") / key()); }

namespace {
QJsonObject settingsOf(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
}  // namespace

namespace {
#ifdef Q_OS_ANDROID
CacheLocation::Mode platformCacheMode = CacheLocation::Mode::AppCache;
#else
CacheLocation::Mode platformCacheMode = CacheLocation::Mode::Folders;
#endif
}  // namespace

CacheLocation::Mode Library::defaultCacheMode() { return platformCacheMode; }
void Library::setDefaultCacheMode(CacheLocation::Mode mode) { platformCacheMode = mode; }

CacheLocation::Mode Library::cacheMode() const {
    const QString mode = settingsOf(configDir() / "library.json").value("cache").toString();
    return mode == QLatin1String("app")       ? CacheLocation::Mode::AppCache
           : mode == QLatin1String("folders") ? CacheLocation::Mode::Folders
                                              : defaultCacheMode();
}

bool Library::hasCacheSetting() const {
    return settingsOf(configDir() / "library.json").value("cache").isString();
}

namespace {
/// Change the settings of a library in its "library.json" (the others stay as they are).
void changeSettings(const fs::path& file, const fs::path& root, const std::function<void(QJsonObject&)>& change) {
    QJsonObject settings = settingsOf(file);
    settings["root"] = QString::fromStdString(root.string());  // (for people looking at the folder)
    change(settings);
    fileio::writeFileAtomically(QString::fromStdString(file.string()), QJsonDocument(settings).toJson());
}
}  // namespace

void Library::setCacheMode(CacheLocation::Mode mode) const {
    changeSettings(configDir() / "library.json", rootDir, [mode](QJsonObject& settings) {
        settings["cache"] = mode == CacheLocation::Mode::AppCache ? "app" : "folders";
    });
}

ShowFilter Library::showFilter() const {
    const QJsonObject show = settingsOf(configDir() / "library.json").value("show").toObject();
    ShowFilter f;
    auto read = [&](const char* key, bool& value) { value = show.value(QLatin1String(key)).toBool(value); };
    read("notes", f.notes);
    read("pdfs", f.pdfs);
    read("onlyPdfsWithNotes", f.onlyPdfsWithNotes);
    read("onlyTextDocuments", f.onlyTextDocuments);
    read("markdown", f.markdown);
    read("images", f.images);
    read("text", f.text);
    read("other", f.other);
    return f;
}

void Library::setShowFilter(const ShowFilter& f) const {
    changeSettings(configDir() / "library.json", rootDir, [&f](QJsonObject& settings) {
        settings["show"] = QJsonObject{{"notes", f.notes},   {"pdfs", f.pdfs}, {"onlyPdfsWithNotes", f.onlyPdfsWithNotes},
                                       {"onlyTextDocuments", f.onlyTextDocuments},
                                       {"markdown", f.markdown}, {"images", f.images}, {"text", f.text},
                                       {"other", f.other}};
    });
}

fs::path Library::placesFile() const { return configDir() / "pages.json"; }

bool Library::contains(const fs::path& p) const { return DocumentFiles::remap(p, rootDir, "/") != p; }

std::string Library::relative(const fs::path& p) const {
    const std::string rel = p.lexically_normal().lexically_relative(rootDir).string();
    return rel == "." ? std::string() : rel;
}

}  // namespace xqt
