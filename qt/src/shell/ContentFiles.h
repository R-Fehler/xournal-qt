/*
 * xournal-qt: files of other apps that are not paths (Android's content:// URIs from "Open with", the share sheet
 * and the system's file pickers) copied into folders of our own, so that the rest of the app only sees paths.
 *
 * Qt reads such URIs with QFile / QFileInfo / QDirIterator (its Android file engine: a document, or a document tree
 * picked as a folder); the same code works with plain paths, which is how it is tested on the desktop.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

#include <QString>
#include <QUrl>

#include "filesystem.h"

namespace xqt::ContentFiles {

/// A URL that is no local file but can be read through Qt (Android: content://).
bool isForeign(const QUrl& url);

/// Where Android's shared storage is (StorageRoots{} on a phone; tests use folders of their own).
struct StorageRoots {
    QString primary = QStringLiteral("/storage/emulated/0");  ///< the internal shared storage ("primary")
    QString volumes = QStringLiteral("/storage");             ///< SD cards and USB drives: <volumes>/<volume id>
};
/// The real path of a folder (or file) picked through Android's system picker, if it is in the shared storage: a
/// tree or document URI of the external storage provider ("content://com.android.externalstorage.documents/tree/
/// primary%3ADocuments%2FUni" is "/storage/emulated/0/Documents/Uni"; "home:" is the Documents folder, "<volume
/// id>:" an SD card) or of the Downloads provider ("downloads", "raw:<path>"). Empty for everything else (a cloud
/// app's provider, a URI that is no folder of these) and when that path does not exist. Only useful with "All files
/// access" (the app can read the path then; SystemApps::hasAllFilesAccess).
QString sharedStoragePath(const QUrl& url, const StorageRoots& roots = {});
/// What QFile opens for a URL: the path of a local file, else the URL itself.
QString sourceOf(const QUrl& url);
/// A file name made safe from a name another app gives (no folders, not hidden, not empty).
std::string safeName(const QString& name);
/// Copies a file or a folder (with its subfolders; hidden entries stay behind) that Qt can read into the folder
/// `into`, under its own name (a file whose name has no extension gets one from its content: .pdf, .xopp, .png, …).
/// Returns the copy; empty with `error` set if nothing could be read.
fs::path copyInto(const QString& source, const fs::path& into, std::string& error);

}  // namespace xqt::ContentFiles
