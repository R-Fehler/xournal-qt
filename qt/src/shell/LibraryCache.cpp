#include "LibraryCache.h"

#include <QCborValue>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>

#include "util/PathUtil.h"

#include "DocumentFiles.h"

namespace xqt {

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

const QByteArray MAGIC("XQPK");
constexpr char COMPRESSED = 1;
constexpr int HEADER = 6;  // magic, format, flags

QByteArray encode(const QString& pack, int contentFormat, const QCborMap& entries, bool compress) {
    const QCborMap body{{QStringLiteral("pack"), pack},
                        {QStringLiteral("format"), contentFormat},
                        {QStringLiteral("entries"), entries}};
    const QByteArray cbor = QCborValue(body).toCbor();
    QByteArray data = MAGIC;
    data.append(static_cast<char>(Packs::FORMAT));
    data.append(compress ? COMPRESSED : char(0));
    data.append(compress ? qCompress(cbor, 6) : cbor);
    return data;
}

std::optional<QCborMap> decode(const QByteArray& data, const QString& pack, int contentFormat) {
    if (data.size() < HEADER || !data.startsWith(MAGIC) || data[4] != static_cast<char>(Packs::FORMAT)) {
        return std::nullopt;
    }
    QByteArray cbor = data.mid(HEADER);
    if (data[5] & COMPRESSED) {
        cbor = qUncompress(cbor);
        if (cbor.isEmpty()) {
            return std::nullopt;
        }
    }
    QCborParserError error;
    const QCborValue value = QCborValue::fromCbor(cbor, &error);
    if (error.error != QCborError::NoError || !value.isMap()) {
        return std::nullopt;
    }
    const QCborMap body = value.toMap();
    if (body.value(QStringLiteral("pack")).toString() != pack ||
        body.value(QStringLiteral("format")).toInteger(-1) != contentFormat) {
        return std::nullopt;
    }
    return body.value(QStringLiteral("entries")).toMap();
}

std::optional<QCborMap> readFile(const fs::path& file, const QString& pack, int contentFormat) {
    QFile f(qstr(file));
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return decode(f.readAll(), pack, contentFormat);
}

bool writeFile(const fs::path& file, const QByteArray& data) {
    QSaveFile f(qstr(file));  // written under another name, then renamed: whole or not at all
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

/// Files of the entries that have one: "<pack>-<16 hex digits>.pack"
bool isOwnFileOf(const QString& name, const QString& pack) {
    static const QRegularExpression own(QStringLiteral("^(.+)-[0-9a-f]{16}\\.pack$"));
    const auto m = own.match(name);
    return m.hasMatch() && m.captured(1) == pack;
}

const QString OWN_FILE_KEY = QStringLiteral("$file");
}  // namespace

// --- CacheLocation --------------------------------------------------------------------------------------------

CacheLocation::CacheLocation(fs::path root, Mode mode, fs::path appCacheDir):
        rootDir(std::move(root)), appDir(std::move(appCacheDir)), cacheMode(mode) {
    if (appDir.empty() && !rootDir.empty()) {
        const QString key = QString::fromLatin1(
                QCryptographicHash::hash(QByteArray::fromStdString(rootDir.string()), QCryptographicHash::Sha1)
                        .toHex()
                        .left(12));
        appDir = Util::getCacheSubfolder("libraries") / key.toStdString();
    }
}

bool CacheLocation::contains(const fs::path& path) const {
    return valid() && (path == rootDir || DocumentFiles::remap(path, rootDir, "/") != path);
}

fs::path CacheLocation::inFolder(const fs::path& folder) const { return folder / DocumentFiles::META_DIR; }

fs::path CacheLocation::mirrorOf(const fs::path& folder) const {
    if (!contains(folder)) {
        return {};
    }
    const fs::path rel = folder.lexically_normal().lexically_relative(rootDir);
    return (rel.empty() || rel == "." ? appDir : appDir / rel) / DocumentFiles::META_DIR;
}

fs::path CacheLocation::dirOf(const fs::path& folder) const {
    if (cacheMode == Mode::AppCache) {
        return mirrorOf(folder);
    }
    const fs::path dot = inFolder(folder);
    const QFileInfo info(qstr(dot));
    if (info.exists()) {
        return info.isDir() && info.isWritable() ? dot : mirrorOf(folder);
    }
    return QFileInfo(qstr(folder)).isWritable() ? dot : mirrorOf(folder);
}

// --- Packs ----------------------------------------------------------------------------------------------------

namespace Packs {

fs::path fileOf(const fs::path& dir, const QString& pack) { return dir / (pack.toStdString() + ".pack"); }

fs::path ownFileOf(const fs::path& dir, const QString& pack, const QString& key) {
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return dir / (pack.toStdString() + '-' + hash.toStdString() + ".pack");
}

std::optional<QCborMap> read(const fs::path& dir, const QString& pack, int contentFormat) {
    auto entries = readFile(fileOf(dir, pack), pack, contentFormat);
    if (!entries) {
        return entries;
    }
    // Entries with a file of their own
    QList<QCborValue> keys;
    for (auto it = entries->cbegin(); it != entries->cend(); ++it) {
        if (it.value().isMap() && it.value().toMap().contains(OWN_FILE_KEY)) {
            keys.append(it.key());
        }
    }
    for (const QCborValue& key: keys) {
        const QString name = entries->value(key).toMap().value(OWN_FILE_KEY).toString();
        const auto own = isOwnFileOf(name, pack) ? readFile(dir / name.toStdString(), pack, contentFormat)
                                                 : std::nullopt;
        if (own && own->contains(key)) {
            entries->insert(key, own->value(key));
        } else {
            entries->remove(key);  // (gone: read again)
        }
    }
    return entries;
}

bool write(const fs::path& dir, const QString& pack, int contentFormat, const QCborMap& entries, bool compress,
           const std::set<QString>* changed) {
    if (entries.isEmpty()) {
        remove(dir, pack);
        return true;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    bool ok = true;
    QCborMap main;
    std::set<std::string> ownFiles;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const QCborValue value = it.value();
        // (only big values are measured exactly; strings are counted as they are, which is close enough)
        if (value.isMap() || value.isArray() || value.isByteArray() || value.isString()) {
            const QByteArray encoded = value.toCbor();
            if (encoded.size() >= OWN_FILE_SIZE) {
                const QString key = it.key().toString();
                const fs::path own = ownFileOf(dir, pack, key);
                if (!changed || changed->count(key) || !fs::exists(own, ec)) {
                    ok = writeFile(own, encode(pack, contentFormat, QCborMap{{it.key(), value}}, compress)) && ok;
                }
                ownFiles.insert(own.filename().string());
                main.insert(it.key(), QCborMap{{OWN_FILE_KEY, QString::fromStdString(own.filename().string())}});
                continue;
            }
        }
        main.insert(it.key(), value);
    }
    ok = writeFile(fileOf(dir, pack), encode(pack, contentFormat, main, compress)) && ok;
    // Files of entries that are gone or small now
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (isOwnFileOf(QString::fromStdString(name), pack) && !ownFiles.count(name)) {
            std::error_code rec;
            fs::remove(it->path(), rec);
        }
    }
    return ok;
}

void remove(const fs::path& dir, const QString& pack) {
    std::error_code ec;
    fs::remove(fileOf(dir, pack), ec);
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (isOwnFileOf(QString::fromStdString(it->path().filename().string()), pack)) {
            std::error_code rec;
            fs::remove(it->path(), rec);
        }
    }
}

