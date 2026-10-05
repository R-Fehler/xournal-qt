/*
 * xournal-qt: the version history of the current document for the page sidebar's History panel (PdfHistory.h,
 * qt/docs/hybrid-pdf.md "Version history").
 *
 * Newest first: "Unsaved changes" while the document is modified, then each version (its date, its message for a
 * milestone) and, where they are in the file, revisions another app added. The list is read from the file on a worker
 * (it reads the whole file: tens of milliseconds) while the panel is shown, and again after each save of the document
 * and when a message changes. The switch ("Keep versions of this document") is the session's keepsVersions().
 *
 * Restoring a version (restore()) loads it on a worker (VersionCache), copies its pages as the page clipboard does,
 * and replaces the document's pages with them in one undo step; the next save is a new version with a message
 * saying which one was restored.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <ctime>
#include <functional>
#include <vector>

#include <QAbstractListModel>
#include <QPointer>
#include <QString>

#include "session/PdfHistory.h"

namespace xqt {

class DocumentSession;

class VersionsModel final: public QAbstractListModel {
    Q_OBJECT
    /// The panel is shown: only then is the list read.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// The document can keep versions (a PDF with notes, or one that becomes one when saved)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    /// Why not (empty when available)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    /// It is a .xopp document: "Save as PDF with notes" makes it one
    Q_PROPERTY(bool needsPdf READ needsPdf NOTIFY changed)
    /// "Keep versions of this document" (DocumentSession::keepsVersions)
    Q_PROPERTY(bool on READ on WRITE setOn NOTIFY changed)
    /// The switch was changed here: the next save writes it into the file
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// The versions in the file
    Q_PROPERTY(int versionCount READ versionCount NOTIFY changed)
    /// Only milestones (versions with a message) are listed
    Q_PROPERTY(bool milestonesOnly READ milestonesOnly WRITE setMilestonesOnly NOTIFY changed)
    /// "18.2 MB in all, 8.3 MB of it from the versions since 4 Oct" (empty: no versions)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    /// Versions another app removed (it wrote the file anew), said once in the panel
    Q_PROPERTY(int removed READ removed NOTIFY changed)
    /// A version is being restored
    Q_PROPERTY(bool restoring READ restoring NOTIFY busyChanged)
public:
    enum Roles {
        VersionIdRole = Qt::UserRole + 1,  ///< -1: not a version
        KindRole,       ///< "unsaved", "version", "other" (another app's revision)
        TitleRole,      ///< its date for people: "Today 14:30", "Yesterday 9:12", "4 Oct 14:30"
        MessageRole,
        MilestoneRole,
        DetailRole,     ///< "As received", "3 pages", …
        CurrentRole,    ///< the latest version (what the file is now)
    };

    explicit VersionsModel(QObject* parent = nullptr);
    ~VersionsModel() override;

    void setSession(DocumentSession* session);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return isActive; }
    void setActive(bool on);
    bool available() const;
    QString unavailableReason() const;
    bool needsPdf() const;
    bool on() const;
    void setOn(bool on);
    bool pending() const;
    bool busy() const { return reading; }
    bool restoring() const { return restoringNow; }
    int versionCount() const { return static_cast<int>(listed.versions.size()); }
    bool milestonesOnly() const { return onlyMilestones; }
    void setMilestonesOnly(bool on);
    QString sizeText() const;
    int removed() const { return listed.removed; }

    /// Read the list again (on a worker).
    Q_INVOKABLE void refresh();
    /// The title of a version ("4 Oct 14:30"), for messages and dialogs.
    Q_INVOKABLE QString titleOf(int id) const;
    Q_INVOKABLE QString messageOf(int id) const;
    /// Restore version `id` (see above); restored() says how it went.
    Q_INVOKABLE bool restore(int id);

    /// The history as last read (tests).
    const PdfHistory::Listed& history() const { return listed; }
    /// A date for people, relative to `now` (tests give one).
    static QString titleFor(const std::string& isoUtc, std::time_t now);

Q_SIGNALS:
    void activeChanged();
    void changed();
    void busyChanged();
    /// restore() finished: `ok`, or why not.
    void restored(bool ok, const QString& message);

private:
    struct Row {
        int id = -1;
        QString kind;
        QString title, message, detail;
        bool milestone = false, current = false;
    };
    void rebuild();
    QPointer<DocumentSession> session;
    std::vector<QMetaObject::Connection> connections;
    bool isActive = false;
    bool reading = false;
    bool again = false;  ///< read again when the running read ends
    bool restoringNow = false;
    bool onlyMilestones = false;
    unsigned generation = 0;
    PdfHistory::Listed listed;
    std::vector<Row> rows;
};

}  // namespace xqt
