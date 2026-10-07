/*
 * xournal-qt: the "To-dos" view of the library home (qt/docs/todos.md): the to-dos of all documents of the library,
 * grouped by document (the default), by folder, or not at all; sorted by due date, document, or when the document was
 * last changed; filtered by state (open, done, all), due date (overdue, today, this week, no date), a text and the
 * library's current folder.
 *
 * It comes from the library's index (LibraryIndex::todos: read into each folder's "notes" pack when a document is
 * indexed), so no document is opened to list them; which task lines are to-dos is the setting (todos::Rules). It also
 * follows the library's "Show" filter and its Favourites filter, as the Bookmarks view does. It is made again when the
 * index's to-dos change (while it is active) or when a filter does. A to-do just ticked in the view shows its new state
 * at once (pending) until the index has it.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <tuple>
#include <vector>

#include <QAbstractListModel>
#include <QDate>
#include <QPointer>
#include <QRectF>
#include <QTimer>

#include "filesystem.h"
#include "DocumentFiles.h"
#include "Library.h"
#include "LibraryIndex.h"
#include "Todos.h"

namespace xqt {

class LibraryModel;

class LibraryTodosModel final: public QAbstractListModel {
    Q_OBJECT
    /// To-dos listed
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// To-dos of the library by the setting, before the view's filters (0: there are none at all)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    /// The view is shown: it follows the index (else it is not made at all)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// "document" (the default), "folder" or "none"
    Q_PROPERTY(QString grouping READ grouping WRITE setGrouping NOTIFY filtersChanged)
    /// "due" (the default: the soonest first, then those without a date), "document", "changed" (last changed first)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY filtersChanged)
    /// "open" (the default), "done", "all"
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY filtersChanged)
    /// "any" (the default), "overdue", "today", "week" (from today to the end of this week), "none" (no date)
    Q_PROPERTY(QString due READ due WRITE setDue NOTIFY filtersChanged)
    /// Only to-dos whose text, document name or folder contains it (case ignored)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filtersChanged)
    /// Only the to-dos of the library's current folder (and its subfolders)
    Q_PROPERTY(bool folderOnly READ folderOnly WRITE setFolderOnly NOTIFY filtersChanged)
    /// Which task lines are to-dos: every check box (else the ones with the marker, and stamps)
    Q_PROPERTY(bool collectAll READ collectAll NOTIFY rulesChanged)
    Q_PROPERTY(QString marker READ marker NOTIFY rulesChanged)
public:
    enum Roles {
        TextRole = Qt::UserRole + 1,  ///< as shown: without the marker and the due date
        RawTextRole,                  ///< as written (to find it again: setTodoDone)
        OccurrenceRole,
        DoneRole,
        DueRole,         ///< "YYYY-MM-DD" ("": none)
        DueStateRole,    ///< "overdue", "today", "week", "later", "" (none)
        PathRole,        ///< the document's main file
        NameRole,        ///< the document's name
        FolderRole,      ///< its folder relative to the library ("": the top)
        PageRole,        ///< 0-based (-1: a Markdown file)
        LineRole,        ///< its line in its box (a Markdown file: in the file), 0-based
        StampRole,       ///< a check-box stamp (handwriting beside it)
        GroupRole,       ///< what it is grouped by (the section of the list; "" not grouped)
        GroupLabelRole,  ///< the group's title (a document's name, a folder, "Library" for the top)
        GroupCountRole,  ///< to-dos listed in its group
        PendingRole,     ///< ticked in the view, not in the index yet
        BoxRole,         ///< its box on the page
        PlaceRole,       ///< { x, y, size, pageWidth } of its box (page coordinates)
        PictureRole,     ///< a stamp: image URL of the handwriting beside it (HitPageProvider::areaUrl; "": none)
        InkTextRole,     ///< a stamp: the handwriting beside it as recognised (handwriting search; "": not read)
    };
    /// A stamp's handwriting: from its check box to the page's right margin, about one line high (page points)
    static QRectF stampArea(const LibraryIndex::Todo& todo);

    explicit LibraryTodosModel(LibraryModel* library, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(rows.size()); }
    int total() const { return totalCount; }
    bool active() const { return isActive; }
    void setActive(bool on);
    QString grouping() const { return groupBy; }
    void setGrouping(const QString& g);
    QString sortBy() const { return sortKey; }
    void setSortBy(const QString& s);
    QString status() const { return state; }
    void setStatus(const QString& s);
    QString due() const { return dueFilter; }
    void setDue(const QString& d);
    QString query() const { return text; }
    void setQuery(const QString& q);
    bool folderOnly() const { return inFolder; }
    void setFolderOnly(bool on);
    bool collectAll() const { return rules.all; }
    QString marker() const { return rules.marker; }
    void setRules(const todos::Rules& r);
    /// The day the due dates are measured against (tests; default: today, read at each refresh)
    void setToday(const QDate& day) { fixedToday = day; }
    QDate today() const;
    /// The due state of a date ("YYYY-MM-DD") on this day: "overdue", "today", "week", "later" ("": none)
    static QString dueState(const QString& due, const QDate& today);

    /// A to-do was ticked or unticked (written, or being written): the view shows it so until the index has it
    void setPending(const QString& path, const QString& rawText, int occurrence, bool done);
    /// It was not written after all: the view shows what the index has
    void clearPending(const QString& path, const QString& rawText, int occurrence);
    /// A group of the list (GroupRole): { label, folder (its document's, grouped by document), count, path }
    Q_INVOKABLE QVariantMap groupOf(const QString& group) const;
    /// A row's roles as a map (calendar, exports)
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Rows of the to-dos listed (for exports): their roles, as a list of maps
    Q_INVOKABLE QVariantList listed() const;
    /// Read the index again (it is also done when its to-dos change).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void countChanged();
    void activeChanged();
    void filtersChanged();
    void rulesChanged();

private:
    struct Row {
        LibraryIndex::Todo todo;
        QString shown, name, folder, group, groupLabel, dueState, picture;
        int groupCount = 0;
        bool pending = false;
        qint64 changed = 0;  ///< its document's file: last changed (ms since the epoch)
        int order = 0;       ///< in the index's order
    };
    void changedMaybe();
    void filterChanged();

    QPointer<LibraryModel> library;
    std::vector<Row> rows;
    int totalCount = 0;
    bool isActive = false;
    QString groupBy = QStringLiteral("document");
    QString sortKey = QStringLiteral("due");
    QString state = QStringLiteral("open");
    QString dueFilter = QStringLiteral("any");
    QString text;
    bool inFolder = false;
    todos::Rules rules;
    QDate fixedToday;
    /// Ticked in the view: (file, raw text, occurrence) -> done
    std::map<std::tuple<fs::path, QString, int>, bool> pending;
    quint64 seen = ~quint64(0);  ///< the index's todoChanges() when made
    QTimer poll;                 ///< (documents saved in the app change the index without a progress signal)
};

}  // namespace xqt