bool isOurs(const QString& name) {
    // Packs, the files of big entries, and what QSaveFile writes before it renames ("notes.pack.AbC123")
    static const QRegularExpression pack(QStringLiteral("^[a-z][a-z-]*(-[0-9a-f]{16})?\\.pack(\\..+)?$"));
    return pack.match(name).hasMatch();
}

bool isOldLayout(const QString& name) {
    return name == QLatin1String("index") || name == QLatin1String("previews") || name == QLatin1String("pages.json") ||
           name == QLatin1String("pages.json.part");
}

namespace {
/// The folders of the layout before the packs ("index/", "previews/") hold only .json and .png files.
bool isOldFile(const fs::path& f) {
    const std::string n = f.filename().string();
    for (const char* ext: {".json", ".png", ".json.part", ".png.part"}) {
        const std::string e(ext);
        if (n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

/// Walks our files (with those of the old layout: `old`); returns false if something else is in the folder.
template <typename Fn>
bool forOurs(const fs::path& dir, bool old, Fn fn) {
    bool onlyOurs = true;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        const QString name = QString::fromStdString(p.filename().string());
        if (!isOurs(name) && !(old && isOldLayout(name))) {
            onlyOurs = false;
            continue;
        }
        std::error_code dec;
        if (fs::is_directory(p, dec)) {
            for (auto sub = fs::directory_iterator(p, dec); !dec && sub != fs::directory_iterator(); sub.increment(dec)) {
                if (isOldFile(sub->path()) && !sub->is_directory()) {
                    fn(sub->path());
                } else {
                    onlyOurs = false;
                }
            }
        } else {
            fn(p);
        }
    }
    return onlyOurs;
}
}  // namespace

bool removeIfOnlyOurs(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return true;
    }
    if (!forOurs(dir, false, [](const fs::path&) {})) {
        return false;
    }
    removeOurs(dir);
    return !fs::exists(dir, ec);
}

qint64 removeOurs(const fs::path& dir) {
    qint64 bytes = 0;
    forOurs(dir, true, [&](const fs::path& f) {
        std::error_code ec;
        const auto size = fs::file_size(f, ec);
        if (fs::remove(f, ec)) {
            bytes += ec ? 0 : static_cast<qint64>(size);
        }
    });
    std::error_code ec;
    for (const char* old: {"index", "previews"}) {
        fs::remove(dir / old, ec);  // (only when empty)
    }
    fs::remove(dir, ec);
    return bytes;
}

void removeOldLayout(const fs::path& dir) {
    std::error_code ec;
    for (const char* sub: {"index", "previews"}) {
        for (auto it = fs::directory_iterator(dir / sub, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (isOldFile(it->path()) && !it->is_directory()) {
                std::error_code rec;
                fs::remove(it->path(), rec);
            }
        }
        ec.clear();
        fs::remove(dir / sub, ec);  // (only when empty)
    }
    for (const char* file: {"pages.json", "pages.json.part"}) {
        fs::remove(dir / file, ec);
    }
}

qint64 sizeOf(const fs::path& dir, int* files) {
    qint64 bytes = 0;
    forOurs(dir, true, [&](const fs::path& f) {
        std::error_code ec;
        const auto size = fs::file_size(f, ec);
        bytes += ec ? 0 : static_cast<qint64>(size);
        if (files) {
            ++*files;
        }
    });
    return bytes;
}

}  // namespace Packs

// --- CacheFolders ---------------------------------------------------------------------------------------------

namespace CacheFolders {

namespace {
/// Remove the empty folders in `dir` (and `dir` itself if it ends up empty).
void removeEmptyFolders(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || fs::is_symlink(dir, ec)) {
        return;
    }
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->is_directory() && !it->is_symlink()) {
            removeEmptyFolders(it->path());
        }
    }
    fs::remove(dir, ec);  // (only when empty)
}
}  // namespace

