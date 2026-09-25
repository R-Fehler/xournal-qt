/*
 * xournal-qt: moving all libraries to another home. On Android: from the app's own folder, which Android deletes
 * with the app, to the phone's Documents/Xournal_Libraries (Library::Home; qt/docs/android.md, "Where the documents
 * are"). The user's documents are at stake, so the old place stays whole and in use until the new one is known to be
 * complete:
 *
 *  1. plan: each library (a folder of the old home; a loose file there too) gets its place in the new home. A name
 *     that is taken there becomes "Name (2)": libraries are never merged. An empty library (no file in it) whose
 *     name is taken has nothing to move and is only removed at the end.
 *  2. copy: every file, hidden ones too, into a hidden staging folder "<new home>/.xqt-moving-<name>", with its
 *     modification time; the hash of each file (SHA-1) is taken while it is read.
 *  3. verify: each copy is read back and must have the size and the hash of what was read; the originals must be
 *     unchanged since (size, time).
 *  4. switch (the caller, on the UI thread): commit() gives the staging folders their names, the manifest is written,
 *     the app uses the new home, and everything that stores paths follows (relocateState(), and the caller's own:
 *     recent files, open tabs, settings).
 *  5. clean up: an old file is deleted only when it is still the file that was copied and verified (the size and
 *     time of the manifest); a file changed since stays where it is. Then its empty folders go. If the app is ended
 *     before, the manifest is still there and the next start does it.
 *
 * Every failure before 4 removes the staging folders and leaves the old home as it was.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QObject>
#include <QString>

#include "filesystem.h"

namespace xqt::LibraryMigration {

/// A file copied: its path in the library, and what was read and verified.
struct FileRecord {
    std::string rel;  ///< relative to the library (the file itself for a loose file: "")
    qint64 size = 0;
    qint64 mtimeNs = 0;  ///< the original's modification time (ns since the epoch of the file clock)
    QByteArray hash;
};

struct Move {
    fs::path from;     ///< the library (or loose file) in the old home
    fs::path to;       ///< its place in the new home
    fs::path staging;  ///< where it is copied first
    bool folder = true;
    bool renamed = false;             ///< `to` has another name than `from` (the name was taken)
    std::vector<std::string> dirs;    ///< its folders (relative), empty ones too
    std::vector<FileRecord> files;    ///< its files, as copied (filled by copyAndVerify)
};

struct Plan {
    fs::path fromHome, toHome;
    std::vector<Move> moves;
    /// Empty libraries whose name is taken in the new home: nothing to move, removed at the clean-up
    std::vector<fs::path> dropped;
    qint64 bytes = 0;
    int files = 0;
    /// Each move as (old path, new path)
    std::vector<std::pair<fs::path, fs::path>> pairs() const;
};

/// There is a file somewhere in `home` (empty folders do not count; nor do staging folders of a move).
bool hasContent(const fs::path& home);

/// Step 1. `error` if the homes are the same or one is inside the other, or the old one cannot be read.
Plan plan(const fs::path& fromHome, const fs::path& toHome, std::string& error);

struct Progress {
    enum class Step { Copy, Verify };
    Step step = Step::Copy;
    qint64 bytes = 0, totalBytes = 0;
    int files = 0, totalFiles = 0;
};
/// Steps 2 and 3, on this thread. False with `error` (and the staging folders removed again); `cancel` is looked at
/// between files and chunks.
bool copyAndVerify(Plan& plan, const std::atomic<bool>& cancel, const std::function<void(const Progress&)>& progress,
                   std::string& error);
/// Step 4: the staging folders get their names. False (with what was renamed renamed back and the staging folders
/// removed) if one cannot.
bool commit(Plan& plan, std::string& error);
/// Remove the staging folders (after a failure or a cancel).
void discard(const Plan& plan);

/// The manifest of a switched move, kept until its clean-up is done (in the config folder).
fs::path manifestFile();
bool writeManifest(const Plan& plan, const fs::path& file);
std::optional<Plan> readManifest(const fs::path& file);

/// Step 4, what the shell keeps by path: each library's folder in the config (its settings and reading positions)
/// and in the app cache (previews, search index) go to the key of its new path; the reading positions of documents
/// outside the current library, and the tabs of the session journals given, follow. Safe to repeat.
void relocateState(const Plan& plan, const std::vector<fs::path>& journals);
/// The session journals there are (the one of the default library and those of the others).
std::vector<fs::path> journalFiles();

struct Cleanup {
    int removed = 0;
    std::vector<fs::path> kept;  ///< old files changed since they were copied (still in the old home)
};
/// Step 5, on this thread.
Cleanup cleanUp(const Plan& plan);

/// (tests) Called after each file was copied, with the original and its copy: may change the copy (a copy that
/// arrived damaged) or return false (the copy failed). Empty: none.
void setCopyHook(std::function<bool(const fs::path& original, const fs::path& copy)> hook);

}  // namespace xqt::LibraryMigration

namespace xqt {

/// Runs the copy / verify and the clean-up of a move on a worker, with progress for the UI.
class LibraryMove: public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    /// "copy", "verify", "clean" (or "" while not running)
    Q_PROPERTY(QString step READ step NOTIFY progressChanged)
    Q_PROPERTY(double fraction READ fraction NOTIFY progressChanged)
    Q_PROPERTY(int files READ files NOTIFY progressChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
public:
    explicit LibraryMove(QObject* parent = nullptr);
    ~LibraryMove() override;

    bool running() const { return state != nullptr; }
    QString step() const { return currentStep; }
    double fraction() const;
    int files() const { return filesDone; }
    int totalFiles() const { return filesTotal; }
    qint64 bytes() const { return bytesDone; }
    qint64 totalBytes() const { return bytesTotal; }

    /// Copy and verify `plan` on a worker; copied() tells how it went.
    void startCopy(LibraryMigration::Plan plan);
    /// Clean up after the switch on a worker; cleanedUp() tells how it went.
    void startCleanup(LibraryMigration::Plan plan);
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    /// The copy is complete and verified (`ok`: `plan` has the files), or failed / was cancelled (`error`; nothing of
    /// it is left in the new home).
    void copied(bool ok, const QString& error, const xqt::LibraryMigration::Plan& plan);
    void cleanedUp(const xqt::LibraryMigration::Cleanup& result);

private:
    struct State;
    std::shared_ptr<State> state;
    QString currentStep;
    int filesDone = 0, filesTotal = 0;
    qint64 bytesDone = 0, bytesTotal = 0;
    void finish();
};

}  // namespace xqt
