#include "LibraryUnzip.h"

#include <algorithm>
#include <map>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QTimeZone>

#include "util/Util.h"

#include "DocumentFiles.h"
#include "LibraryShare.h"
#include "ZipFile.h"

namespace xqt {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("LibraryUnzip", text); }

std::string u8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}
fs::path fromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::vector<std::string> partsOf(const std::string& name) {
    std::vector<std::string> parts;
    std::string part;
    for (char c: name) {
        if (c == '/' || c == '\\') {
            if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.empty() && part != ".") {
        parts.push_back(part);
    }
    return parts;
}

/// The folder a zip unpacks into: its one top folder, else the zip's name. `strip`: its entries start with that
/// folder.
std::string topOf(const fs::path& zip, const std::vector<Zip::Entry>& entries, bool& strip) {
    std::string top;
    strip = true;
    for (const auto& e: entries) {
        const auto parts = partsOf(e.name);
        if (parts.empty()) {
            continue;
        }
        if ((parts.size() < 2 && !e.folder) || (!top.empty() && parts[0] != top)) {
            strip = false;
            break;
        }
        top = parts[0];
    }
    if (!strip || top.empty() || LibraryUnzip::safePath(top).empty() || top[0] == '.') {
        strip = false;
        return u8(zip.stem());
    }
    return top;
}

bool isManifest(const std::string& name) {
    const std::string tail = std::string(DocumentFiles::META_DIR) + "/" +
                             LibraryShare::MANIFEST_PACK.toStdString() + ".pack";
    return name.size() >= tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0;
}

/// Move a file (another disk: copied, then removed).
bool moveFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    fs::remove(from, ec);
    return true;
}

}  // namespace

fs::path LibraryUnzip::safePath(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos || name[0] == '/' || name[0] == '\\' ||
        (name.size() >= 2 && name[1] == ':' && std::isalpha(static_cast<unsigned char>(name[0])))) {
        return {};  // (absolute: "/x", "\\server\x", "C:x")
    }
    fs::path out;
    for (const std::string& part: partsOf(name)) {
        if (part == ".." || part.find(':') != std::string::npos) {
            return {};  // (out of the target; a stream or drive on Windows)
        }
        out /= fromU8(part);
    }
    return out;
}

LibraryUnzip::Inspection LibraryUnzip::inspect(const fs::path& zip) {
    Inspection in;
    Zip::Reader r(zip);
    if (!r.ok()) {
        in.error = tr("This zip cannot be read: %1").arg(QString::fromStdString(r.error())).toStdString();
        return in;
    }
    bool aes = false;
    for (const auto& e: r.entries()) {
        if (!e.folder) {
            ++in.files;
            in.bytes += e.size;
        }
        in.encrypted = in.encrypted || e.encrypted;
        aes = aes || e.aes;
        in.share = in.share || isManifest(e.name);
    }
    in.supported = !aes || Zip::aesAvailable();
    bool strip = false;
    in.name = topOf(zip, r.entries(), strip);
    in.ok = true;
    return in;
}

