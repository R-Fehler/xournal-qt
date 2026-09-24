/*
 * xournal-qt: what the app needs on Windows before the core starts (see qt/docs/windows.md).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

namespace xqt::windows {

/// Call once, right after the QApplication exists and before anything uses std::filesystem, GLib or the resources:
/// - the C library's character set becomes UTF-8 (LC_CTYPE ".UTF-8"). libstdc++'s std::filesystem converts narrow
///   strings with it, and the app hands it UTF-8 everywhere (QString::toStdString, GLib, the core). Without this a
///   path with an umlaut would be read in the ANSI code page and point to another file;
/// - XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_CACHE_HOME, which GLib reads on Windows too, point to Qt's generic
///   folders (%LOCALAPPDATA%, and %LOCALAPPDATA%\cache for the caches: GLib's own default for the cache is the
///   Internet Explorer cache folder). Variables that are already set are left alone;
/// - Pango draws text with its fontconfig backend and a configuration of the app's own (WindowsFonts.h), and the
///   font cache is loaded, or built on the first start, on a background thread.
void prepareEnvironment();

}  // namespace xqt::windows
