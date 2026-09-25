/*
 * xournal-qt: what the app needs on Android before the core starts (see AndroidSetup.h and qt/docs/android.md).
 *
 * @license GNU GPLv2 or later
 */
#include "AndroidSetup.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QtGlobal>

#include "EmojiFont.h"

namespace xqt::android {

namespace {

QString writable(QStandardPaths::StandardLocation location) {
    const QString dir = QStandardPaths::writableLocation(location);
    QDir().mkpath(dir);
    return dir;
}

void setIfUnset(const char* name, const QString& value) {
    if (!qEnvironmentVariableIsSet(name) || qEnvironmentVariableIsEmpty(name)) {
        qputenv(name, QFile::encodeName(value));
    }
}

/// Copies the resource tree below `from` (":/...") to the folder `to`, file by file where missing or changed in size
/// (an update of the app replaces them; a start without changes only compares sizes).
void copyResources(const QString& from, const QString& to) {
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + source.mid(from.size());
        const QFileInfo existing(target);
        const QFileInfo resource(source);
        if (existing.exists() && existing.size() == resource.size()) {
            continue;
        }
        QDir().mkpath(existing.absolutePath());
        QFile::remove(target);
        if (QFile::copy(source, target)) {
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        } else {
            qWarning("xournal-qt: could not copy %s to %s", qPrintable(source), qPrintable(target));
        }
    }
}

/// fontconfig has no configuration on Android (vcpkg's build points at its own build prefix). Ours lists Android's
/// font folders, an app folder for fonts of the user's own, and maps the generic families Xournal++ uses ("Sans" is
/// the default text font) to Android's standard fonts. The app's own fonts (<resources>/fonts: the emoji font,
/// EmojiFont.h) come with their rules (there is no conf.d: fontconfig's rule for scaling bitmap fonts is among them).
void writeFontConfig(const QString& file, const QString& cacheDir, const QString& userFonts, const QString& appFonts) {
    const QString conf = QStringLiteral(R"(<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<!-- Written by xournal-qt at every start (AndroidSetup.cpp) -->
<fontconfig>
  <dir>/system/fonts</dir>
  <dir>/product/fonts</dir>
  <dir>%1</dir>
  <dir>%3</dir>
  <cachedir>%2</cachedir>
  <alias binding="same"><family>Sans</family><prefer><family>Roboto</family><family>Noto Sans</family></prefer></alias>
  <alias binding="same"><family>sans-serif</family><prefer><family>Roboto</family><family>Noto Sans</family></prefer></alias>
  <alias binding="same"><family>Serif</family><prefer><family>Noto Serif</family></prefer></alias>
  <alias binding="same"><family>serif</family><prefer><family>Noto Serif</family></prefer></alias>
  <alias binding="same"><family>Monospace</family><prefer><family>Droid Sans Mono</family><family>Cutive Mono</family></prefer></alias>
  <alias binding="same"><family>monospace</family><prefer><family>Droid Sans Mono</family><family>Cutive Mono</family></prefer></alias>
  <match target="pattern">
    <test qual="all" name="family" compare="not_eq"><string>sans-serif</string></test>
    <edit name="family" mode="append_last"><string>sans-serif</string></edit>
  </match>
%4</fontconfig>
)")
                                 .arg(userFonts.toHtmlEscaped(), cacheDir.toHtmlEscaped(), appFonts.toHtmlEscaped(),
                                      QString::fromStdString(emoji::fontconfigRules(true)));
    QFile f(file);
    if (f.open(QIODevice::ReadOnly) && f.readAll() == conf.toUtf8()) {
        return;
    }
    f.close();
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(conf.toUtf8());
    }
}

}  // namespace

void addSymbolFallback() {
    // Roboto (the UI font) has no ✓ ✎ ☐ ● ↵ ← → and friends, and Qt finds them in no other font: they showed as empty
    // boxes. (Android's "Noto Sans Symbols" has them, but in one of two files of that name, and Qt uses the other.)
    // A subset of DejaVu Sans with them comes with the app (qt/resources/fonts).
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/xqt-fonts/XqtSymbols.ttf"));
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qWarning("xournal-qt: the symbol font could not be loaded");
        return;
    }
    // (Symbols in a label are shaped with the text around them, so the fallback is needed for Latin text too.)
    for (const auto script: {QChar::Script_Common, QChar::Script_Latin}) {
        QFontDatabase::addApplicationFallbackFontFamily(script, families.first());
    }
}

void prepareEnvironment() {
    // The app's private folders: /data/user/0/<package>/files and .../cache.
    const QString data = writable(QStandardPaths::AppDataLocation);
    const QString cache = writable(QStandardPaths::CacheLocation);
    const QString config = writable(QStandardPaths::GenericConfigLocation);
    QDir().mkpath(data + "/state");
    setIfUnset("HOME", data);
    setIfUnset("TMPDIR", cache);
    setIfUnset("XDG_CONFIG_HOME", config);  // upstream's config folder: <this>/xournal-qt
    setIfUnset("XDG_CACHE_HOME", cache);
    setIfUnset("XDG_DATA_HOME", data);
    setIfUnset("XDG_STATE_HOME", data + "/state");

    // Page templates, palettes and icons: Qt resources in the APK (XqtAndroid.cmake), plain files for the core.
    const QString resources = data + "/share/xournal-qt";
    copyResources(QStringLiteral(":/xqt-share"), resources);
    setIfUnset("XQT_RESOURCE_DIR", resources);

    const QString fontCache = cache + "/fontconfig";
    const QString userFonts = data + "/fonts";
    QDir().mkpath(fontCache);
    QDir().mkpath(userFonts);
    const QString fontsConf = data + "/fonts.conf";
    writeFontConfig(fontsConf, fontCache, userFonts, resources + "/fonts");
    setIfUnset("FONTCONFIG_FILE", fontsConf);
    // GIO: no modules and no D-Bus session on Android.
    setIfUnset("GIO_USE_VFS", QStringLiteral("local"));
    setIfUnset("GSETTINGS_BACKEND", QStringLiteral("memory"));
}

}  // namespace xqt::android
