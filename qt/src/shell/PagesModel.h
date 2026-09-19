/*
 * xournal-qt: the pages of the current tab, for the page sidebar.
 *
 * Follows the document events of the session it is attached to (pages inserted, deleted, changed, resized) and
 * the undoable edits (DocumentSession::pageContentChanged). Each page has a revision that is increased when its
 * content changed, so the thumbnail image URL changes and QML reloads it. Content changes are collected for a
 * moment first, so that writing does not re-render thumbnails continuously.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <set>
#include <vector>

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>

#include "model/DocumentListener.h"

namespace xqt {

class DocumentSession;

class PagesModel final: public QAbstractListModel, public DocumentListener {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    /// Height / width of most pages (median): the page grid sizes its cells with it (slides, A4, ...).
    Q_PROPERTY(qreal typicalAspect READ typicalAspect NOTIFY typicalAspectChanged)
public:
    enum Roles {
        PageNumberRole = Qt::UserRole + 1,
        AspectRole,
        ThumbnailRole,
        CurrentRole,
        /// Search hits on the page: list of rectangles relative to the page size (0..1)
        SearchHitsRole,
        /// Index of the current search hit in SearchHitsRole, or -1
        CurrentSearchHitRole,
        /// Number of search hits on the page
        SearchHitCountRole,
        /// 0-based page index (the row; stays right in filtered views)
        PageIndexRole
    };

    explicit PagesModel(QObject* parent = nullptr);
    ~PagesModel() override;

    /// Show the pages of this session (nullptr: none).
    void setSession(DocumentSession* session);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int currentPage() const;
    qreal typicalAspect() const { return aspect; }

    /// Milliseconds to collect content changes before thumbnails are refreshed.
    void setRefreshDelay(int ms) { refreshTimer.setInterval(ms); }

    // DocumentListener
    void documentChanged(DocumentChangeType type) override;
    void pageSizeChanged(size_t page) override;
    void pageChanged(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;
    void pageSelected(size_t page) override;

Q_SIGNALS:
    void countChanged();
    void currentPageChanged();
    void typicalAspectChanged();

private:
    void reset();
    void updateTypicalAspect();
    void markChanged(size_t page);
    void flushChanges();

    QPointer<DocumentSession> session;
    quint64 sessionId = 0;
    std::vector<QSizeF> sizes;
    std::vector<quint64> revisions;
    quint64 nextRevision = 1;
    std::set<size_t> changed;
    QTimer refreshTimer;
    int current = 0;
    qreal aspect = 1.414;
    std::vector<QMetaObject::Connection> connections;
};

}  // namespace xqt
