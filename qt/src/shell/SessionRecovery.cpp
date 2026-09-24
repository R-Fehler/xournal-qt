#include "SessionRecovery.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "session/DocumentSession.h"
#include "util/PathUtil.h"
#include "util/Util.h"

#include "TabManager.h"

#ifdef _WIN32
// Last: <windows.h> defines macros (min, max, ERROR, ...) that the headers above must not see.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xqt {

namespace {
// Sessions protected by the crash handler. Fixed size and lock-free, as it is read from a signal handler.
std::array<std::atomic<const DocumentSession*>, 256> registry{};

QDateTime modificationTime(const fs::path& p) {
    return QFileInfo(QString::fromStdString(p.string())).lastModified();
}

bool fileExists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec);
}

/// Saves the unsaved documents once, whichever handler comes first (on Windows a crash can reach both the
/// exception filter and a signal handler).
void emergencySaveOnce(const char* what, long code) {
    static std::atomic<int> entered{0};
    if (entered++ == 0) {
        std::cerr << "\n[xournal-qt] " << what << " " << code << ": saving unsaved documents for recovery...\n";
        const int n = SessionRecovery::emergencySaveAll();
        std::cerr << "[xournal-qt] " << n << " document(s) saved to " << Util::getAutosaveFilepath().parent_path()
                  << "; they are offered for recovery at the next start.\n";
    }
}

extern "C" void xqtFatalSignal(int sig) {
    emergencySaveOnce("Fatal signal", sig);
    // Default handling (core dump for crashes, exit for SIGTERM/SIGINT).
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#ifdef _WIN32
// Windows reports crashes (access violations, stack overflows, ...) as structured exceptions. MinGW's runtime turns
// some of them into signals through its own filter; this one comes first and hands on to it afterwards.
LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;

LONG WINAPI xqtUnhandledException(EXCEPTION_POINTERS* info) {
    emergencySaveOnce("Fatal exception",
                      info && info->ExceptionRecord ? static_cast<long>(info->ExceptionRecord->ExceptionCode) : 0);
    return previousExceptionFilter ? previousExceptionFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}
#endif
}  // namespace

SessionRecovery::SessionRecovery(TabManager& tabs, fs::path journalFile, QObject* parent):
        QObject(parent), tabs(tabs), journalFile(std::move(journalFile)) {
    previousJournal = readJournal(this->journalFile);
    writeTimer.setSingleShot(true);
    writeTimer.setInterval(500);
    connect(&writeTimer, &QTimer::timeout, this, &SessionRecovery::writeNow);
}

SessionRecovery::~SessionRecovery() {
    for (int i = 0; i < tabs.count(); ++i) {
        unregisterSession(tabs.session(i));
    }
    for (TabManager* other: otherWindows) {
        for (int i = 0; i < other->count(); ++i) {
            unregisterSession(other->session(i));
        }
    }
}

// A document that moved to a window of its own is still part of this session: it keeps its place in the journal and
// its unsaved changes are written when the program crashes.
void SessionRecovery::watch(TabManager& other) {
    if (std::find(otherWindows.begin(), otherWindows.end(), &other) != otherWindows.end()) {
        return;
    }
    otherWindows.push_back(&other);
    connect(&other, &QObject::destroyed, this, [this, &other] {
        otherWindows.erase(std::remove(otherWindows.begin(), otherWindows.end(), &other), otherWindows.end());
        writeNow();
    });
    if (!running) {
        return;
    }
    for (int i = 0; i < other.count(); ++i) {
        registerSession(other.session(i));
    }
    connect(&other, &QAbstractItemModel::rowsInserted, this, [this, &other](const QModelIndex&, int first, int last) {
        for (int i = first; i <= last; ++i) {
            registerSession(other.session(i));
        }
        writeNow();
    });
    connect(&other, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this, &other](const QModelIndex&, int first, int last) {
                for (int i = first; i <= last; ++i) {
                    unregisterSession(other.session(i));
                }
            });
    connect(&other, &QAbstractItemModel::rowsRemoved, this, &SessionRecovery::writeNow);
    connect(&other, &QAbstractItemModel::dataChanged, this, &SessionRecovery::scheduleWrite);
    writeNow();
}

