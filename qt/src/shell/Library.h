/*
 * xournal-qt: a library, the folder of documents a window works in (like a workspace).
 *
 * A library is a plain folder with PDFs and .xopp files, in subfolders if wanted. By default it is
 * "<Documents>/Xournal_Libraries/Default"; any folder can be opened as one. Each process shows one library.
 * Metadata that only speeds things up (the documents' covers, the search index: LibraryIndex.h) is kept per
 * folder, in its hidden folder ".xournal_library" or in the app cache (see LibraryCache.h); it can be deleted at any
 * time.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <QString>

#include "filesystem.h"
#include "DocumentFiles.h"
#include "LibraryCache.h"

class QJsonObject;

namespace xqt {

/// The folders of the platform the libraries' home and the Downloads folder can be in (see qt/docs/android.md,
/// "Where the documents are"). Tests set their own (Library::setPlatformFolders).
struct PlatformFolders {
    /// The Documents folder the app may always use: "~/Documents" on the desktop; on Android the app's own folder in
    /// the shared storage (Android/data/<package>/files/Documents), which Android deletes with the app.
    fs::path appDocuments;
    /// The same for downloads ("~/Downloads"; on Android the app's own, where no download ever lands).
    fs::path appDownloads;
    /// Android: the phone's own Documents and Download folders (Environment.getExternalStoragePublicDirectory), and
    /// the storage they are in (Environment.getExternalStorageDirectory: where the in-app folder chooser starts). The
    /// app reads and writes them with "All files access". Empty on the desktop.
    fs::path sharedDocuments;
    fs::path sharedDownloads;
    fs::path sharedStorage;

    /// This platform's (QStandardPaths; on Android also android.os.Environment through JNI).
    static PlatformFolders detect();
};

class Library {
public:
    explicit Library(const fs::path& root);

    /// Where the libraries live: in the Documents folder the app may always use, or in the phone's shared Documents
    /// folder (Android with "All files access", once the libraries were moved there: LibraryMigration.h).
    enum class Home { App, Shared };
    /// The folders of this platform; tests set their own (nullptr: this platform's again).
    static PlatformFolders platformFolders();
    static void setPlatformFolders(const PlatformFolders* folders);
    /// The home in use (App until the app says otherwise: AppController::chooseLibrariesHome). Shared only where the
    /// platform has shared folders.
    static Home home();
    static void setHome(Home home);
    /// The home to use: Shared when the platform has a shared Documents folder, the app may use it (`access`) and the
    /// libraries were moved there (`moved`, the setting kept since); else App.
    static Home chooseHome(const PlatformFolders& folders, bool access, bool moved);

    /// "<Documents>/Xournal_Libraries" of the home in use, or of the one given
    static fs::path librariesFolder();
    static fs::path librariesFolder(Home home);
    static fs::path defaultRoot();
    /// The user's Downloads folder: can be opened as a (quick) library, but its files are short-lived. On Android the
    /// phone's Download folder (a folder of the shared storage: opening it needs "All files access").
    static fs::path downloadsFolder();

    const fs::path& root() const { return rootDir; }
    QString name() const;
    bool isDefault() const;
    /// In the standard folder of libraries ("<Documents>/Xournal_Libraries", any depth).
    bool isInLibrariesFolder() const;
    /// The Downloads folder or a folder in it: files there are often cleaned up (the UI warns before importing).
    bool isTemporary() const;
    /// Short hash of the root (one instance and one session journal per library).
    std::string key() const;
    /// The library's own state in the config folder, "~/.config/xournal-qt/libraries/<key>/" (created when
    /// needed): what is not a cache and must survive cleaning it, e.g. the reading positions.
    fs::path configDir() const;
    /// Where the library keeps its cache: in its folders or in the app's cache folder (for folders that sync clients
    /// upload). A setting of the library, kept in its config folder ("library.json", read once by this Library and
    /// its copies, which also keep what they write there); without one, defaultCacheMode().
    CacheLocation::Mode cacheMode() const;
    void setCacheMode(CacheLocation::Mode mode) const;
    /// The library has a cache setting of its own (else it follows defaultCacheMode()).
    bool hasCacheSetting() const;
    /// Where a library without a setting keeps its cache: in its folders on the desktop; in the app cache on Android,
    /// where libraries are often folders that sync apps (Syncthing, FolderSync, ...) upload.
    static CacheLocation::Mode defaultCacheMode();
    /// (tests) Pretend another platform: the default from now on.
    static void setDefaultCacheMode(CacheLocation::Mode mode);
    CacheLocation cacheLocation() const { return CacheLocation(rootDir, cacheMode()); }
    /// Which kinds of files the library shows ("Show" in the library; the defaults when never set). Kept in its config
    /// folder, next to the cache mode.
    ShowFilter showFilter() const;
    void setShowFilter(const ShowFilter& filter) const;
    /// The reading positions (DocumentPlaces) of its documents: "pages.json" in its config folder.
    fs::path placesFile() const;
    /// The file or folder is in the library.
    bool contains(const fs::path& p) const;
    /// Path relative to the root ("" for the root itself).
    std::string relative(const fs::path& p) const;

private:
    struct SettingsCache;
    /// Its "library.json" (read once)
    QJsonObject settings() const;
    /// Change it (the other settings stay as they are), on disk and in memory
    void changeSettings(const std::function<void(QJsonObject&)>& change) const;

    fs::path rootDir;
    std::shared_ptr<SettingsCache> cache;  ///< (shared by copies: the same library)
};

}  // namespace xqt
