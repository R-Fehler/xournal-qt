/*
 * xournal-qt: see LibraryMigration.h.
 *
 * @license GNU GPLv2 or later
 */
#include "LibraryMigration.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <set>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "util/PathUtil.h"

#include "DocumentFiles.h"
#include "DocumentPlaces.h"
#include "Library.h"
#include "LibraryCache.h"
#include "SessionRecovery.h"

namespace xqt::LibraryMigration {

namespace {

const std::string STAGING = ".xqt-moving-";
constexpr qint64 CHUNK = 1024 * 1024;
/// Room left free on the new home's storage besides the copies
constexpr qint64 SPARE_BYTES = 64 * 1024 * 1024;

std::mutex hookMutex;
std::function<bool(const fs::path&, const fs::path&)> copyHook;

QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
std::string str(const QString& s) { return s.toStdString(); }

bool isStaging(const fs::path& p) { return p.filename().string().rfind(STAGING, 0) == 0; }

/// A file of a library's cache (in a ".xournal_library" folder): the library may write it until it is let go (the
/// writes finish in the background), and the new place has its own copy; neither stops the move.
bool isCache(const fs::path& p) {
    return std::any_of(p.begin(), p.end(), [](const fs::path& part) { return part == DocumentFiles::META_DIR; });
}

fs::path normalized(const fs::path& p) {
    std::error_code ec;
    fs::path n = fs::weakly_canonical(fs::absolute(p, ec), ec);
    if (ec) {
        std::error_code aec;
        n = fs::absolute(p, aec).lexically_normal();
    }
    if (!n.has_filename() && n.has_parent_path() && n != n.root_path()) {
        n = n.parent_path();
    }
    return n;
}

bool inside(const fs::path& p, const fs::path& folder) {
    return p == folder || DocumentFiles::remap(p, folder, "/") != p;
}

qint64 mtimeOf(const fs::path& p, std::error_code& ec) {
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

/// Case-insensitive, as Android's shared storage is
std::string folded(const std::string& name) { return QString::fromStdString(name).toCaseFolded().toStdString(); }

/// A name in `home` that is neither there nor taken by another move: "Name", "Name (2)", ... ("stem (2).ext" for a
/// file).
std::string freeName(const fs::path& home, const std::string& name, bool folder, const std::set<std::string>& taken) {
    std::error_code ec;
    auto isFree = [&](const std::string& n) { return !fs::exists(home / n, ec) && !taken.count(folded(n)); };
    if (isFree(name)) {
        return name;
    }
    const fs::path p(name);
    const std::string stem = folder ? name : p.stem().string(), ext = folder ? std::string() : p.extension().string();
    for (int i = 2;; ++i) {
        std::string n = stem + " (" + std::to_string(i) + ")" + ext;
        if (isFree(n)) {
            return n;
        }
    }
}

/// Copies `from` to `to`, hashing what is read. False with `error`.
bool copyFile(const fs::path& from, const fs::path& to, QByteArray& hash, qint64& read, const std::atomic<bool>& cancel,
              const std::function<void(qint64)>& step, std::string& error) {
    QFile in(qstr(from));
    if (!in.open(QIODevice::ReadOnly)) {
        error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be read: %2")
                            .arg(qstr(from), in.errorString()));
        return false;
    }
    QFile out(qstr(to));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be written: %2")
                            .arg(qstr(to), out.errorString()));
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha1);
    read = 0;
    QByteArray buffer;
    while (!in.atEnd()) {
        if (cancel) {
            error = str(QCoreApplication::translate("LibraryMigration", "Cancelled."));
            return false;
        }
        buffer = in.read(CHUNK);
        if (buffer.isEmpty() && in.error() != QFileDevice::NoError) {
            error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be read: %2")
                                .arg(qstr(from), in.errorString()));
            return false;
        }
        if (out.write(buffer) != buffer.size()) {
            error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be written: %2")
                                .arg(qstr(to), out.errorString()));
            return false;
        }
        h.addData(buffer);
        read += buffer.size();
        step(buffer.size());
    }
    if (!out.flush()) {
        error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be written: %2")
                            .arg(qstr(to), out.errorString()));
        return false;
    }
#ifdef Q_OS_UNIX
    ::fsync(out.handle());  // (on the storage before the original can go)
#endif
    out.close();
    hash = h.result();
    return true;
}

