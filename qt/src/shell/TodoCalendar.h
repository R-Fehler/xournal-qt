/*
 * xournal-qt: to-dos handed to the system's calendar, one way (qt/docs/todos.md, "Calendar"): an all-day event on the
 * to-do's due date in an iCalendar file (.ics, RFC 5545) that the calendar app imports, or on Android its "new event"
 * screen filled in (CalendarContract's ACTION_INSERT). Nothing comes back: no sync, no reminders. The open to-dos of
 * the To-dos view are exported as one .ics (those with a due date) or as a Markdown list.
 *
 * Why an all-day VEVENT and not a VTODO: Google Calendar, Outlook and most phone calendars import events from an .ics
 * but ignore or reject VTODOs (only Apple Reminders, Thunderbird and a few task apps read those).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "filesystem.h"

namespace xqt::todocal {

/// A to-do as the calendar gets it
struct Item {
    QString text;      ///< what it says (as the list shows it)
    QDate due;         ///< its day (invalid: none: not in a calendar)
    QString document;  ///< its document's name
    int page = -1;     ///< 0-based (-1: none)
    QUrl link;         ///< back to its page (file URL with #page=N)
    QString uid;       ///< stable for the same to-do (another import replaces it in apps that go by it)
};

/// An item from a row of the To-dos view (LibraryTodosModel::listed)
Item itemOf(const QVariantMap& row);
/// A file link to a document's page: "file:///…/name.xopp#page=3" (`page` 0-based; -1: the document)
QUrl linkTo(const fs::path& file, int page);

/// One VCALENDAR with an all-day VEVENT per item that has a due date (CRLF lines, folded at 75 octets, text escaped)
QByteArray ics(const std::vector<Item>& items);
/// The items as a Markdown task list, under a heading per document, with their due dates and links
QString markdown(const std::vector<Item>& items, const QString& title);

/// Write an .ics for one to-do into the app's cache (its folder "calendar": its files older than a day go, at most 20
/// are kept). Returns the file ("": it could not be written).
QString writeToCache(const Item& item);
/// Android: the calendar app's "new event" screen with the to-do filled in (all day, its text, the document and page).
/// False elsewhere, or when no calendar app takes it.
bool insertIntoCalendar(const Item& item);

}  // namespace xqt::todocal
