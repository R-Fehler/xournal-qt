#include "WindowsSetup.h"

#include <clocale>
#include <iostream>

#include <QDir>
#include <QStandardPaths>
#include <QString>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace xqt::windows {

namespace {
void setIfUnset(const wchar_t* name, const QString& value) {
    if (value.isEmpty() || GetEnvironmentVariableW(name, nullptr, 0) > 0) {
        return;
    }
    // The process environment, which GLib reads (g_getenv is GetEnvironmentVariableW on Windows).
    const QString native = QDir::toNativeSeparators(value);
    SetEnvironmentVariableW(name, reinterpret_cast<const wchar_t*>(native.utf16()));
}
}  // namespace

void prepareEnvironment() {
    // UCRT (Windows 10 1803 and newer) knows UTF-8 as a C locale character set.
    if (!std::setlocale(LC_CTYPE, ".UTF-8")) {
        std::cerr << "[xournal-qt] No UTF-8 C locale: file names with non-ASCII characters may not open.\n";
    }
    setIfUnset(L"XDG_CONFIG_HOME", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    setIfUnset(L"XDG_DATA_HOME", QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    setIfUnset(L"XDG_CACHE_HOME", QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
}

}  // namespace xqt::windows
