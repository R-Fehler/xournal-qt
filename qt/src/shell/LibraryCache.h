/*
 * xournal-qt: where a library keeps its cache, and the files it is kept in ("packs").
 *
 * Every folder with documents has a cache of its own, with only the documents directly in it (never those of its
 * subfolders): the hidden folder "<folder>/.xournal_library". A library can keep it in the app cache instead
 * ("~/.cache/xournal-qt/libraries/<library key>/<folder in the library>/.xournal_library"), which keeps synced folders
 * clean; folders that cannot be written use the app cache anyway. Entries are keyed by file name, so a folder moved
 * by any program takes its cache along.
 *
 * A pack is one file of such a folder with the entries of all its documents of one kind: "notes" (what the .xopp
 * files say: small, changes on every save), "pdf-text" (big, changes when a PDF does), "previews". It is CBOR,
 * compressed with zlib (not the previews: PNG is compressed already), behind a small header with the format number.
 * A pack is always written whole and under another name first (QSaveFile), never changed in place: sync clients
 * upload whole files anyway, and a reader never sees half a file. An entry of over 1 MB (the text of a long PDF)
 * gets a file of its own, so it is not written again with its neighbours.
 *
 * WriteScheduler writes a few seconds after the last change (and at the latest half a minute after the first one),
 * so a burst of changes is one write.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <optional>
#include <set>

#include <QCborMap>
#include <QObject>
#include <QString>
#include <QTimer>

#include "filesystem.h"

namespace xqt {

class CacheLocation {
public:
    enum class Mode { Folders, AppCache };

    CacheLocation() = default;
    /// The cache of the library at `root`; `appCacheDir` is its folder in the app cache (default:
    /// "~/.cache/xournal-qt/libraries/<key of root>").
    explicit CacheLocation(fs::path root, Mode mode = Mode::Folders, fs::path appCacheDir = {});

    bool valid() const { return !rootDir.empty(); }
    const fs::path& root() const { return rootDir; }
    Mode mode() const { return cacheMode; }
    const fs::path& appCacheDir() const { return appDir; }
    /// `path` is the library or in it.
    bool contains(const fs::path& path) const;

    /// Where the cache of `folder` (the library or a folder in it) is kept: in the folder, or in the app cache when
    /// the library keeps it there or the folder cannot be written.
    fs::path dirOf(const fs::path& folder) const;
    /// The cache folder in the folder itself, and its mirror in the app cache.
    fs::path inFolder(const fs::path& folder) const;
    fs::path mirrorOf(const fs::path& folder) const;

private:
    fs::path rootDir, appDir;
    Mode cacheMode = Mode::Folders;
};

namespace Packs {

/// Format of the pack files themselves (header, CBOR layout); the content has a format of its own (see read()).
constexpr int FORMAT = 1;
/// Entries of this size (encoded) or bigger get a file of their own.
constexpr qsizetype OWN_FILE_SIZE = 1024 * 1024;

/// "<dir>/<pack>.pack"
fs::path fileOf(const fs::path& dir, const QString& pack);
/// The file of an entry that has one: "<dir>/<pack>-<hash of the key>.pack"
fs::path ownFileOf(const fs::path& dir, const QString& pack, const QString& key);

/// The entries of a pack (by key); nothing if it is missing, damaged, not a pack of this name, or of another pack
/// or content format (it is written anew then).
std::optional<QCborMap> read(const fs::path& dir, const QString& pack, int contentFormat);

/// Write a pack whole (atomically). Entries of OWN_FILE_SIZE or more go to a file of their own, which is written only
/// when its key is in `changed` (or the file is missing; `changed` null: all); the files of entries that are gone
/// are removed. No entries: the pack is removed. Returns false if it could not be written.
bool write(const fs::path& dir, const QString& pack, int contentFormat, const QCborMap& entries, bool compress,
           const std::set<QString>* changed = nullptr);
/// Remove a pack and the files of its entries.
void remove(const fs::path& dir, const QString& pack);

/// A pack file of a cache folder (also the files of big entries, and QSaveFile's temporary files).
bool isOurs(const QString& fileName);
/// A file or folder of the layout before the packs: "index/", "previews/", "pages.json" (at the library's root).
bool isOldLayout(const QString& fileName);
/// Remove the cache folder if nothing but packs are in it (e.g. its last document is gone; the old layout keeps
/// it: it may not be converted yet). Returns whether it is gone.
bool removeIfOnlyOurs(const fs::path& dir);
/// Remove our files (packs and the old layout) from the cache folder, and the folder if nothing else is left.
/// Returns the bytes removed.
qint64 removeOurs(const fs::path& dir);
/// Bytes of our files (packs and the old layout) in a cache folder.
qint64 sizeOf(const fs::path& dir);
/// Remove the files of the old layout ("index/*.json", "previews/*.png", "pages.json"), nothing else.
void removeOldLayout(const fs::path& dir);

}  // namespace Packs

/// Calls `write` (on the thread it lives on) a while after the last call of changed(), at the latest `maxDelay`
/// after the first one.
class WriteScheduler final: public QObject {
    Q_OBJECT
public:
    explicit WriteScheduler(std::function<void()> write, QObject* parent = nullptr);
    /// Something changed. Any thread.
    void changed();
    void setDelays(int quietMs, int maxDelayMs);
    /// A write is waiting (the thread it lives on).
    bool pending() const { return quiet.isActive(); }
    /// Forget the waiting write (the thread it lives on).
    void cancel();

    static constexpr int QUIET_MS = 3000;
    static constexpr int MAX_DELAY_MS = 30000;

private:
    void arm();
    void fire();

    std::function<void()> writeFn;
    QTimer quiet, latest;
};

}  // namespace xqt
