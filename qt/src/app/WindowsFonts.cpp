#include "WindowsFonts.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

#include <pango/pangocairo.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

namespace xqt::windows {

namespace {
std::wstring getEnv(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    size = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(size);
    return value;
}

/// Sets a variable for the process and for the C library, whose getenv() Pango and fontconfig read.
void setEnv(const std::wstring& name, const std::wstring& value) { _wputenv((name + L"=" + value).c_str()); }

/// The 8.3 name of an existing path when the volume has them: plain ASCII, so that fontconfig's ANSI file calls
/// find it whatever characters the user's name has. Else the path as it is.
fs::path shortPath(const fs::path& path) {
    const std::wstring& wide = path.native();
    DWORD size = GetShortPathNameW(wide.c_str(), nullptr, 0);
    if (size == 0) {
        return path;
    }
    std::wstring shortName(size, L'\0');
    size = GetShortPathNameW(wide.c_str(), shortName.data(), size);
    if (size == 0) {
        return path;
    }
    shortName.resize(size);
    return fs::path(shortName);
}

/// A path for fonts.conf: UTF-8, forward slashes, XML-escaped.
std::string xmlPath(const fs::path& path) {
    const std::u8string u8 = shortPath(path).generic_u8string();
    std::string result;
    for (char8_t c: u8) {
        switch (c) {
            case u8'&': result += "&amp;"; break;
            case u8'<': result += "&lt;"; break;
            case u8'>': result += "&gt;"; break;
            case u8'"': result += "&quot;"; break;
            default: result += static_cast<char>(c);
        }
    }
    return result;
}

fs::path programFolder() {
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(size);
    return fs::path(buffer).parent_path();
}

void alias(std::ofstream& out, const char* family, const char* font) {
    out << "  <alias binding=\"same\"><family>" << family << "</family><prefer><family>" << font
        << "</family></prefer></alias>\n";
}
}  // namespace

bool useFontconfig() {
    if (!getEnv(L"XQT_WIN_PANGO_WIN32").empty()) {
        std::cerr << "[xournal-qt] XQT_WIN_PANGO_WIN32: Pango's default font backend (win32).\n";
        return false;
    }
    if (!getEnv(L"PANGOCAIRO_BACKEND").empty() || !getEnv(L"FONTCONFIG_FILE").empty()) {
        return false;  // (someone chose already)
    }
    std::error_code ec;
    fs::path cache = getEnv(L"XDG_CACHE_HOME");
    if (cache.empty()) {
        cache = fs::path(getEnv(L"LOCALAPPDATA")) / L"cache";  // Qt's generic cache folder, as the app uses
    }
    const fs::path folder = cache / L"xournal-qt" / L"fontconfig";
    fs::create_directories(folder, ec);
    const fs::path file = folder / L"fonts.conf";

    std::wstring windowsFolder(MAX_PATH, L'\0');
    windowsFolder.resize(GetWindowsDirectoryW(windowsFolder.data(), MAX_PATH));
    const fs::path userFonts = fs::path(getEnv(L"LOCALAPPDATA")) / L"Microsoft" / L"Windows" / L"Fonts";
    const fs::path rules = programFolder().parent_path() / L"etc" / L"fonts" / L"conf.d";

    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
               "<!-- Written by xournal-qt at every start (qt/src/app/WindowsFonts.cpp): changes are lost. -->\n"
               "<fontconfig>\n";
        out << "  <dir>" << xmlPath(fs::path(windowsFolder) / L"Fonts") << "</dir>\n";
        out << "  <dir>" << xmlPath(userFonts) << "</dir>\n";
        out << "  <cachedir>" << xmlPath(folder) << "</cachedir>\n";
        // Before conf.d: a "prefer" that comes first stays in front of the ones conf.d adds.
        // (Family names match whatever their case.)
        for (const char* family: {"Sans", "sans-serif"}) {
            alias(out, family, "Arial");
        }
        alias(out, "Serif", "Times New Roman");
        for (const char* family: {"Monospace", "mono"}) {
            alias(out, family, "Courier New");
        }
        if (fs::is_directory(rules, ec)) {
            out << "  <include ignore_missing=\"yes\">" << xmlPath(rules) << "</include>\n";
        }
        out << "</fontconfig>\n";
        if (!out) {
            std::cerr << "[xournal-qt] Could not write " << xmlPath(file) << "; Pango keeps its default font backend.\n";
            return false;
        }
    }
    setEnv(L"FONTCONFIG_FILE", shortPath(file).native());
    setEnv(L"PANGOCAIRO_BACKEND", L"fc");
    return true;
}

void warmUpFonts() {
    std::thread([] {
        // A font map of this thread's own (Pango's default ones are per thread); fontconfig's configuration and font
        // list, which it loads here, are shared by the whole process.
        PangoFontMap* map = pango_cairo_font_map_new();
        PangoContext* context = pango_font_map_create_context(map);
        PangoFontDescription* description = pango_font_description_from_string("Sans 12");
        if (PangoFont* font = pango_font_map_load_font(map, context, description)) {
            g_object_unref(font);
        }
        pango_font_description_free(description);
        g_object_unref(context);
        g_object_unref(map);
    }).detach();
}

}  // namespace xqt::windows
