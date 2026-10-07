/*
 * xournal-qt: zip files through libzip (sharing a folder or the library as a zip, qt/docs/features/library.md "Sharing
 * a folder or the library").
 *
 * Writing: every entry carries its modification time twice, as the DOS time every unzipper reads (local time, two
 * seconds) and as the extended timestamp field (0x5455, UTC seconds) that Info-ZIP, 7-Zip, macOS and this app read, so
 * a file unpacked in another time zone keeps its time. Sizes are exact (libzip writes zip64 where needed). Files are
 * read from disk when the archive is closed (close(): the work happens there, with progress and cancel); libzip
 * writes into a temporary file next to the target and renames it at the end, so a cancelled or failed write leaves
 * nothing. An optional password encrypts every entry with WinZip's AES-256 (aesAvailable(): libzip built with a
 * crypto backend; Explorer and Finder cannot open such zips, 7-Zip and Keka can).
 *
 * Reading: the entries with their names, sizes, UTC times (the 0x5455 field, else the DOS time), whether they are
 * folders, symbolic links or encrypted; one entry at a time into a file, checked against its stated size.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "filesystem.h"

typedef struct zip zip_t;

namespace xqt::Zip {

/// libzip can write and read WinZip AES-256 here (built with a crypto backend: OpenSSL, GnuTLS, mbed TLS, Windows'
/// or Apple's). Not in the Android build (vcpkg's libzip without features): the option is hidden there.
bool aesAvailable();

/// The extended timestamp extra field (0x5455) with a modification time (UTC seconds), as stored.
std::string extendedTimestamp(std::time_t mtime);

class Writer {
public:
    /// A new zip at `file` (an existing one is replaced at close()).
    explicit Writer(const fs::path& file);
    /// Discards what was not closed.
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool ok() const { return archive != nullptr && failure.empty(); }
    const std::string& error() const { return failure; }
    /// Encrypt the entries added from now on with AES-256 and this password (aesAvailable()). Empty: none.
    void setPassword(const std::string& password);

    /// A file read from `source` at close(); `name` with "/" between folders. `compress` false: stored (PDFs, PNGs,
    /// recordings are compressed already: deflating them again only costs time).
    bool addFile(const std::string& name, const fs::path& source, std::time_t mtime, bool compress = true);
    /// Bytes from memory.
    bool addData(const std::string& name, std::string data, std::time_t mtime, bool compress = true);
    /// A folder entry ("name/"), so empty folders come along.
    bool addFolder(const std::string& name, std::time_t mtime);
    /// Write it: the files are read and compressed now. `progress(fraction 0..1)` returns false to cancel (the zip is
    /// not written then, and false is returned with error() "cancelled"); it is also called with -1 only to ask whether
    /// to go on.
    bool close(const std::function<bool(double)>& progress = {});
    /// Forget it: nothing is written.
    void discard();

private:
    bool added(int64_t index, const std::string& name, std::time_t mtime, bool compress);

    zip_t* archive = nullptr;
    std::string failure;
    std::string password;
    std::vector<std::shared_ptr<std::string>> buffers;  ///< addData's bytes, until close()
    std::function<bool(double)> onProgress;
};

struct Entry {
    int64_t index = -1;
    std::string name;       ///< as stored ("/" between folders; "name/" for a folder)
    uint64_t size = 0;      ///< uncompressed
    uint64_t compressed = 0;
    std::time_t mtime = 0;  ///< UTC: the extended timestamp, else the DOS time read as local time
    bool utc = false;       ///< mtime is from the extended timestamp
    bool folder = false;
    bool symlink = false;   ///< a symbolic link (Unix attributes)
    bool encrypted = false;
    bool aes = false;       ///< WinZip AES (else traditional PKWARE encryption, or none)
};

class Reader {
public:
    explicit Reader(const fs::path& file);
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool ok() const { return archive != nullptr; }
    const std::string& error() const { return failure; }
    const std::vector<Entry>& entries() const { return list; }
    bool encrypted() const;
    void setPassword(const std::string& password) { this->password = password; }
    /// The password opens the encrypted entries (the first one is tried).
    bool passwordWorks();
    /// Write an entry into `target` (created, replaced). Stops (false) when it holds more than its stated size,
    /// on `cancel`, or a wrong password. `written` adds the bytes written.
    bool extract(const Entry& entry, const fs::path& target, const std::function<bool()>& cancel, uint64_t& written,
                 std::string& error);
    /// An entry's bytes (at most `limit`; more: empty with `error`).
    std::string read(const Entry& entry, uint64_t limit, std::string& error);

private:
    zip_t* archive = nullptr;
    std::string failure;
    std::string password;
    std::vector<Entry> list;
};

}  // namespace xqt::Zip