/// The hash of a file (empty if it cannot be read).
QByteArray hashOf(const fs::path& file, const std::atomic<bool>& cancel, const std::function<void(qint64)>& step) {
    QFile f(qstr(file));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha1);
    while (!f.atEnd() && !cancel) {
        const QByteArray buffer = f.read(CHUNK);
        if (buffer.isEmpty() && f.error() != QFileDevice::NoError) {
            return {};
        }
        h.addData(buffer);
        step(buffer.size());
    }
    return h.result();
}

fs::path pathIn(const Move& m, const fs::path& base, const FileRecord& f) { return m.folder ? base / f.rel : base; }

/// Moves the files of the config folder `from` into `to`; the reading positions (pages.json) of both are merged
/// (those of `from` win: they are the moved documents').
void mergeConfig(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (!fs::is_directory(from, ec) || from == to) {
        return;
    }
    fs::create_directories(to, ec);
    for (auto it = fs::directory_iterator(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path target = to / it->path().filename();
        std::error_code e;
        if (!fs::exists(target, e)) {
            fs::rename(it->path(), target, e);
        } else if (it->path().filename() == "pages.json") {
            auto read = [](const fs::path& p) {
                QFile f(qstr(p));
                return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
            };
            QJsonObject merged = read(target);
            const QJsonObject moved = read(it->path());
            for (auto i = moved.begin(); i != moved.end(); ++i) {
                merged.insert(i.key(), i.value());
            }
            QSaveFile f(qstr(target));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(merged).toJson(QJsonDocument::Compact));
                f.commit();
            }
        }
    }
    fs::remove_all(from, ec);
}

}  // namespace

std::vector<std::pair<fs::path, fs::path>> Plan::pairs() const {
    std::vector<std::pair<fs::path, fs::path>> p;
    for (const Move& m: moves) {
        p.emplace_back(m.from, m.to);
    }
    return p;
}

bool hasContent(const fs::path& home) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(home, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it.depth() == 0 && isStaging(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code e;
        if (!it->is_directory(e)) {
            return true;
        }
    }
    return false;
}

Plan plan(const fs::path& fromHome, const fs::path& toHome, std::string& error) {
    Plan p;
    p.fromHome = normalized(fromHome);
    p.toHome = normalized(toHome);
    error.clear();
    if (inside(p.fromHome, p.toHome) || inside(p.toHome, p.fromHome)) {
        error = str(QCoreApplication::translate("LibraryMigration", "The libraries cannot move into themselves."));
        return p;
    }
    std::error_code ec;
    if (!fs::exists(p.fromHome, ec)) {
        return p;
    }
    std::vector<fs::path> entries;
    for (auto it = fs::directory_iterator(p.fromHome, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!isStaging(it->path())) {
            entries.push_back(it->path());
        }
    }
    if (ec) {
        error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be read: %2")
                            .arg(qstr(p.fromHome), QString::fromStdString(ec.message())));
        return p;
    }
    std::sort(entries.begin(), entries.end());
    std::set<std::string> taken;
    for (const fs::path& e: entries) {
        std::error_code se;
        const auto st = fs::symlink_status(e, se);
        if (fs::is_symlink(st)) {
            continue;  // (never made by the app; it stays where it is)
        }
        Move m;
        m.from = e;
        m.folder = fs::is_directory(st);
        if (m.folder) {
            for (auto it = fs::recursive_directory_iterator(e, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                std::error_code fe;
                const auto fst = it->symlink_status(fe);
                const std::string rel = it->path().lexically_relative(e).string();
                if (fs::is_symlink(fst)) {
                    continue;
                }
                if (fs::is_directory(fst)) {
                    m.dirs.push_back(rel);
                } else if (fs::is_regular_file(fst)) {
                    FileRecord f;
                    f.rel = rel;
                    f.size = static_cast<qint64>(it->file_size(fe));
                    m.files.push_back(f);
                }
            }
            if (ec) {
                error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be read: %2")
                                    .arg(qstr(e), QString::fromStdString(ec.message())));
                return p;
            }
        } else if (fs::is_regular_file(st)) {
            FileRecord f;
            f.size = static_cast<qint64>(fs::file_size(e, se));
            m.files.push_back(f);
        } else {
            continue;
        }
        const std::string name = e.filename().string();
        std::error_code xe;
        const bool clash = fs::exists(p.toHome / name, xe) || taken.count(folded(name));
        if (clash && m.folder && m.files.empty()) {
            p.dropped.push_back(e);  // (an empty library: nothing to move)
            continue;
        }
        const std::string target = clash ? freeName(p.toHome, name, m.folder, taken) : name;
        taken.insert(folded(target));
        m.to = p.toHome / target;
        m.renamed = target != name;
        m.staging = p.toHome / (STAGING + target);
        for (const FileRecord& f: m.files) {
            p.bytes += f.size;
        }
        p.files += static_cast<int>(m.files.size());
        p.moves.push_back(std::move(m));
    }
    return p;
}

