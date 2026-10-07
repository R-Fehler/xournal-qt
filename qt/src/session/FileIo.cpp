/*
 * xournal-qt: the small file helpers (FileIo.h).
 *
 * @license GNU GPLv2 or later
 */
#include "FileIo.h"

#include <QCryptographicHash>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include <fcntl.h>
#include <zlib.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "util/Util.h"

namespace xqt::fileio {

// --- writing a file whole ---------------------------------------------------------------------------------------

fs::path tempNameFor(const fs::path& target) {
    static std::atomic<unsigned> counter{0};
    return target.parent_path() / ("." + target.filename().string() + "." + std::to_string(Util::getPid()) + "-" +
                                   std::to_string(++counter) + ".part");
}

bool syncFile(const fs::path& file) {
#ifdef _WIN32
    std::error_code ec;
    if (fs::is_directory(file, ec)) {
        return true;  // (Windows syncs no folder; a rename is in its journal)
    }
    const int fd = _wopen(file.wstring().c_str(), _O_RDWR | _O_BINARY);
    if (fd < 0) {
        return false;
    }
    const bool ok = _commit(fd) == 0;
    _close(fd);
    return ok;
#else
    const int fd = ::open(file.string().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
#endif
}

namespace {
/// A link is followed: the file it points to is written, the link stays (as QSaveFile does).
fs::path resolved(fs::path target) {
    std::error_code ec;
    if (fs::is_symlink(target, ec)) {
        if (fs::path real = fs::canonical(target, ec); !ec) {
            return real;
        }
    }
    return target;
}
}  // namespace

AtomicFile::AtomicFile(fs::path target): to(resolved(std::move(target))), tmp(tempNameFor(to)) {}

AtomicFile::~AtomicFile() { cancel(); }

void AtomicFile::cancel() {
    if (!done) {
        std::error_code ec;
        fs::remove(tmp, ec);
        done = true;
    }
}

bool AtomicFile::commit(std::string& error, Sync sync) {
    if (done) {
        error = "Already written.";
        return false;
    }
    std::error_code ec;
    if (!fs::exists(tmp, ec)) {
        cancel();
        error = "Could not write \"" + to.string() + "\": nothing was written.";
        return false;
    }
    if (const auto st = fs::status(to, ec); !ec && fs::exists(st)) {
        fs::permissions(tmp, st.permissions(), ec);  // (the file keeps its permissions, as with QSaveFile)
        ec.clear();
    }
    if (sync == Sync::Durable && !syncFile(tmp)) {
        cancel();
        error = "Could not write \"" + to.string() + "\": it could not be synced to the storage.";
        return false;
    }
    fs::rename(tmp, to, ec);
    if (ec) {
        cancel();
        error = "Could not write \"" + to.string() + "\": " + ec.message();
        return false;
    }
    done = true;
    if (sync == Sync::Durable) {
        syncFile(to.parent_path().empty() ? fs::path(".") : to.parent_path());  // (the rename itself)
    }
    return true;
}

bool writeFileAtomically(const fs::path& target, std::string_view bytes, std::string& error, Sync sync) {
    AtomicFile file(target);
    {
        std::ofstream out(file.temp(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            error = "Could not write \"" + target.string() + "\".";
            return false;
        }
    }
    return file.commit(error, sync);
}

bool writeFileAtomically(const QString& target, const QByteArray& bytes, Sync sync, QString* error) {
    std::string e;
    const bool ok =
            writeFileAtomically(fs::path(target.toStdU16String()),
                                std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())), e, sync);
    if (!ok && error) {
        *error = QString::fromStdString(e);
    }
    return ok;
}

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// --- one writer of a file at a time -----------------------------------------------------------------------------

struct FileWriteLock::Entry {
    std::recursive_mutex mtx;
    std::atomic<int> users{0};
};

namespace {
struct Locks {
    std::mutex mtx;
    std::unordered_map<std::string, std::weak_ptr<FileWriteLock::Entry>> entries;
};
Locks& locks() {
    static auto* l = new Locks;  // (never destroyed: workers may still write while statics go)
    return *l;
}
std::string keyOf(const fs::path& file) {
    std::error_code ec;
    const fs::path abs = fs::absolute(file, ec);
    return (ec ? file : abs).lexically_normal().string();
}
}  // namespace

FileWriteLock::FileWriteLock(const fs::path& file) {
    {
        Locks& l = locks();
        std::lock_guard lock(l.mtx);
        auto& weak = l.entries[keyOf(file)];
        entry = weak.lock();
        if (!entry) {
            entry = std::make_shared<Entry>();
            weak = entry;
        }
        if (l.entries.size() > 64) {
            std::erase_if(l.entries, [](const auto& e) { return e.second.expired(); });
        }
        ++entry->users;
    }
    entry->mtx.lock();
}

FileWriteLock::~FileWriteLock() {
    entry->mtx.unlock();
    --entry->users;
}

bool FileWriteLock::busy(const fs::path& file) {
    Locks& l = locks();
    std::lock_guard lock(l.mtx);
    auto it = l.entries.find(keyOf(file));
    if (it == l.entries.end()) {
        return false;
    }
    const auto e = it->second.lock();
    return e && e->users > 0;
}

// --- telling files apart ----------------------------------------------------------------------------------------

std::optional<FileStamp> fileStamp(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto time = fs::last_write_time(file, ec);
    if (ec) {
        return std::nullopt;
    }
    // (libc++'s file clock counts in a 128-bit integer; nanoseconds fit into 64 bits until 2262)
    return FileStamp{size,
                     static_cast<long long>(
                             std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count())};
}

std::string stampOf(const fs::path& file) {
    const auto s = fileStamp(file);
    return s ? std::to_string(s->size) + "-" + std::to_string(s->time) : std::string();
}

std::string hex16(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string md5Of(std::string_view bytes) {
    return QCryptographicHash::hash(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())),
                                    QCryptographicHash::Md5)
            .toStdString();
}

bool hasExtension(const fs::path& p, std::string_view ext) {
    const std::string e = p.extension().string();
    return e.size() == ext.size() && std::equal(e.begin(), e.end(), ext.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

// --- gzip ---------------------------------------------------------------------------------------------------------

std::string gzip(std::string_view data) {
    z_stream z{};
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("Could not compress the data");
    }
    std::string out(deflateBound(&z, static_cast<uLong>(data.size())) + 32, '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    if (rc != Z_STREAM_END) {
        throw std::runtime_error("Could not compress the data");
    }
    return out;
}

bool isGzip(std::string_view data) {
    return data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1f &&
           static_cast<unsigned char>(data[1]) == 0x8b;
}

std::string gunzip(std::string_view data, bool& ok) {
    ok = false;
    z_stream z{};
    if (inflateInit2(&z, 15 + 32) != Z_OK) {  // (gzip or zlib, by its header)
        return {};
    }
    std::string out;
    char buf[64 * 1024];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    z.avail_in = static_cast<uInt>(data.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        z.next_out = reinterpret_cast<Bytef*>(buf);
        z.avail_out = sizeof buf;
        rc = inflate(&z, Z_NO_FLUSH);
        out.append(buf, sizeof buf - z.avail_out);
    }
    inflateEnd(&z);
    ok = rc == Z_STREAM_END;
    return out;
}

}  // namespace xqt::fileio
