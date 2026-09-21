/*
 * xournal-qt: the open documents (tabs) of the window.
 *
 * Each tab owns a DocumentSession (the per-document "Control") and the CanvasView that shows it, so every tab keeps
 * its own zoom, scroll position, undo history and rendered pages. Pages of tabs that stay in the background for a
 * while release their rendered buffers (they are re-rendered when the tab is shown again).
 * The class is a list model for the QML tab strip.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <vector>

#include <QTimer>

#include <QAbstractListModel>
#include <QTimer>

#include "filesystem.h"

namespace xqt {

class AppContext;
class CanvasView;
class DocumentSession;

class TabManager final: public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    /// Hit marks on one page preview of the extended search (a one-letter search has hundreds)
    static constexpr int MAX_PAGE_HITS = 50;
    enum Roles { TitleRole = Qt::UserRole + 1, ModifiedRole, FilePathRole, CurrentRole, ThumbnailRole, PageCountRole,
                 SearchHitsRole, SearchRunningRole,
                 /// The pages with search hits, for the extended search of the overview:
                 /// [{ page, count, aspect, thumbnail, rects: [normalized hit rects, at most 50] }]
                 HitPagesRole };

    explicit TabManager(AppContext& app, QObject* parent = nullptr);
    ~TabManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(tabs.size()); }
    int currentIndex() const { return current; }
    void setCurrentIndex(int index);

    /// One open document: its session, the view showing it, when to release its rendered pages.
    struct Tab {
        std::unique_ptr<DocumentSession> session;
        std::unique_ptr<CanvasView> view;
        std::unique_ptr<QTimer> releaseTimer;
        quint64 thumbnailRevision = 0;  ///< increased when the current page or its content changed
    };

    /// Adds a tab after the current one and makes it current. Returns its index.
    int addTab(std::unique_ptr<DocumentSession> session);
    /// Takes the tab out (with its zoom, undo history and rendered pages): for another window's tab list.
    std::unique_ptr<Tab> takeTab(int index);
    /// Adds a tab that another window gave up. Returns its index.
    int adoptTab(std::unique_ptr<Tab> tab);
    /// Removes a tab (no questions asked: the UI deals with unsaved changes first).
    void closeTab(int index);
    void moveTab(int from, int to);

    /// Index of the tab showing this file, or -1.
    int indexOfFile(const fs::path& path) const;
    /// An untouched new document (no file, no changes): opening a file replaces it instead of adding a tab.
    bool isPristine(int index) const;

    DocumentSession* session(int index) const;
    CanvasView* view(int index) const;
    DocumentSession* currentSession() const { return session(current); }
    CanvasView* currentView() const { return view(current); }

    /// Milliseconds a tab must be in the background before its page buffers are released.
    void setBackgroundReleaseDelay(int ms) { releaseDelayMs = ms; }

    /// The picture of a tab may be another one now (its title page was chosen).
    void thumbnailChanged(const DocumentSession* s) { tabDataChanged(s, {ThumbnailRole}); }

Q_SIGNALS:
    void currentIndexChanged();
    void countChanged();
    /// The current tab is now another document (index change, or the current tab was closed or replaced).
    void currentTabChanged();

private:
    /// The tab reports to this list (and stops reporting to the one it came from).
    void listenTo(Tab& tab);
    int insertTab(Tab tab);
    int rowOf(const DocumentSession* s) const;
    void tabDataChanged(const DocumentSession* s, const QList<int>& roles);
    void backgroundChanged(int oldCurrent);

    AppContext& app;
    std::vector<Tab> tabs;
    int current = -1;
    int releaseDelayMs = 30000;
};

}  // namespace xqt
