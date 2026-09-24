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
/// What QFile opens for a URL: the path of a local file, else the URL itself.
QString sourceOf(const QUrl& url);
/// A file name made safe from a name another app gives (no folders, not hidden, not empty).
std::string safeName(const QString& name);
/// Copies a file or a folder (with its subfolders; hidden entries stay behind) that Qt can read into the folder
/// `into`, under its own name (a file whose name has no extension gets one from its content: .pdf, .xopp, .png, …).
/// Returns the copy; empty with `error` set if nothing could be read.
fs::path copyInto(const QString& source, const fs::path& into, std::string& error);

}  // namespace xqt::ContentFiles
