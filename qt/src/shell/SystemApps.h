/*
 * xournal-qt: what the app hands to the system: a file to the app the system opens it with, a file to show in the
 * file manager, a library to open in a window of its own (another process), a file for the trash, files to share
 * (the file manager on the desktop; the share sheet on Android and iOS later) or to paste elsewhere (the clipboard);
 * and what it asks the system for: Android's "All files access" for libraries in the shared storage.
 *
 * Everything goes through one object that tests replace (setInstance), so no test starts an app, a file manager or
 * a window.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace xqt {

class SystemApps {
public:
    virtual ~SystemApps() = default;

    /// Open a file with the app the system has for it (QDesktopServices::openUrl: xdg-open on Linux, `open` on
    /// macOS, ShellExecute on Windows, an intent on Android).
    virtual bool openWithSystemApp(const QString& path);
    /// Open a web address (http, https) in the browser (QDesktopServices::openUrl). The caller has shown it first
    /// (qt/docs/citations.md).
    virtual bool openWebAddress(const QUrl& url);
    /// Show a file in the file manager, selected: org.freedesktop.FileManager1.ShowItems over D-Bus on Linux (the
    /// folder itself when no file manager answers there), `explorer /select,` on Windows, `open -R` on macOS. A
    /// folder is opened. Not on Android (false).
    virtual bool showInFileManager(const QString& path);
    /// Open a folder as a library in a window of its own: the app started again with the folder (one library per
    /// process; a running window of that library takes over and comes to the front).
    virtual bool startLibraryWindow(const QString& folder);
    /// Hand files to the user to send them on: on the desktop the file manager shows them, selected (the share sheet
    /// on Android and iOS, later: false there for now).
    virtual bool share(const QStringList& files);
    /// Put files on the clipboard to paste them into another app: their URLs (text/uri-list, also as GNOME's
    /// x-special/gnome-copied-files), a single PDF of at most `maxPdfBytes` also as application/pdf data, and their
    /// paths as text.
    virtual bool copyToClipboard(const QStringList& files);
    static constexpr qint64 maxPdfBytes = 50 * 1024 * 1024;
    /// Move a file or folder to the desktop trash (QFile::moveToTrash). Every trash of the app goes through here,
    /// so tests never fill the user's trash.
    virtual bool moveToTrash(const QString& path);

    /// Each library opens in a window of its own (another process: the desktop). On Android there is one window,
    /// and it switches to the other library (AppController::switchLibrary).
    virtual bool librariesInOwnWindows();
    /// A folder the app can read and write only with "All files access" (Android's MANAGE_EXTERNAL_STORAGE): one in
    /// the shared storage, outside the app's own folders. Never on the desktop.
    virtual bool needsAllFilesAccess(const QString& folder);
    /// The app has "All files access" (Android 11 and newer: Environment.isExternalStorageManager(); before: the
    /// storage permission). Always on the desktop.
    virtual bool hasAllFilesAccess();
    /// Ask for it: Android shows the system's settings page for it (the app goes to the background and comes back
    /// when the user returns). False if there is nothing to ask (the desktop) or it could not be shown.
    virtual bool requestAllFilesAccess();

    /// There is a file manager to show files in (not on Android).
    static bool canShowInFileManager();
    /// There is a trash (QFile::moveToTrash; not on Android, where the app deletes after asking).
    static bool canTrash();
    /// share() works here (not on Android yet).
    static bool canShare();

    /// The one in use.
    static SystemApps& instance();
    /// Use another one (tests: a fake); nullptr: the real one again. The caller keeps ownership.
    static void setInstance(SystemApps* apps);
};

}  // namespace xqt
