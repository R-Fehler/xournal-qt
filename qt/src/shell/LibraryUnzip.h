/*
 * xournal-qt: "Open in library…" for a zip (qt/docs/library.md, "Receiving a shared zip").
 *
 * A zip (shared from this app, or any zip of documents) is unpacked into a new folder of the library: the zip's top
 * folder, or a folder named after the zip when its files are not in one, inside a folder the user chooses (Inbox by
 * default), " (2)" when the name is taken. Its files get the times the zip gives (UTC from the extended timestamp,
 * else the DOS time). The library's readings a shared zip carries (".xournal_library/" packs) go where the library
 * keeps its cache (in the folders, or in the app cache), so its documents are not read again: their stamps match,
 * and where an unzip changed their times the library adopts the entries by size and content hash (Library.h).
 *
 * Safe against crafted zips: no paths outside the target (no "..", no absolute paths, no drive letters), symbolic
 * links are left out, at most MAX_ENTRIES entries, the sizes must fit the free space and MAX_BYTES, and each entry is
 * checked against its stated size while it is written. Everything goes into a hidden folder first, which is renamed
 * into place at the end (the library never sees half a folder); cancelled or failed, it is removed.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "LibraryCache.h"
#include "filesystem.h"

namespace xqt {

class LibraryUnzip: public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString current READ current NOTIFY progressChanged)
public:
    static constexpr int MAX_ENTRIES = 200000;
    static constexpr uint64_t MAX_BYTES = 64ull * 1024 * 1024 * 1024;

    struct Inspection {
        bool ok = false;
        std::string error;
        int files = 0;
        uint64_t bytes = 0;         ///< unpacked
        bool encrypted = false;     ///< it needs a password
        bool supported = true;      ///< its encryption can be read here (AES needs Zip::aesAvailable())
        bool share = false;         ///< shared from this app (its manifest), with its readings
        std::string name;           ///< the folder it unpacks into (its top folder, else the zip's name)
    };
    static Inspection inspect(const fs::path& zip);

    struct Result {
        bool ok = false;
        std::string error;
        bool wrongPassword = false;
        bool cancelled = false;
        fs::path folder;            ///< the new folder
        int files = 0;
        std::vector<std::string> skipped;  ///< entries left out, with why
    };
    /// Unpack `zip` into a new folder inside `into` (made if needed) on this thread. `location`: where the library
    /// keeps its cache (the zip's packs go there). `progress(done, total, current)`.
    static Result unpack(const fs::path& zip, const fs::path& into, const CacheLocation& location,
                         const std::string& password, const std::atomic<bool>& cancel,
                         const std::function<void(int, int, const std::string&)>& progress = {});
    /// The path in the target of a zip entry's name, or empty when it must not be written (absolute, "..", a drive
    /// letter, empty).
    static fs::path safePath(const std::string& name);

    explicit LibraryUnzip(QObject* parent = nullptr);
    ~LibraryUnzip() override;

    bool running() const { return state != nullptr; }
    int done() const { return doneCount; }
    int total() const { return totalCount; }
    QString current() const { return currentName; }

    bool start(const fs::path& zip, const fs::path& into, const CacheLocation& location, const std::string& password,
               std::string& error);
    void cancel();

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    /// "ok", "error", "wrongPassword", "cancelled", "folder", "files", "skipped" (list)
    void finished(const QVariantMap& result);

private:
    struct State;
    std::shared_ptr<State> state;
    int doneCount = 0, totalCount = 0;
    QString currentName;
};

}  // namespace xqt