bool copyAndVerify(Plan& plan, const std::atomic<bool>& cancel, const std::function<void(const Progress&)>& progress,
                   std::string& error) {
    error.clear();
    std::error_code ec;
    fs::create_directories(plan.toHome, ec);
    if (ec) {
        error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be made: %2")
                            .arg(qstr(plan.toHome), QString::fromStdString(ec.message())));
        return false;
    }
    const fs::space_info space = fs::space(plan.toHome, ec);
    if (!ec && static_cast<qint64>(space.available) < plan.bytes + SPARE_BYTES) {
        error = str(QCoreApplication::translate("LibraryMigration",
                                                "There is not enough free space: %1 MB are needed, %2 MB are free.")
                            .arg((plan.bytes + SPARE_BYTES) / (1024 * 1024))
                            .arg(static_cast<qint64>(space.available) / (1024 * 1024)));
        return false;
    }
    Progress pr;
    pr.totalBytes = plan.bytes;
    pr.totalFiles = plan.files;
    const auto step = [&](qint64 bytes) {
        pr.bytes += bytes;
        if (progress) {
            progress(pr);
        }
    };
    const auto fail = [&](const std::string& why) {
        error = why;
        discard(plan);
        return false;
    };
    std::function<bool(const fs::path&, const fs::path&)> hook;
    {
        std::lock_guard lock(hookMutex);
        hook = copyHook;
    }

    // Copy
    for (Move& m: plan.moves) {
        fs::remove_all(m.staging, ec);  // (a staging folder of a move that was ended)
        if (m.folder) {
            fs::create_directories(m.staging, ec);
            for (const std::string& d: m.dirs) {
                fs::create_directories(m.staging / d, ec);
            }
            if (ec) {
                return fail(str(QCoreApplication::translate("LibraryMigration", "%1 cannot be made: %2")
                                        .arg(qstr(m.staging), QString::fromStdString(ec.message()))));
            }
        }
        for (FileRecord& f: m.files) {
            if (cancel) {
                return fail(str(QCoreApplication::translate("LibraryMigration", "Cancelled.")));
            }
            const fs::path source = pathIn(m, m.from, f), copy = pathIn(m, m.staging, f);
            std::error_code se;
            f.mtimeNs = mtimeOf(source, se);
            if (se) {
                return fail(str(QCoreApplication::translate("LibraryMigration", "%1 cannot be read: %2")
                                        .arg(qstr(source), QString::fromStdString(se.message()))));
            }
            qint64 read = 0;
            std::string why;
            if (!copyFile(source, copy, f.hash, read, cancel, step, why)) {
                return fail(why);
            }
            if (hook && !hook(source, copy)) {
                return fail(str(QCoreApplication::translate("LibraryMigration", "%1 cannot be written.").arg(qstr(copy))));
            }
            if (read != f.size) {
                pr.totalBytes += read - f.size;
                plan.bytes += read - f.size;
                f.size = read;
            }
            // (the time too: the library knows its documents by size and time; a storage that does not take it
            // costs a new reading of the document, nothing else)
            std::error_code te;
            fs::last_write_time(copy, fs::file_time_type(std::chrono::duration_cast<fs::file_time_type::duration>(
                                              std::chrono::nanoseconds(f.mtimeNs))),
                                te);
            ++pr.files;
            step(0);
        }
    }

    // Verify: every copy read back, every original unchanged since
    pr = Progress{Progress::Step::Verify, 0, plan.bytes, 0, plan.files};
    step(0);
    for (const Move& m: plan.moves) {
        for (const FileRecord& f: m.files) {
            if (cancel) {
                return fail(str(QCoreApplication::translate("LibraryMigration", "Cancelled.")));
            }
            const fs::path source = pathIn(m, m.from, f), copy = pathIn(m, m.staging, f);
            std::error_code se;
            const auto size = static_cast<qint64>(fs::file_size(copy, se));
            if (se || size != f.size || hashOf(copy, cancel, step) != f.hash) {
                if (cancel) {
                    return fail(str(QCoreApplication::translate("LibraryMigration", "Cancelled.")));
                }
                return fail(str(QCoreApplication::translate("LibraryMigration",
                                                            "The copy of %1 is not the same as the original.")
                                        .arg(qstr(source))));
            }
            const auto sourceSize = static_cast<qint64>(fs::file_size(source, se));
            if ((se || sourceSize != f.size || mtimeOf(source, se) != f.mtimeNs || se) && !isCache(f.rel)) {
                return fail(str(QCoreApplication::translate("LibraryMigration",
                                                            "%1 was changed while it was moved. Please try again.")
                                        .arg(qstr(source))));
            }
            ++pr.files;
            step(0);
        }
    }
    return true;
}

