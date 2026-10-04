#include "TodoCalendar.h"

#include <map>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QVariantMap>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace xqt::todocal {

namespace {
/// TEXT of RFC 5545: \ ; , escaped, line breaks as \n
QString escaped(QString s) {
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char(';'), QLatin1String("\;"));
    s.replace(QLatin1Char(','), QLatin1String("\\,"));
    s.replace(QLatin1String("\r\n"), QLatin1String("\\n"));
    s.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return s;
}

/// A content line, folded at 75 octets (not inside a UTF-8 character), ending in CRLF
QByteArray line(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    QByteArray out;
    int start = 0;
    int limit = 75;
    while (bytes.size() - start > limit) {
        int cut = start + limit;
        while (cut > start && (static_cast<unsigned char>(bytes[cut]) & 0xC0) == 0x80) {
            --cut;  // (a continuation byte: the character goes to the next line)
        }
        out += bytes.mid(start, cut - start) + "\r\n ";
        start = cut;
        limit = 74;  // (the space that starts a folded line counts)
    }
    out += bytes.mid(start) + "\r\n";
    return out;
}

QString day(const QDate& d) { return d.toString(QStringLiteral("yyyyMMdd")); }

QString cacheDir() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/calendar");
}
}  // namespace

QUrl linkTo(const fs::path& file, int page) {
    QUrl url = QUrl::fromLocalFile(QString::fromStdString(file.string()));
    if (page >= 0) {
        url.setFragment(QStringLiteral("page=%1").arg(page + 1));
    }
    return url;
}

Item itemOf(const QVariantMap& row) {
    Item it;
    it.text = row.value("text").toString();
    if (it.text.isEmpty()) {
        it.text = row.value("inkText").toString();
    }
    it.due = QDate::fromString(row.value("due").toString(), Qt::ISODate);
    it.document = row.value("name").toString();
    it.page = row.value("page").toInt();
    const fs::path file(row.value("path").toString().toStdString());
    it.link = linkTo(file, it.page);
    it.uid = QString::fromLatin1(QCryptographicHash::hash((row.value("path").toString() + '|' +
                                                           row.value("rawText").toString() + '|' +
                                                           QString::number(row.value("occurrence").toInt()))
                                                                  .toUtf8(),
                                                          QCryptographicHash::Sha1)
                                         .toHex()
                                         .left(24)) +
             QStringLiteral("@xournal-qt");
    return it;
}

QByteArray ics(const std::vector<Item>& items) {
    QByteArray out;
    out += line(QStringLiteral("BEGIN:VCALENDAR"));
    out += line(QStringLiteral("VERSION:2.0"));
    out += line(QStringLiteral("PRODID:-//xournal-qt//To-dos//EN"));
    out += line(QStringLiteral("CALSCALE:GREGORIAN"));
    out += line(QStringLiteral("METHOD:PUBLISH"));
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    for (const Item& it: items) {
        if (!it.due.isValid()) {
            continue;
        }
        QString where = it.document;
        if (it.page >= 0) {
            where = QStringLiteral("%1, page %2").arg(it.document).arg(it.page + 1);
        }
        out += line(QStringLiteral("BEGIN:VEVENT"));
        out += line(QStringLiteral("UID:") + it.uid);
        out += line(QStringLiteral("DTSTAMP:") + stamp);
        out += line(QStringLiteral("DTSTART;VALUE=DATE:") + day(it.due));
        out += line(QStringLiteral("DTEND;VALUE=DATE:") + day(it.due.addDays(1)));
        out += line(QStringLiteral("SUMMARY:") + escaped(it.text.isEmpty() ? QStringLiteral("To-do") : it.text));
        out += line(QStringLiteral("DESCRIPTION:") +
                    escaped(QStringLiteral("To-do in %1\n%2").arg(where, it.link.toString(QUrl::FullyEncoded))));
        if (it.link.isValid()) {
            out += line(QStringLiteral("URL:") + it.link.toString(QUrl::FullyEncoded));
        }
        out += line(QStringLiteral("TRANSP:TRANSPARENT"));
        out += line(QStringLiteral("END:VEVENT"));
    }
    out += line(QStringLiteral("END:VCALENDAR"));
    return out;
}

QString markdown(const std::vector<Item>& items, const QString& title) {
    QString out = QStringLiteral("# %1\n").arg(title);
    std::vector<QString> order;
    std::map<QString, std::vector<const Item*>> byDocument;
    for (const Item& it: items) {
        auto& in = byDocument[it.document];
        if (in.empty()) {
            order.push_back(it.document);
        }
        in.push_back(&it);
    }
    for (const QString& doc: order) {
        out += QStringLiteral("\n## %1\n\n").arg(doc);
        for (const Item* it: byDocument[doc]) {
            QString entry = QStringLiteral("- [ ] ") + (it->text.isEmpty() ? QStringLiteral("(handwritten)") : it->text);
            if (it->due.isValid()) {
                entry += QStringLiteral(" \U0001F4C5 ") + it->due.toString(Qt::ISODate);
            }
            const QString where = it->page >= 0 ? QStringLiteral("page %1").arg(it->page + 1) : QStringLiteral("open");
            entry += QStringLiteral(" ([%1](%2))").arg(where, it->link.toString(QUrl::FullyEncoded));
            out += entry + '\n';
        }
    }
    return out;
}

QString writeToCache(const Item& item) {
    QDir dir(cacheDir());
    if (!dir.mkpath(QStringLiteral("."))) {
        return {};
    }
    // (the folder's owner: files older than a day go, at most 20 stay)
    const QFileInfoList old = dir.entryInfoList({QStringLiteral("*.ics")}, QDir::Files, QDir::Time);
    for (qsizetype i = 0; i < old.size(); ++i) {
        if (i >= 19 || old[i].lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600) {
            QFile::remove(old[i].absoluteFilePath());
        }
    }
    QString name = item.text.left(40);
    for (QChar& c: name) {
        if (!c.isLetterOrNumber()) {
            c = QLatin1Char('-');
        }
    }
    const QString path =
            dir.filePath(QStringLiteral("%1-%2.ics").arg(name.isEmpty() ? QStringLiteral("todo") : name, day(item.due)));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(ics({item})) < 0 || !f.commit()) {
        return {};
    }
    return path;
}

bool insertIntoCalendar(const Item& item) {
#ifdef Q_OS_ANDROID
    if (!item.due.isValid()) {
        return false;
    }
    // (an all-day event starts at midnight UTC: CalendarContract's rule)
    const qint64 begin = QDateTime(item.due, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
    const QString where = item.page >= 0 ? QStringLiteral("%1, page %2").arg(item.document).arg(item.page + 1)
                                         : item.document;
    const QJniObject title = QJniObject::fromString(item.text);
    const QJniObject description =
            QJniObject::fromString(QStringLiteral("To-do in %1\n%2").arg(where, item.link.toString()));
    return QJniObject::callStaticMethod<jboolean>("org/xournalqt/app/XournalActivity", "insertCalendarEvent",
                                                  "(Ljava/lang/String;Ljava/lang/String;J)Z", title.object<jstring>(),
                                                  description.object<jstring>(), static_cast<jlong>(begin));
#else
    Q_UNUSED(item);
    return false;
#endif
}

}  // namespace xqt::todocal
