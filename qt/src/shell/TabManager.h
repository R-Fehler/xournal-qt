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
    enum Roles { TitleRole = Qt::UserRole + 1, ModifiedRole, FilePathRole, CurrentRole, ThumbnailRole, PageCountRole,
                 SearchHitsRole, SearchRunningRole };

    explicit TabManager(AppContext& app, QObject* parent = nullptr);
    ~TabManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(tabs.size()); }
    int currentIndex() const { return current; }
    void setCurrentIndex(int index);

    /// Adds a tab after the current one and makes it current. Returns its index.
    int addTab(std::unique_ptr<DocumentSession> session);
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

Q_SIGNALS:
    void currentIndexChanged();
    void countChanged();
    /// The current tab is now another document (index change, or the current tab was closed or replaced).
    void currentTabChanged();

private:
    struct Tab {
        std::unique_ptr<DocumentSession> session;
        std::unique_ptr<CanvasView> view;
        std::unique_ptr<QTimer> releaseTimer;
        quint64 thumbnailRevision = 0;  ///< increased when the current page or its content changed
    };
    int rowOf(const DocumentSession* s) const;
    void tabDataChanged(const DocumentSession* s, const QList<int>& roles);
    void backgroundChanged(int oldCurrent);

    AppContext& app;
    std::vector<Tab> tabs;
    int current = -1;
    int releaseDelayMs = 30000;
};

}  // namespace xqt
