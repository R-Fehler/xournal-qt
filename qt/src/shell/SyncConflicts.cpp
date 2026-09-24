/*
 * xournal-qt: see SyncConflicts.h.
 *
 * @license GNU GPLv2 or later
 */
#include "SyncConflicts.h"

#include <QRegularExpression>
#include <QString>

namespace xqt::SyncConflicts {

namespace {

QString dateTime(const QString& date, const QString& time) {
    // "20240312" / "2024-03-12", "101530" / "10-15-30" / "1015" -> "2024-03-12 10:15"
    QString d = date;
    d.remove(QLatin1Char('-'));
    QString t = time;
    t.remove(QLatin1Char('-'));
    if (d.size() != 8) {
        return {};
    }
    QString out = d.left(4) + QLatin1Char('-') + d.mid(4, 2) + QLatin1Char('-') + d.mid(6, 2);
    if (t.size() >= 4) {
        out += QLatin1Char(' ') + t.left(2) + QLatin1Char(':') + t.mid(2, 2);
    }
    return out;
}

Conflict make(const QString& stem, const QString& ext, const char* app, const QString& when) {
    return {(stem + ext).toStdString(), app, when.toStdString()};
}

}  // namespace

std::optional<Conflict> parse(const std::string& fileName) {
    const QString name = QString::fromStdString(fileName);
    if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) {
        return std::nullopt;  // (hidden files are never documents)
    }
    // Syncthing: "<name>.sync-conflict-<date>-<time>-<device>.<ext>", the extension optional
    static const QRegularExpression syncthing(
            QStringLiteral(R"(^(.+?)\.sync-conflict-(\d{8})-(\d{6})(?:-[A-Z0-9]{7})?(\.[^.]+)?$)"));
    if (const auto m = syncthing.match(name); m.hasMatch()) {
        return make(m.captured(1), m.captured(4), "Syncthing", dateTime(m.captured(2), m.captured(3)));
    }
    // Older ownCloud clients: "<name>_conflict-<date>-<time>.<ext>"
    static const QRegularExpression owncloud(QStringLiteral(R"(^(.+?)_conflict-(\d{8})-(\d{6})(\.[^.]+)?$)"));
    if (const auto m = owncloud.match(name); m.hasMatch()) {
        return make(m.captured(1), m.captured(4), "ownCloud", dateTime(m.captured(2), m.captured(3)));
    }
    // OneDrive: "<name>-<computer>.<ext>", Windows' default computer names only ("DESKTOP-AB12CDE"), maybe "-2"
    static const QRegularExpression onedrive(
            QStringLiteral(R"(^(.+?)-(?:DESKTOP|LAPTOP)-[A-Z0-9]{7}(?:-\d+)?(\.[^.]+)?$)"));
    if (const auto m = onedrive.match(name); m.hasMatch()) {
        return make(m.captured(1), m.captured(2), "OneDrive", {});
    }
    // A mark in parentheses before the extension with a word for "conflict" in it (Dropbox, Nextcloud and ownCloud
    // in any language, Seafile's "SFConflict", Dropbox's "Case Conflict", ...)
    static const QRegularExpression marked(QStringLiteral(R"(^(.+?) ?\(([^()]*)\)(\.[^.]+)?$)"));
    const auto m = marked.match(name);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const QString mark = m.captured(2);
    static const QRegularExpression word(
            QString::fromUtf8("conflict|conflit|konflikt|конфликт|衝突|冲突|충돌"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (!word.match(mark).hasMatch()) {
        return std::nullopt;
    }
    const char* app = "";
    QString when;
    static const QRegularExpression dated(QStringLiteral(R"((\d{4}-\d{2}-\d{2})(?:[ _-](\d{2}-?\d{2}-?\d{2}|\d{4,6}))?)"));
    if (const auto d = dated.match(mark); d.hasMatch()) {
        when = dateTime(d.captured(1), d.captured(2));
    }
    if (mark.startsWith(QLatin1String("SFConflict"), Qt::CaseInsensitive)) {
        app = "Seafile";
    } else if (mark.contains(QRegularExpression(QString::fromUtf8("['’]s conflicted copy"),
                                                QRegularExpression::CaseInsensitiveOption)) ||
               mark.contains(QLatin1String("Case Conflict")) || mark.contains(QLatin1String("Sync Conflict")) ||
               mark.contains(QLatin1String("Encoding Conflict"))) {
        app = "Dropbox";
    } else if (mark.contains(QRegularExpression(QStringLiteral(R"(\d{4}-\d{2}-\d{2} \d{6})")))) {
        app = "Nextcloud";  // (also ownCloud's newer clients: the same name)
    }
    return make(m.captured(1), m.captured(3), app, when);
}

}  // namespace xqt::SyncConflicts