fs::path SessionRecovery::defaultJournalFile() { return Util::getConfigFile("session.json"); }

bool SessionRecovery::previousCrashed() const {
    return previousJournal && !previousJournal->clean && !processAlive(previousJournal->pid);
}

std::vector<SessionRecovery::Candidate> SessionRecovery::candidates() const {
    return previousCrashed() ? findCandidates(*previousJournal) : std::vector<Candidate>{};
}

void SessionRecovery::start() {
    if (running) {
        return;
    }
    running = true;
    for (int i = 0; i < tabs.count(); ++i) {
        registerSession(tabs.session(i));
    }
    // Which tabs and files are open is written at once; the current page of a tab a moment later.
    connect(&tabs, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first, int last) {
        for (int i = first; i <= last; ++i) {
            registerSession(tabs.session(i));
        }
        writeNow();
    });
    connect(&tabs, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex&, int first, int last) {
        for (int i = first; i <= last; ++i) {
            unregisterSession(tabs.session(i));
        }
    });
    connect(&tabs, &QAbstractItemModel::rowsRemoved, this, &SessionRecovery::writeNow);
    connect(&tabs, &QAbstractItemModel::rowsMoved, this, &SessionRecovery::writeNow);
    connect(&tabs, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                if (roles.contains(TabManager::FilePathRole)) {
                    writeNow();
                } else {
                    scheduleWrite();
                }
            });
    connect(&tabs, &TabManager::currentIndexChanged, this, &SessionRecovery::scheduleWrite);
    writeNow();
}

void SessionRecovery::finish() {
    if (!running) {
        return;
    }
    writeTimer.stop();
    Journal j = currentJournal();
    j.clean = true;
    writeJournal(j, journalFile);
    running = false;
}

void SessionRecovery::scheduleWrite() {
    if (running) {
        writeTimer.start();
    }
}

void SessionRecovery::writeNow() {
    if (running) {
        writeTimer.stop();
        writeJournal(currentJournal(), journalFile);
    }
}

SessionRecovery::Journal SessionRecovery::currentJournal() const {
    Journal j;
    j.pid = Util::getPid();
    j.current = std::max(0, tabs.currentIndex());
    auto add = [&j](const TabManager& list) {
        for (int i = 0; i < list.count(); ++i) {
            const DocumentSession* s = list.session(i);
            const bool text = s->textFile() && !s->hasFilePath();  // (a text file: reopened as itself)
            j.tabs.push_back({s->hasFilePath() ? s->getFilePath() : (text ? s->shownFile() : fs::path()), j.pid,
                              s->serial(), static_cast<int>(s->getCurrentPageNo()), text});
        }
    };
    add(tabs);
    for (const TabManager* other: otherWindows) {  // the documents in windows of their own (reopened as tabs)
        add(*other);
    }
    return j;
}

bool SessionRecovery::writeJournal(const Journal& journal, const fs::path& file) {
    QJsonArray tabs;
    for (const auto& t: journal.tabs) {
        tabs.append(QJsonObject{{"file", QString::fromStdString(t.file.string())},
                                {"pid", t.pid},
                                {"serial", static_cast<qint64>(t.serial)},
                                {"page", t.page},
                                {"text", t.text}});
    }
    const QJsonObject root{
            {"pid", journal.pid}, {"clean", journal.clean}, {"current", journal.current}, {"tabs", tabs}};
    QSaveFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return f.commit();
}

std::optional<SessionRecovery::Journal> SessionRecovery::readJournal(const fs::path& file) {
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    Journal j;
    j.pid = root.value("pid").toInteger();
    j.clean = root.value("clean").toBool();
    j.current = root.value("current").toInt();
    for (const QJsonValue& v: root.value("tabs").toArray()) {
        const QJsonObject t = v.toObject();
        j.tabs.push_back({fs::path(t.value("file").toString().toStdString()), t.value("pid").toInteger(),
                          static_cast<quint64>(t.value("serial").toInteger()), t.value("page").toInt(),
                          t.value("text").toBool()});
    }
    return j;
}