Usage usage(const CacheLocation& location, const std::vector<fs::path>& folders) {
    Usage u;
    for (const fs::path& folder: folders) {
        u.bytes += Packs::sizeOf(location.inFolder(folder), &u.files);
    }
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(location.appCacheDir(), ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) {
            std::error_code sec;
            u.bytes += static_cast<qint64>(it->file_size(sec));
            ++u.files;
        }
    }
    return u;
}

void move(const CacheLocation& from, const CacheLocation& to, const std::vector<fs::path>& folders) {
    for (const fs::path& folder: folders) {
        const fs::path target = to.dirOf(folder);
        for (const fs::path& source: {from.inFolder(folder), from.mirrorOf(folder)}) {
            std::error_code ec;
            if (source.empty() || source == target || !fs::is_directory(source, ec)) {
                continue;
            }
            for (auto it = fs::directory_iterator(source, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file() || !Packs::isOurs(QString::fromStdString(it->path().filename().string()))) {
                    continue;
                }
                std::error_code mec;
                fs::create_directories(target, mec);
                const fs::path dest = target / it->path().filename();
                fs::rename(it->path(), dest, mec);
                if (mec) {  // (another disk)
                    mec.clear();
                    if (fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing, mec)) {
                        fs::remove(it->path(), mec);
                    }
                }
            }
            Packs::removeIfOnlyOurs(source);
        }
    }
    removeEmptyFolders(from.appCacheDir());
}

qint64 removeAll(const CacheLocation& location, const std::vector<fs::path>& folders) {
    qint64 bytes = 0;
    for (const fs::path& folder: folders) {
        bytes += Packs::removeOurs(location.inFolder(folder));
    }
    // The library's folder in the app cache: its mirrors, and the old layout at its top
    std::vector<fs::path> dirs{location.appCacheDir()};
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(location.appCacheDir(), ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory() && it->path().filename() == DocumentFiles::META_DIR) {
            dirs.push_back(it->path());
            it.disable_recursion_pending();
        }
    }
    for (const fs::path& dir: dirs) {
        bytes += Packs::removeOurs(dir);
    }
    removeEmptyFolders(location.appCacheDir());
    return bytes;
}

}  // namespace CacheFolders

// --- WriteScheduler -------------------------------------------------------------------------------------------

WriteScheduler::WriteScheduler(std::function<void()> write, QObject* parent): QObject(parent), writeFn(std::move(write)) {
    quiet.setSingleShot(true);
    latest.setSingleShot(true);
    setDelays(QUIET_MS, MAX_DELAY_MS);
    connect(&quiet, &QTimer::timeout, this, &WriteScheduler::fire);
    connect(&latest, &QTimer::timeout, this, &WriteScheduler::fire);
}

void WriteScheduler::setDelays(int quietMs, int maxDelayMs) {
    quiet.setInterval(quietMs);
    latest.setInterval(std::max(quietMs, maxDelayMs));
}

void WriteScheduler::changed() {
    if (QThread::currentThread() == thread()) {
        arm();
    } else {
        QMetaObject::invokeMethod(this, [this] { arm(); }, Qt::QueuedConnection);
    }
}

void WriteScheduler::arm() {
    quiet.start();
    if (!latest.isActive()) {
        latest.start();
    }
}

void WriteScheduler::fire() {
    cancel();
    writeFn();
}

void WriteScheduler::cancel() {
    quiet.stop();
    latest.stop();
}

}  // namespace xqt
