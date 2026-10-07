/*
 * xournal-qt: comparing two versions of a document, or a version and now, in the reference view (`app.compare`;
 * qt/docs/reference-view.md "Comparing", qt/docs/hybrid-pdf.md "Version history").
 *
 * The newer one is the tab's document (now, or a version shown read-only), the older one its reference (a version,
 * read-only), and the two are scrolled together (ReferenceMode::scrollLocked). The pages that differ are found from
 * what the pages hold (VersionDiff.h; nothing is drawn): marked in the page sidebar, the page grid and the reference's
 * grid (CompareMarks), counted in the comparison's bar, with next and previous change, which show the change on both
 * sides (a page added or removed: where it would be on the other side).
 *
 * The pages are signed on the GUI thread a few milliseconds at a time (never long enough to be seen; the document is
 * not changed meanwhile), and kept by page revision (DocumentSession::pageRevision), so writing on the newer side
 * signs again only the pages written on. That cache is this object's and holds the pages of the two documents only.
 * The comparison ends when the reference is closed or replaced, or either document is closed.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include "model/PageRef.h"
#include "session/VersionDiff.h"

namespace xqt {

class DocumentSession;
class ReferenceMode;
class TabManager;

/// The pages marked as changed in a comparison, per document: the page models show them (PagesModel::DiffersRole).
class CompareMarks final: public QObject {
    Q_OBJECT
public:
    static CompareMarks& instance();
    bool differs(const DocumentSession* session, size_t page) const;
    /// Mark the pages of a document (empty: none)
    void set(const DocumentSession* session, std::vector<char> marks);

Q_SIGNALS:
    void changed(const xqt::DocumentSession* session);

private:
    std::map<const DocumentSession*, std::vector<char>> marks;
};

class VersionCompare final: public QObject {
    Q_OBJECT
    /// A comparison is set up (its documents are open)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    /// ... and on screen: the current tab is the newer one, with the older one as its reference
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    /// The pages are being compared
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// "4 Oct 14:30", "Now"
    Q_PROPERTY(QString olderTitle READ olderTitle NOTIFY changed)
    Q_PROPERTY(QString newerTitle READ newerTitle NOTIFY changed)
    /// The changes (pages changed, added or removed)
    Q_PROPERTY(int changeCount READ changeCount NOTIFY changed)
    /// "3 pages changed, 1 added" ("No page changed")
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
public:
    VersionCompare(TabManager& tabs, ReferenceMode& reference, QObject* parent = nullptr);
    ~VersionCompare() override;

    /// Compare `older`, shown as the reference of the tab of `newer` (the current one), with it, and lock their
    /// scrolling. Both are open tabs.
    void start(DocumentSession* newer, DocumentSession* older, const QString& newerTitle, const QString& olderTitle);
    /// End the comparison (the reference stays).
    Q_INVOKABLE void stop();
    /// End it and close the reference.
    Q_INVOKABLE void close();
    Q_INVOKABLE void nextChange();
    Q_INVOKABLE void previousChange();
    /// Show change `index` (0-based) on both sides
    Q_INVOKABLE void showChange(int index);

    bool active() const { return !newer.isNull() && !older.isNull(); }
    bool shown() const;
    bool busy() const { return !pending.empty() || again.isActive(); }
    QString olderTitle() const { return olderName; }
    QString newerTitle() const { return newerName; }
    int changeCount() const { return static_cast<int>(result.changes.size()); }
    QString summary() const;
    /// The change shown last (1-based; 0: none yet)
    int currentChange() const { return current + 1; }
    const versiondiff::Result& changes() const { return result; }
    DocumentSession* newerSession() const;
    DocumentSession* olderSession() const;

    /// Milliseconds a slice of signing may take (tests)
    int sliceMs = 8;

Q_SIGNALS:
    void changed();
    void positionChanged();
    /// The pages were compared (again)
    void compared();

private:
    struct Job {
        bool newerSide;
        size_t index;
        quint64 revision;
        PageRef page;
    };
    /// Sign what is not signed yet, then compare (in slices)
    void compute();
    void step();
    void finish();
    void clearMarks();
    /// The page the current tab's view and the reference show (for next / previous from there)
    size_t pageInView() const;

    TabManager& tabs;
    ReferenceMode& reference;
    QPointer<DocumentSession> newer, older;
    QString newerName, olderName;
    std::vector<QMetaObject::Connection> connections;
    /// Page revision -> signature (the pages of the two documents only)
    std::map<quint64, uint64_t> signatures;
    std::vector<quint64> newerRevisions, olderRevisions;
    std::vector<Job> pending;
    QTimer slice;
    QTimer again;  ///< a document changed: compared again after a moment
    versiondiff::Result result;
    int current = -1;
};

}  // namespace xqt
