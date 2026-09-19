/*
 * xournal-qt: crash recovery and session restore for the tabs of the window.
 *
 * - A journal (session.json in the config folder) always lists the open tabs: file, current page, and the process
 *   id + session serial that name the tab's autosave and emergency files. It is marked "clean" when the app quits
 *   normally.
 * - On a fatal signal (crash, SIGTERM, SIGINT) every modified document is written to its emergency file, like
 *   upstream's CrashHandler does for its single document.
 * - On the next start: after a clean exit the last tabs are reopened (setting); after a crash, tabs with a newer
 *   emergency or autosave file than the document are offered for recovery.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include "filesystem.h"

namespace xqt {

class DocumentSession;
class TabManager;

class SessionRecovery final: public QObject {
    Q_OBJECT
public:
    struct TabRecord {
        fs::path file;  ///< empty: never saved
        qint64 pid = 0;
        quint64 serial = 0;
        int page = 0;
    };
    struct Journal {
        qint64 pid = 0;
        bool clean = false;
        int current = 0;
        std::vector<TabRecord> tabs;
    };
    /// A tab of a crashed session with changes that are newer than its file.
    struct Candidate {
        size_t tab = 0;          ///< index in Journal::tabs
        fs::path recoveryFile;   ///< emergency or autosave file
        fs::path originalFile;   ///< the document's own file (empty: never saved)
        QDateTime time;
    };

    SessionRecovery(TabManager& tabs, fs::path journalFile, QObject* parent = nullptr);
    ~SessionRecovery() override;

    /// The journal of the previous run (read before this run writes its own), if any.
    const std::optional<Journal>& previous() const { return previousJournal; }
    /// The previous run ended without quitting normally (crash, killed, logout without closing).
    bool previousCrashed() const;
    /// Recovery candidates of the previous run (empty unless it crashed).
    std::vector<Candidate> candidates() const;

    /// Start journaling this run (and protect its documents with emergency saves).
    void start();
    /// Normal exit: the journal is marked clean (the tabs are reopened next time).
    void finish();
    /// Write the journal now (normally done shortly after each change).
    void writeNow();

    static fs::path defaultJournalFile();
    static bool writeJournal(const Journal& journal, const fs::path& file);
    static std::optional<Journal> readJournal(const fs::path& file);
    /// Whether a process with this id runs this program.
    static bool processAlive(qint64 pid);
    static std::vector<Candidate> findCandidates(const Journal& journal);

    /// Install handlers for fatal signals that emergency-save all registered sessions.
    static void installCrashHandlers();
    /// Emergency-save all registered, modified sessions now (also used by the crash handler). Returns the count.
    static int emergencySaveAll();
    static void registerSession(const DocumentSession* session);
    static void unregisterSession(const DocumentSession* session);

private:
    Journal currentJournal() const;
    void scheduleWrite();

    TabManager& tabs;
    fs::path journalFile;
    std::optional<Journal> previousJournal;
    QTimer writeTimer;
    bool running = false;
};

}  // namespace xqt