bool SessionRecovery::processAlive(qint64 pid) {
    if (pid <= 0) {
        return false;
    }
    if (pid == Util::getPid()) {
        return true;
    }
    // Same program? (pids are reused)
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return false;
    }
    DWORD exitCode = 0;
    std::wstring image(32768, L'\0');  // (the longest path Windows has)
    auto size = static_cast<DWORD>(image.size());
    const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE &&
                         QueryFullProcessImageNameW(process, 0, image.data(), &size);
    CloseHandle(process);
    if (!running) {
        return false;
    }
    const std::string name =
            QFileInfo(QString::fromWCharArray(image.data(), static_cast<qsizetype>(size))).fileName().toStdString();
#else
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(comm, name);
#endif
    return name.rfind("xournal-qt", 0) == 0 || name.rfind("xqt-", 0) == 0;
}

std::vector<SessionRecovery::Candidate> SessionRecovery::findCandidates(const Journal& journal) {
    std::vector<Candidate> result;
    for (size_t i = 0; i < journal.tabs.size(); ++i) {
        const TabRecord& t = journal.tabs[i];
        const fs::path autosave = t.text           ? DocumentSession::textAutosavePath(t.pid, t.serial)
                                  : t.file.empty() ? DocumentSession::unnamedAutosavePath(t.pid, t.serial)
                                                   : DocumentSession::namedAutosavePath(t.file);
        const fs::path emergency = t.text ? DocumentSession::textEmergencyPath(t.pid, t.serial)
                                          : DocumentSession::emergencyPath(t.pid, t.serial);
        const QDateTime saved = fileExists(t.file) ? modificationTime(t.file) : QDateTime();
        Candidate best;
        for (const fs::path& f: {emergency, autosave}) {
            if (!fileExists(f)) {
                continue;
            }
            const QDateTime time = modificationTime(f);
            if ((!saved.isValid() || time > saved) && (!best.time.isValid() || time > best.time)) {
                best = {i, f, t.file, time};
            }
        }
        if (best.time.isValid()) {
            result.push_back(std::move(best));
        }
    }
    return result;
}

void SessionRecovery::installCrashHandlers() {
    for (int sig: {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGTERM, SIGINT}) {
        std::signal(sig, xqtFatalSignal);
    }
#ifdef SIGBUS  // (not on Windows)
    std::signal(SIGBUS, xqtFatalSignal);
#endif
#ifdef _WIN32
    previousExceptionFilter = SetUnhandledExceptionFilter(xqtUnhandledException);
#endif
}

int SessionRecovery::emergencySaveAll() {
    int saved = 0;
    for (auto& slot: registry) {
        const DocumentSession* s = slot.load();
        if (!s || !s->isModified()) {
            continue;
        }
        if (s->textFile() && !s->hasFilePath()) {
            // A text file: its text (no locking, as below)
            const std::string text = s->currentText(false);
            std::ofstream out(DocumentSession::textEmergencyPath(Util::getPid(), s->serial()), std::ios::binary);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            saved += out.good() ? 1 : 0;
            continue;
        }
        const fs::path target = DocumentSession::emergencyPath(Util::getPid(), s->serial());
        // Like upstream's emergencySave: no locking (the crashed thread may hold the document lock).
        SaveHandler handler;
        handler.prepareSave(s->getDocument(), target);
        handler.saveTo(target);
        if (handler.getErrorMessage().empty()) {
            ++saved;
        }
    }
    return saved;
}

void SessionRecovery::registerSession(const DocumentSession* session) {
    if (!session) {
        return;
    }
    for (auto& slot: registry) {
        const DocumentSession* expected = nullptr;
        if (slot.load() == session || slot.compare_exchange_strong(expected, session)) {
            return;
        }
    }
}

void SessionRecovery::unregisterSession(const DocumentSession* session) {
    for (auto& slot: registry) {
        const DocumentSession* expected = session;
        slot.compare_exchange_strong(expected, nullptr);
    }
}

}  // namespace xqt
