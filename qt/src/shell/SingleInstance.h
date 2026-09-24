/*
 * xournal-qt: one application instance per user session.
 *
 * The first instance listens on a local socket. A second instance (e.g. started by opening a file in the file
 * manager) hands its files over and exits, so the documents open as tabs in the existing window.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;

namespace xqt {

class SingleInstance final: public QObject {
    Q_OBJECT
public:
    /// @param key socket name (default: per user), tests use their own
    explicit SingleInstance(QString key = {}, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// Who runs this process, for socket names that are per user: the uid on Unix, the user name on Windows (local
    /// sockets are named pipes there, which all users of the machine share).
    static QString userId();

    /// Try to hand the files to a running instance. Returns true if one received them (this process should exit).
    bool sendToRunningInstance(const QStringList& absolutePaths, int timeoutMs = 1000);
    /// Become the primary instance: listen for other instances.
    bool listen();

Q_SIGNALS:
    /// Another instance asked to open these files (may be empty: just show the window).
    void filesRequested(const QStringList& paths);

private:
    QString key;
    QLocalServer* server = nullptr;
};

}  // namespace xqt