bool commit(Plan& plan, std::string& error) {
    error.clear();
    std::vector<Move*> done;
    std::set<std::string> taken;
    for (Move& m: plan.moves) {
        taken.insert(folded(m.to.filename().string()));
    }
    for (Move& m: plan.moves) {
        std::error_code ec;
        if (fs::exists(m.to, ec)) {
            // (made meanwhile by another app: another name, never into it)
            taken.erase(folded(m.to.filename().string()));
            const std::string name = freeName(plan.toHome, m.to.filename().string(), m.folder, taken);
            taken.insert(folded(name));
            m.to = plan.toHome / name;
            m.renamed = m.to.filename() != m.from.filename();
        }
        fs::rename(m.staging, m.to, ec);
        if (ec) {
            error = str(QCoreApplication::translate("LibraryMigration", "%1 cannot be renamed to %2: %3")
                                .arg(qstr(m.staging), qstr(m.to), QString::fromStdString(ec.message())));
            for (Move* d: done) {
                std::error_code re;
                fs::rename(d->to, d->staging, re);
            }
            discard(plan);
            return false;
        }
        done.push_back(&m);
    }
    return true;
}

void discard(const Plan& plan) {
    for (const Move& m: plan.moves) {
        std::error_code ec;
        if (!m.staging.empty() && isStaging(m.staging)) {
            fs::remove_all(m.staging, ec);
        }
    }
}

fs::path manifestFile() { return Util::getConfigFile("library-move.json"); }

bool writeManifest(const Plan& plan, const fs::path& file) {
    QJsonArray moves;
    for (const Move& m: plan.moves) {
        QJsonArray files;
        for (const FileRecord& f: m.files) {
            files.append(QJsonObject{{"rel", QString::fromStdString(f.rel)},
                                     {"size", QString::number(f.size)},
                                     {"mtime", QString::number(f.mtimeNs)},
                                     {"hash", QString::fromLatin1(f.hash.toHex())}});
        }
        QJsonArray dirs;
        for (const std::string& d: m.dirs) {
            dirs.append(QString::fromStdString(d));
        }
        moves.append(QJsonObject{{"from", qstr(m.from)},
                                 {"to", qstr(m.to)},
                                 {"folder", m.folder},
                                 {"renamed", m.renamed},
                                 {"dirs", dirs},
                                 {"files", files}});
    }
    QJsonArray dropped;
    for (const fs::path& d: plan.dropped) {
        dropped.append(qstr(d));
    }
    const QJsonObject o{{"fromHome", qstr(plan.fromHome)},
                        {"toHome", qstr(plan.toHome)},
                        {"moves", moves},
                        {"dropped", dropped}};
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    QSaveFile f(qstr(file));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

std::optional<Plan> readManifest(const fs::path& file) {
    QFile f(qstr(file));
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) {
        return std::nullopt;
    }
    Plan p;
    p.fromHome = str(o.value("fromHome").toString());
    p.toHome = str(o.value("toHome").toString());
    for (const QJsonValue& v: o.value("moves").toArray()) {
        const QJsonObject mo = v.toObject();
        Move m;
        m.from = str(mo.value("from").toString());
        m.to = str(mo.value("to").toString());
        m.folder = mo.value("folder").toBool(true);
        m.renamed = mo.value("renamed").toBool();
        for (const QJsonValue& d: mo.value("dirs").toArray()) {
            m.dirs.push_back(str(d.toString()));
        }
        for (const QJsonValue& fv: mo.value("files").toArray()) {
            const QJsonObject fo = fv.toObject();
            FileRecord r;
            r.rel = str(fo.value("rel").toString());
            r.size = fo.value("size").toString().toLongLong();
            r.mtimeNs = fo.value("mtime").toString().toLongLong();
            r.hash = QByteArray::fromHex(fo.value("hash").toString().toLatin1());
            p.bytes += r.size;
            m.files.push_back(r);
        }
        p.files += static_cast<int>(m.files.size());
        if (m.from.empty() || m.to.empty()) {
            return std::nullopt;
        }
        p.moves.push_back(std::move(m));
    }
    for (const QJsonValue& d: o.value("dropped").toArray()) {
        p.dropped.emplace_back(str(d.toString()));
    }
    return p;
}

