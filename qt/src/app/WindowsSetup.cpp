#include "WindowsSetup.h"

#include <clocale>
#include <cstdlib>
#include <iostream>
#include <string>

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "EmojiFont.h"
#include "WindowsFonts.h"
#include "session/AppContext.h"

namespace xqt::windows {

namespace {
void setIfUnset(const wchar_t* name, const QString& value) {
    if (value.isEmpty() || GetEnvironmentVariableW(name, nullptr, 0) > 0) {
        return;
    }
    // _wputenv: the process environment, which GLib reads (g_getenv is GetEnvironmentVariableW on Windows), and the
    // C library's copy, which getenv() reads.
    const QString native = QDir::toNativeSeparators(value);
    _wputenv((std::wstring(name) + L"=" + native.toStdWString()).c_str());
}
}  // namespace

void prepareEnvironment() {
    // UCRT (Windows 10 1803 and newer) knows UTF-8 as a C locale character set. XQT_NO_UTF8_LOCALE=1 leaves the
    // C library's default (a diagnostic, see qt/scripts/windows-smoke.sh).
    if (qEnvironmentVariableIsSet("XQT_NO_UTF8_LOCALE")) {
        std::cerr << "[xournal-qt] XQT_NO_UTF8_LOCALE: keeping the C library's character set.\n";
    } else if (!std::setlocale(LC_CTYPE, ".UTF-8")) {
        std::cerr << "[xournal-qt] No UTF-8 C locale: file names with non-ASCII characters may not open.\n";
    }
    setIfUnset(L"XDG_CONFIG_HOME", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    setIfUnset(L"XDG_DATA_HOME", QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    setIfUnset(L"XDG_CACHE_HOME", QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
    // Text: Pango's fontconfig backend (its Windows one dies drawing into images, see WindowsFonts.h), with the font
    // cache built in the background while the window comes up.
    // (with the app's emoji font, EmojiFont.h)
    const std::filesystem::path fonts = AppContext::defaultResourceDir() / "fonts";
    if (useFontconfig(fonts, [](bool scaleBitmaps) { return emoji::fontconfigRules(scaleBitmaps); })) {
        warmUpFonts();
    }
}

}  // namespace xqt::windows
