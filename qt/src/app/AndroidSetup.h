/*
 * xournal-qt: what the app needs on Android before the core starts (see qt/docs/android.md).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

namespace xqt::android {

/// Call once, right after the QApplication exists and before anything uses GLib, fontconfig or the resources:
/// - XDG_CONFIG_HOME, XDG_CACHE_HOME, XDG_DATA_HOME, XDG_STATE_HOME, HOME and TMPDIR point into the app's own
///   folders (GLib's g_get_user_*_dir and upstream's PathUtil read them);
/// - the resources (page templates, palettes, icons), which are Qt resources in the APK, are copied to
///   <app data>/share/xournal-qt, and XQT_RESOURCE_DIR points there (the core reads them as plain files);
/// - fontconfig gets a fonts.conf of its own (FONTCONFIG_FILE) with Android's /system/fonts, so that Pango finds
///   fonts, and a cache folder in the app's cache.
void prepareEnvironment();

/// Symbols the UI font has no glyph for (✓ ✎ ☐ ● arrows) come from Android's symbol font. After the QApplication.
void addSymbolFallback();

}  // namespace xqt::android
