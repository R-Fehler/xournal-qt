/*
 * xournal-qt: what the app hands to the system: a file to the app the system opens it with, a file to show in the
 * file manager, a library to open in a window of its own (another process), a file for the trash.
 *
 * Everything goes through one object that tests replace (setInstance), so no test starts an app, a file manager or
 * a window.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>

namespace xqt {

class SystemApps {
public:
    virtual ~SystemApps() = default;

    /// Open a file with the app the system has for it (QDesktopServices::openUrl: xdg-open on Linux, `open` on
    /// macOS, ShellExecute on Windows, an intent on Android).
    virtual bool openWithSystemApp(const QString& path);
    /// Show a file in the file manager, selected: org.freedesktop.FileManager1.ShowItems over D-Bus on Linux (the
    /// folder itself when no file manager answers there), `explorer /select,` on Windows, `open -R` on macOS. A
    /// folder is opened. Not on Android (false).
    virtual bool showInFileManager(const QString& path);
    /// Open a folder as a library in a window of its own: the app started again with the folder (one library per
    /// process; a running window of that library takes over and comes to the front).
    virtual bool startLibraryWindow(const QString& folder);
    /// Move a file or folder to the desktop trash (QFile::moveToTrash). Every trash of the app goes through here,
    /// so tests never fill the user's trash.
    virtual bool moveToTrash(const QString& path);

    /// There is a file manager to show files in (not on Android).
    static bool canShowInFileManager();

    /// The one in use.
    static SystemApps& instance();
    /// Use another one (tests: a fake); nullptr: the real one again. The caller keeps ownership.
    static void setInstance(SystemApps* apps);
};

}  // namespace xqt