LibraryUnzip::Result LibraryUnzip::unpack(const fs::path& zip, const fs::path& into, const CacheLocation& location,
                                          const std::string& password, const std::atomic<bool>& cancel,
                                          const std::function<void(int, int, const std::string&)>& progress) {
    Result res;
    Zip::Reader r(zip);
    if (!r.ok()) {
        res.error = tr("This zip cannot be read: %1").arg(QString::fromStdString(r.error())).toStdString();
        return res;
    }
    const auto& entries = r.entries();
    if (entries.size() > static_cast<size_t>(MAX_ENTRIES)) {
        res.error = tr("This zip has too many files (more than %1).").arg(MAX_ENTRIES).toStdString();
        return res;
    }
    if (r.encrypted()) {
        bool aes = false;
        for (const auto& e: entries) {
            aes = aes || e.aes;
        }
        if (aes && !Zip::aesAvailable()) {
            res.error = tr("This zip is protected with AES encryption, which this version of the app cannot read. "
                           "Unpack it with another app (7-Zip, Keka, the Files app) and import the folder.")
                                .toStdString();
            return res;
        }
        r.setPassword(password);
        if (password.empty() || !r.passwordWorks()) {
            res.wrongPassword = true;
            res.error = password.empty() ? tr("This zip is protected with a password.").toStdString()
                                         : tr("The password is not right.").toStdString();
            return res;
        }
    }
    uint64_t bytes = 0;
    for (const auto& e: entries) {
        bytes += e.size;
    }
    std::error_code ec;
    fs::create_directories(into, ec);
    const auto space = fs::space(into, ec);
    if (bytes > MAX_BYTES || (!ec && bytes + 64ull * 1024 * 1024 > space.available)) {
        res.error = tr("There is not enough room for this zip: it unpacks to %1 MB.")
                            .arg(static_cast<double>(bytes) / 1e6, 0, 'f', 0)
                            .toStdString();
        return res;
    }
    bool strip = false;
    const std::string name = topOf(zip, entries, strip);
    fs::path target = into / fromU8(name);
    for (int n = 2; fs::exists(target, ec) && n < 10000; ++n) {
        target = into / fromU8(name + " (" + std::to_string(n) + ")");
    }
    const fs::path work = into / fromU8("." + name + ".unzip-" + std::to_string(Util::getPid()));
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    auto fail = [&](std::string why) {
        fs::remove_all(work, ec);
        res.error = std::move(why);
        return res;
    };
    const int total = static_cast<int>(entries.size());
    int done = 0;
    uint64_t written = 0;
    std::map<fs::path, std::time_t> times;
    std::set<fs::path> made;
    for (const auto& e: entries) {
        if (cancel) {
            res.cancelled = true;
            return fail(tr("Cancelled.").toStdString());
        }
        fs::path rel = safePath(e.name);
        if (!rel.empty() && strip) {
            fs::path rest;
            for (auto it = std::next(rel.begin()); it != rel.end(); ++it) {
                rest /= *it;
            }
            rel = rest;
        }
        if (progress) {
            progress(++done, total, e.name);
        }
        if (rel.empty()) {
            if (!(strip && e.folder && partsOf(e.name).size() == 1)) {
                res.skipped.push_back(e.name + ": " + tr("a path outside the folder").toStdString());
            }
            continue;
        }
        if (e.symlink) {
            res.skipped.push_back(e.name + ": " + tr("a link").toStdString());
            continue;
        }
        if (isManifest(u8(rel))) {
            continue;  // (what was shared: read here, not kept)
        }
        const fs::path out = work / rel;
        if (e.folder) {
            fs::create_directories(out, ec);
            continue;
        }
        if (made.count(out)) {
            res.skipped.push_back(e.name + ": " + tr("twice in the zip").toStdString());
            continue;
        }
        fs::create_directories(out.parent_path(), ec);
        std::string error;
        if (!r.extract(e, out, [&cancel] { return cancel.load(); }, written, error)) {
            if (cancel) {
                res.cancelled = true;
                return fail(tr("Cancelled.").toStdString());
            }
            if (e.encrypted && error.find("assword") != std::string::npos) {
                res.wrongPassword = true;
                return fail(tr("The password is not right.").toStdString());
            }
            res.skipped.push_back(e.name + ": " + error);
            continue;
        }
        made.insert(out);
        times[out] = e.mtime;
        ++res.files;
    }
    // The times the zip gives (UTC): the library's readings in it are stamped with them
    for (const auto& [file, t]: times) {
        QFile f(QString::fromStdU16String(file.u16string()));
        if (f.open(QIODevice::ReadWrite)) {
            f.setFileTime(QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC), QFileDevice::FileModificationTime);
        }
    }
    // The readings go where the library keeps its cache (in the app cache: under the folders' final paths, before
    // the folder is there, so the library finds them when it first looks at it)
    if (location.valid() && location.mode() == CacheLocation::Mode::AppCache && location.contains(into)) {
        std::vector<fs::path> caches;
        for (auto it = fs::recursive_directory_iterator(work, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_directory() && it->path().filename() == DocumentFiles::META_DIR) {
                caches.push_back(it->path());
                it.disable_recursion_pending();
            }
        }
        for (const fs::path& cache: caches) {
            const fs::path folder = target / cache.parent_path().lexically_relative(work);
            const fs::path dest = location.mirrorOf(folder.lexically_normal());
            for (auto it = fs::directory_iterator(cache, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file() && Packs::isOurs(QString::fromStdString(it->path().filename().string()))) {
                    moveFile(it->path(), dest / it->path().filename());
                }
            }
            Packs::removeIfOnlyOurs(cache);
            fs::remove(cache, ec);
        }
    }
    fs::rename(work, target, ec);
    if (ec) {
        return fail(tr("The folder cannot be made: %1").arg(QString::fromStdString(ec.message())).toStdString());
    }
    res.ok = true;
    res.folder = target;
    return res;
}

// --- in the background -----------------------------------------------------------------------------------------------

struct LibraryUnzip::State {
    std::atomic<bool> cancel{false};
};

LibraryUnzip::LibraryUnzip(QObject* parent): QObject(parent) {}

LibraryUnzip::~LibraryUnzip() { cancel(); }

void LibraryUnzip::cancel() {
    if (state) {
        state->cancel = true;
    }
}

bool LibraryUnzip::start(const fs::path& zip, const fs::path& into, const CacheLocation& location,
                         const std::string& password, std::string& error) {
    if (state) {
        error = tr("A zip is being unpacked already.").toStdString();
        return false;
    }
    state = std::make_shared<State>();
    doneCount = 0;
    totalCount = 0;
    currentName.clear();
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    QPointer<LibraryUnzip> self(this);
    std::shared_ptr<State> st = state;
    QThreadPool::globalInstance()->start([self, st, zip, into, location, password] {
        QThread::currentThread()->setPriority(QThread::LowPriority);
        Result r = unpack(zip, into, location, password, st->cancel, [self, st](int done, int total, const std::string& c) {
            const QString name = QString::fromStdString(c);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, done, total, name] {
                if (self && self->state == st) {
                    self->doneCount = done;
                    self->totalCount = total;
                    self->currentName = name;
                    Q_EMIT self->progressChanged();
                }
            });
        });
        QThread::currentThread()->setPriority(QThread::NormalPriority);
        QStringList skipped;
        for (const auto& s: r.skipped) {
            skipped << QString::fromStdString(s);
        }
        QVariantMap map{{"ok", r.ok},
                        {"error", QString::fromStdString(r.error)},
                        {"wrongPassword", r.wrongPassword},
                        {"cancelled", r.cancelled},
                        {"folder", QString::fromStdString(u8(r.folder))},
                        {"files", r.files},
                        {"skipped", skipped}};
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, map] {
            if (!self || self->state != st) {
                return;
            }
            self->state.reset();
            Q_EMIT self->runningChanged();
            Q_EMIT self->finished(map);
        });
    });
    return true;
}

}  // namespace xqt
