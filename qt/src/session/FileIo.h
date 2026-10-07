/*
 * xournal-qt: the small file helpers every writer of a file uses (one copy each):
 * - AtomicFile / writeFileAtomically: a file written whole or not at all (a unique temporary name next to it, written,
 *   made durable with fsync, renamed over it);
 * - FileWriteLock: one writer of a file at a time in this process (a save and a tag change of the same PDF);
 * - stampOf: a file's size and modification time, the "is it still the file we looked at" test;
 * - fnv1a, gzip/gunzip, md5Of, hasExtension.
 *
 * All of them may be called from any thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "filesystem.h"

namespace xqt::fileio {

// --- writing a file whole ---------------------------------------------------------------------------------------

/// Whether a written file is made durable (fsync) before it replaces the old one. Files of the user and the app's own
/// state: Durable. Caches that are made again when they are lost, and files written on the UI thread where a sync
/// would stall it (RecentFiles): None.
enum class Sync { Durable, None };

/// A unique temporary name next to `target`: ".<name>.<pid>-<n>.part" (several threads may write the same target at
/// once; IncrementalPdf removes such files of a crash by the prefix ".<name>.").
fs::path tempNameFor(const fs::path& target);

/// Writes `target` whole or not at all. Write into temp() (by name: QPDFWriter, copy_file, QImage::save, ...), then
/// commit(): the temporary file gets the permissions of the file it replaces, is synced (Sync::Durable), renamed over
/// the target, and its folder synced. A file not committed is removed when this goes (also when the writer threw). A
/// target that is a symbolic link: the file it points to is written, the link stays.
class AtomicFile {
public:
    explicit AtomicFile(fs::path target);
    ~AtomicFile();
    AtomicFile(const AtomicFile&) = delete;
    AtomicFile& operator=(const AtomicFile&) = delete;

    const fs::path& target() const { return to; }
    const fs::path& temp() const { return tmp; }
    /// False with `error` (the temporary file is removed).
    bool commit(std::string& error, Sync sync = Sync::Durable);
    /// The temporary file goes, the target stays as it was.
    void cancel();

private:
    fs::path to;
    fs::path tmp;
    bool done = false;
};

/// `bytes` as the whole content of `target` (AtomicFile). False with `error`.
bool writeFileAtomically(const fs::path& target, std::string_view bytes, std::string& error, Sync sync = Sync::Durable);
/// The same for Qt callers; `error` (optional) is set on failure.
bool writeFileAtomically(const QString& target, const QByteArray& bytes, Sync sync = Sync::Durable,
                         QString* error = nullptr);
/// Made durable: its data on the storage (fsync; a folder too). False when it cannot be opened or synced.
bool syncFile(const fs::path& file);

/// A whole file's bytes ({} when it cannot be read).
std::string readFile(const fs::path& file);

// --- one writer of a file at a time -----------------------------------------------------------------------------

/// Held by every writer that reads a file and then replaces it (a save of a PDF with notes, its tags, a version's
/// message): another writer of the same file in this process waits until it goes, so neither writes over what the
/// other wrote. Files are told apart by their absolute normalized path. The same thread may take it again.
class FileWriteLock {
public:
    explicit FileWriteLock(const fs::path& file);
    ~FileWriteLock();
    FileWriteLock(const FileWriteLock&) = delete;
    FileWriteLock& operator=(const FileWriteLock&) = delete;

    /// For tests: whether a writer of `file` holds its lock now (or waits for it).
    static bool busy(const fs::path& file);

    struct Entry;  ///< (FileIo.cpp: one per file that is written)

private:
    std::shared_ptr<Entry> entry;
};

// --- telling files apart ----------------------------------------------------------------------------------------

/// A file's size and modification time (nanoseconds since the file clock's epoch).
struct FileStamp {
    std::uintmax_t size = 0;
    long long time = 0;
    bool operator==(const FileStamp& o) const { return size == o.size && time == o.time; }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};
/// Nothing when the file does not exist (or cannot be read).
std::optional<FileStamp> fileStamp(const fs::path& file);
/// "<size>-<time>" (a file name may hold it: the clean-copy cache names its entries by it); empty when the file does
/// not exist. Every stamp kept as a string is this one: a save compares the file's stamp with the one it read.
std::string stampOf(const fs::path& file);

/// The start value. Not FNV's published offset basis (14695981039346656037): every copy this replaced had it with its
/// last digit missing, and files keep the hashes (the signatures in a PDF with notes' marker, cache names). Keep it.
constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;
/// FNV-1a 64 of `bytes`, continued from `h` (the same value in every run: names of cache files and folders, the
/// signatures of pages and layers in a PDF with notes).
constexpr uint64_t fnv1a(std::string_view bytes, uint64_t h = FNV_OFFSET) {
    for (const char c: bytes) {
        h ^= static_cast<unsigned char>(c);
        h *= FNV_PRIME;
    }
    return h;
}
/// 16 lower-case hex digits.
std::string hex16(uint64_t v);

/// The MD5 of `bytes`, 16 raw bytes (a PDF's embedded-file /CheckSum).
std::string md5Of(std::string_view bytes);

/// Whether `p`'s extension is `ext` (".pdf"), in any case.
bool hasExtension(const fs::path& p, std::string_view ext);

// --- gzip ---------------------------------------------------------------------------------------------------------

/// Gzipped at zlib's default level (the bytes of an embedded .xopp depend on it). Throws std::runtime_error.
std::string gzip(std::string_view data);
/// Whether `data` begins as gzip data does.
bool isGzip(std::string_view data);
/// The content of gzip (or zlib) data; `ok` false (and what could be read) when it does not read whole.
std::string gunzip(std::string_view data, bool& ok);

}  // namespace xqt::fileio