std::vector<fs::path> journalFiles() {
    std::vector<fs::path> files;
    const fs::path main = SessionRecovery::defaultJournalFile();
    std::error_code ec;
    if (fs::exists(main, ec)) {
        files.push_back(main);
    }
    for (auto it = fs::directory_iterator(main.parent_path() / "sessions", ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        if (it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    return files;
}

void relocateState(const Plan& plan, const std::vector<fs::path>& journals) {
    const fs::path configs = Util::getConfigSubfolder("libraries");
    for (const Move& m: plan.moves) {
        if (!m.folder) {
            continue;
        }
        const Library before(m.from), after(m.to);
        mergeConfig(configs / before.key(), configs / after.key());
        // The library's cache in the app cache (previews, search index): kept under the key of its new path
        const fs::path oldCache = CacheLocation(before.root(), CacheLocation::Mode::AppCache).appCacheDir();
        const fs::path newCache = CacheLocation(after.root(), CacheLocation::Mode::AppCache).appCacheDir();
        std::error_code ec;
        if (oldCache != newCache && fs::exists(oldCache, ec)) {
            if (fs::exists(newCache, ec)) {
                fs::remove_all(oldCache, ec);
            } else {
                fs::create_directories(newCache.parent_path(), ec);
                fs::rename(oldCache, newCache, ec);
            }
        }
    }
    const auto pairs = plan.pairs();
    DocumentPlaces::moved(pairs);
    for (const fs::path& file: journals) {
        auto journal = SessionRecovery::readJournal(file);
        if (!journal) {
            continue;
        }
        bool changed = false;
        for (auto& tab: journal->tabs) {
            for (const auto& [from, to]: pairs) {
                if (const fs::path n = DocumentFiles::remap(tab.file, from, to); !tab.file.empty() && n != tab.file) {
                    tab.file = n;
                    changed = true;
                    break;
                }
            }
        }
        if (changed) {
            SessionRecovery::writeJournal(*journal, file);
        }
    }
}

Cleanup cleanUp(const Plan& plan) {
    Cleanup c;
    for (const Move& m: plan.moves) {
        for (const FileRecord& f: m.files) {
            const fs::path old = pathIn(m, m.from, f);
            std::error_code ec;
            if (!fs::exists(old, ec)) {
                continue;
            }
            const auto size = static_cast<qint64>(fs::file_size(old, ec));
            const bool same = !ec && size == f.size && mtimeOf(old, ec) == f.mtimeNs && !ec;
            if ((same || isCache(f.rel)) && fs::remove(old, ec)) {
                ++c.removed;
            }
        }
        if (m.folder) {
            // Its folders, the deepest first, as far as they are empty now
            std::vector<std::string> dirs = m.dirs;
            std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
            for (const std::string& d: dirs) {
                std::error_code ec;
                fs::remove(m.from / d, ec);  // (fails for a folder that is not empty)
            }
            std::error_code ec;
            fs::remove(m.from, ec);
        }
    }
    for (const fs::path& d: plan.dropped) {
        std::error_code ec;
        if (!hasContent(d)) {
            fs::remove_all(d, ec);
        }
    }
    // What is left: cache files written meanwhile go too (with the folders that are empty then); the rest are files
    // changed since they were copied (or made meanwhile)
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (auto it = fs::recursive_directory_iterator(plan.fromHome, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code e;
        if (it->is_directory(e)) {
            dirs.push_back(it->path());
        } else if (isCache(it->path().lexically_relative(plan.fromHome)) && fs::remove(it->path(), e)) {
            ++c.removed;
        } else {
            c.kept.push_back(it->path());
        }
    }
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.string().size() > b.string().size(); });
    for (const fs::path& d: dirs) {
        std::error_code e;
        fs::remove(d, e);  // (only if empty)
    }
    if (c.kept.empty()) {
        fs::remove_all(plan.fromHome, ec);  // (only empty folders are left)
    }
    return c;
}

void setCopyHook(std::function<bool(const fs::path&, const fs::path&)> hook) {
    std::lock_guard lock(hookMutex);
    copyHook = std::move(hook);
}

}  // namespace xqt::LibraryMigration

// --- LibraryMove -----------------------------------------------------------------------------------------------------

namespace xqt {

struct LibraryMove::State {
    std::atomic<bool> cancel{false};
};

LibraryMove::LibraryMove(QObject* parent): QObject(parent) {}

LibraryMove::~LibraryMove() { cancel(); }

void LibraryMove::cancel() {
    if (state) {
        state->cancel = true;
    }
}

double LibraryMove::fraction() const {
    const double part = bytesTotal > 0 ? static_cast<double>(bytesDone) / static_cast<double>(bytesTotal)
                                       : (filesTotal > 0 ? static_cast<double>(filesDone) / filesTotal : 1.0);
    if (currentStep == QLatin1String("copy")) {
        return 0.5 * part;
    }
    if (currentStep == QLatin1String("verify")) {
        return 0.5 + 0.5 * part;
    }
    return currentStep == QLatin1String("clean") ? 1.0 : 0.0;
}

void LibraryMove::finish() {
    state.reset();
    currentStep.clear();
    Q_EMIT progressChanged();
    Q_EMIT runningChanged();
}

void LibraryMove::startCopy(LibraryMigration::Plan plan) {
    if (state) {
        return;
    }
    state = std::make_shared<State>();
    currentStep = QStringLiteral("copy");
    filesDone = 0;
    filesTotal = plan.files;
    bytesDone = 0;
    bytesTotal = plan.bytes;
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    QPointer<LibraryMove> self(this);
    std::shared_ptr<State> st = state;
    QThreadPool::globalInstance()->start([self, st, plan = std::move(plan)]() mutable {
        qint64 reported = -1;
        LibraryMigration::Progress::Step reportedStep = LibraryMigration::Progress::Step::Copy;
        int reportedFiles = -1;
        std::string error;
        const bool ok = LibraryMigration::copyAndVerify(
                plan, st->cancel,
                [&](const LibraryMigration::Progress& p) {
                    // (at most every 4 MB, or for each file)
                    if (p.step == reportedStep && p.files == reportedFiles && p.bytes - reported < 4 * 1024 * 1024) {
                        return;
                    }
                    reported = p.bytes;
                    reportedStep = p.step;
                    reportedFiles = p.files;
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, p] {
                        if (self && self->state == st) {
                            self->currentStep = p.step == LibraryMigration::Progress::Step::Copy
                                                        ? QStringLiteral("copy")
                                                        : QStringLiteral("verify");
                            self->filesDone = p.files;
                            self->filesTotal = p.totalFiles;
                            self->bytesDone = p.bytes;
                            self->bytesTotal = p.totalBytes;
                            Q_EMIT self->progressChanged();
                        }
                    });
                },
                error);
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [self, st, ok, error = QString::fromStdString(error), plan = std::move(plan)] {
                                      if (!self || self->state != st) {
                                          return;
                                      }
                                      self->finish();
                                      Q_EMIT self->copied(ok, error, plan);
                                  });
    });
}

void LibraryMove::startCleanup(LibraryMigration::Plan plan) {
    if (state) {
        return;
    }
    state = std::make_shared<State>();
    currentStep = QStringLiteral("clean");
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    QPointer<LibraryMove> self(this);
    std::shared_ptr<State> st = state;
    QThreadPool::globalInstance()->start([self, st, plan = std::move(plan)] {
        const LibraryMigration::Cleanup result = LibraryMigration::cleanUp(plan);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, result] {
            if (!self || self->state != st) {
                return;
            }
            self->finish();
            Q_EMIT self->cleanedUp(result);
        });
    });
}

}  // namespace xqt
