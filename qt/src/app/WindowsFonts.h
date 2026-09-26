/*
 * xournal-qt: the fonts of the core's text (Pango) on Windows, for the app and the CLI (Qt-free). See
 * qt/docs/windows.md, "Text and fonts".
 *
 * Pango's default font backend on Windows (win32, DirectWrite) kills the process when text is drawn into an image
 * surface, as the page rasters, thumbnails and PNG exports do (found in the CI smoke test, 2026-09-24; the process
 * ends with a fatal NTSTATUS, no message). Pango's fontconfig backend, with FreeType, works; Linux uses it too.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace xqt::windows {

/// Makes Pango use its fontconfig backend (PANGOCAIRO_BACKEND=fc) with a configuration of our own, written at every
/// start to <cache>/xournal-qt/fontconfig/fonts.conf (FONTCONFIG_FILE), <cache> being XDG_CACHE_HOME or
/// %LOCALAPPDATA%\cache:
/// - the fonts of Windows and the user's own (%LOCALAPPDATA%\Microsoft\Windows\Fonts);
/// - fontconfig's cache in that folder;
/// - Sans, Serif and Monospace as Pango's Windows backend and upstream Xournal++ on Windows have them (Arial, Times
///   New Roman, Courier New), so that text boxes keep their size;
/// - the rules of the program folder's etc/fonts/conf.d;
/// - `appFonts`, if given: a folder of fonts that come with the app (the emoji font, EmojiFont.h), and after conf.d
///   the rules `appRules` returns (its argument: conf.d lacks fontconfig's rule for scaling bitmap fonts).
/// Call before anything uses Pango, after the C locale and XDG_CACHE_HOME are set. Nothing happens when
/// PANGOCAIRO_BACKEND or FONTCONFIG_FILE is set already, or with XQT_WIN_PANGO_WIN32=1 (Pango's own default, to try
/// the win32 backend again). Returns whether it set things up.
bool useFontconfig(const std::filesystem::path& appFonts = {},
                   const std::function<std::string(bool scaleBitmaps)>& appRules = {});

/// Loads fontconfig's configuration and fonts on a background thread. The first start builds the font cache (a scan
/// of C:\Windows\Fonts, a few seconds), which would otherwise hold up the first page with text.
void warmUpFonts();

}  // namespace xqt::windows
