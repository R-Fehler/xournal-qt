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
#include <set>
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
    /// Hit marks on one page preview of the extended search (a one-letter search has hundreds)
    static constexpr int MAX_PAGE_HITS = 50;
    /// The pages with hits of a document whose hits are placed for the overview (the first ones).
    static constexpr int PLACED_HIT_PAGES = 24;
    enum Roles { TitleRole = Qt::UserRole + 1, ModifiedRole, FilePathRole, CurrentRole, ThumbnailRole, PageCountRole,
                 SearchHitsRole, SearchRunningRole,
                 /// The pages with search hits, for the extended search of the overview:
                 /// [{ page, count, aspect, thumbnail, rects: [normalized hit rects, at most 50] }]
                 HitPagesRole,
                 /// URL of the sketch of the current page (PageSketches), shown under the thumbnail; may be empty
                 SketchRole,
                 /// The document is being saved (in the background)
                 SavingRole,
                 /// The document matches its search: it has hits; a fuzzy search: its expression holds with the terms
                 /// in the title or the text (DocumentSearch::matches)
                 SearchMatchRole,
                 /// The document is shown as the reference beside the current tab's document (reference mode)
                 ReferenceRole };

    explicit TabManager(AppContext& app, QObject* parent = nullptr);
    ~TabManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(tabs.size()); }
    int currentIndex() const { return current; }
    void setCurrentIndex(int index);

    /// One open document: its session and the view showing it.
    struct Tab {
        std::unique_ptr<DocumentSession> session;
        std::unique_ptr<CanvasView> view;
        /// Reference mode: the document of another tab shown beside this one (for reading), or none
        DocumentSession* reference = nullptr;
        /// ... and whether it is written in there (off: for reading only)
        bool referenceEditable = false;
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
    /// The tab of this session, or -1.
    int indexOf(const DocumentSession* s) const { return rowOf(s); }
    /// A document of this list is being saved.
    bool anySaving() const;
    CanvasView* view(int index) const;
    DocumentSession* currentSession() const { return session(current); }
    CanvasView* currentView() const { return view(current); }

    // --- reference mode: each tab may show another tab's document beside its own ---
    /// The tab shown beside tab `index` (-1: none).
    int referenceOf(int index) const;
    /// Show tab `reference` beside tab `index` (-1: no reference). A tab is not its own reference.
    void setReference(int index, int reference);
    /// The current tab's document and its reference change places: the reference becomes the current tab, with the
    /// document it was shown beside as its reference.
    void swapReference();
    /// Whether tab `index` writes in its reference (the edit switch of the reference's pill; off for a new one).
    bool referenceEditable(int index) const;
    void setReferenceEditable(int index, bool on);

    /// The picture of a tab may be another one now (its title page was chosen).
    void thumbnailChanged(const DocumentSession* s) { tabDataChanged(s, {ThumbnailRole}); }

Q_SIGNALS:
    void currentIndexChanged();
    void countChanged();
    /// The current tab is now another document (index change, or the current tab was closed or replaced).
    void currentTabChanged();
    /// A document started or finished saving.
    void savingChanged();
    /// Pasted PDF pages could not be added to a document's merged PDF (DocumentSession::pdfPagesFailed).
    void pdfPagesFailed(const QString& error);
    /// A tab got another reference, or lost it (its reference was closed or moved to another window).
    void referencesChanged();

private:
    /// The tab reports to this list (and stops reporting to the one it came from).
    void listenTo(Tab& tab);
    int insertTab(Tab tab);
    int rowOf(const DocumentSession* s) const;
    void tabDataChanged(const DocumentSession* s, const QList<int>& roles);
    void backgroundChanged(int oldCurrent);
    /// A tab goes: no tab shows it as its reference any more.
    void forgetReferencesTo(const DocumentSession* s);
    /// Which tab is marked as the reference of the current one (ReferenceRole)
    void referenceMarksChanged();

    /// Hits found while a search runs are told in one go now and then (rebuilding the list of pages with hits
    /// makes the overview build its previews again).
    void searchChanged(const DocumentSession* s, bool finished);

    AppContext& app;
    std::vector<Tab> tabs;
    QTimer searchRefresh;
    std::set<const DocumentSession*> searchPending;
    int current = -1;
};

}  // namespace xqt
