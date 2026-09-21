/*
 * xournal-qt: the pages of the current tab, for the page sidebar.
 *
 * Follows the document events of the session it is attached to (pages inserted, deleted, changed, resized) and
 * the undoable edits (DocumentSession::pageContentChanged). The thumbnail image URL names the page's revision
 * (DocumentSession::pageRevision), so it changes with the content and QML reloads it; a page that did not change
 * keeps its URL, also when the tab is shown again (its kept image is shown, see ThumbnailProvider). Content changes
 * are collected for a moment first, so that writing does not re-render thumbnails continuously.
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
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
public:
    enum Roles {
        PageNumberRole = Qt::UserRole + 1,
        AspectRole,
        ThumbnailRole,
        CurrentRole,
        /// Search hits on the page: list of rectangles relative to the page size (0..1), at most
        /// MAX_THUMBNAIL_HITS (and the current hit)
        SearchHitsRole,
        /// Index of the current search hit in SearchHitsRole, or -1
        CurrentSearchHitRole,
        /// Number of search hits on the page
        SearchHitCountRole,
        /// 0-based page index (the row; stays right in filtered views)
        PageIndexRole,
        /// The page is selected (for page operations in the sidebar and the page grid)
        SelectedRole
    };

    static constexpr int MAX_THUMBNAIL_HITS = 50;

    explicit PagesModel(QObject* parent = nullptr);
    ~PagesModel() override;

    /// Show the pages of this session (nullptr: none).
    void setSession(DocumentSession* session);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int currentPage() const;
    qreal typicalAspect() const { return aspect; }

    // --- selection (like a file manager: Ctrl toggles, Shift selects a range from the last clicked page) ---
    Q_INVOKABLE void select(int page, int modifiers = 0);
    Q_INVOKABLE void toggleSelected(int page);
    Q_INVOKABLE void selectPages(const QList<int>& pages);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    /// Start of a later Shift+click range (e.g. the page opened by a plain click).
    Q_INVOKABLE void setAnchor(int page) { anchor = page; }
    Q_INVOKABLE bool isSelected(int page) const;
    /// Selected pages in document order.
    Q_INVOKABLE QList<int> selectedPages() const;
    /// Thumbnail image URL of a page (as the ThumbnailRole), e.g. for the contents overview.
    Q_INVOKABLE QString thumbnailUrl(int page) const;
    /// Height / width of a page.
    Q_INVOKABLE qreal aspectOf(int page) const;
    int selectionCount() const;

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
    void selectionChanged();

private:
    void reset();
    void updateTypicalAspect();
    void setSelection(std::vector<char> next);
    void markChanged(size_t page);
    void flushChanges();

    QPointer<DocumentSession> session;
    quint64 sessionId = 0;
    std::vector<QSizeF> sizes;
    std::set<size_t> changed;
    QTimer refreshTimer;
    int current = 0;
    qreal aspect = 1.414;
    std::vector<char> selected;  ///< per row
    int anchor = -1;             ///< last page clicked (Shift+click range start)
    std::vector<QMetaObject::Connection> connections;
};

}  // namespace xqt
