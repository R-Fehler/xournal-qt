/*
 * xournal-qt: the library grid (QML list model) and its file operations.
 *
 * Rows are the subfolders and documents of the current folder, all documents of the library ("flat"), or the
 * search results: folders whose name matches, then documents whose text or name matches. What kinds of files are
 * shown is the library's "Show" filter (ShowFilter, a setting of each library): it applies to the grid, the counts of
 * the folders and the search results. Text files are in the search index when shown; other files are found by
 * name. Changes on disk (also by other programs) are picked up by a file system watcher.
 * Renaming and moving go through DocumentFiles (a .xopp and its PDF stay together); `onFilesChanged` lets the
 * controller update open tabs and the recent files.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include "filesystem.h"
#include "DocumentFiles.h"
#include "GridSelection.h"
#include "Library.h"

class QFileSystemWatcher;

namespace xqt {

class LibraryModel final: public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY libraryChanged)
    Q_PROPERTY(QString name READ name NOTIFY libraryChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY libraryChanged)
    /// The library is the Downloads folder (or in it): short-lived files, imports get a warning
    Q_PROPERTY(bool temporary READ temporary NOTIFY libraryChanged)
    /// Current folder, relative to the library ("" = the library itself)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    /// [{ name, folder }] from the library down to the current folder
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY folderChanged)
    /// All documents of the library in one grid, without folders
    Q_PROPERTY(bool flat READ flat WRITE setFlat NOTIFY flatChanged)
    /// The search looks at names only: of the documents, and of the folders when they are shown (not in the flat
    /// list) - not at what is written in the documents.
    Q_PROPERTY(bool namesOnly READ namesOnly WRITE setNamesOnly NOTIFY namesOnlyChanged)
    /// "name" or "modified"
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)
    /// The search reads the query with fzf's extended syntax (FuzzyQuery.h): names fuzzy and ranked, `|`, `!`,
    /// parentheses, ... An app-wide setting (the controller keeps it), shared by all windows and the tab overview.
    Q_PROPERTY(bool fuzzySearch READ fuzzySearch WRITE setFuzzySearch NOTIFY fuzzySearchChanged)
    /// Fuzzy search: why the query is searched as plain text ("": it is not)
    Q_PROPERTY(QString searchHint READ searchHint NOTIFY searchChanged)
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexChanged)
    Q_PROPERTY(int indexed READ indexed NOTIFY indexChanged)
    Q_PROPERTY(int indexTotal READ indexTotal NOTIFY indexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Documents are being imported / copied
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    /// Selected documents and folders (several can be opened, copied, moved, trashed at once)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    /// The library keeps its cache in the app's cache folder, not in hidden folders in its folders (a setting of
    /// the library; changing it moves the cache)
    Q_PROPERTY(bool cacheInAppCache READ cacheInAppCache WRITE setCacheInAppCache NOTIFY cacheChanged)
    /// Its folder in the app's cache
    Q_PROPERTY(QString appCachePath READ appCachePath NOTIFY cacheChanged)
    /// What its cache takes on disk (-1: not counted yet, see measureCache()), and in how many files
    Q_PROPERTY(double cacheBytes READ cacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(int cacheFiles READ cacheFiles NOTIFY cacheChanged)
    /// Its cache was removed: nothing is cached any more until the library is opened again
    Q_PROPERTY(bool cacheRemoved READ cacheRemoved NOTIFY cacheChanged)
    /// Which kinds of files are shown: { notes, pdfs, onlyPdfsWithNotes, markdown, images, text, other } (bools; see
    /// setShown)
    Q_PROPERTY(QVariantMap show READ show NOTIFY showChanged)
    /// The filter is not the default one (the "Show" button is marked)
    Q_PROPERTY(bool showFiltered READ showFiltered NOTIFY showChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsFolderRole,
        PathRole,          ///< absolute path of the folder, or of the document's main file (.xopp, else PDF)
        LocationRole,      ///< folder of the row relative to the library ("" = top)
        PreviewRole,       ///< image URL
        ModifiedRole,
        HasPdfRole,
        HasXoppRole,
        PageCountRole,     ///< -1: not known yet
        HitsRole,          ///< search matches in the text
        HitPagesRole,
        FirstHitPageRole,
        NameMatchRole,
        SnippetRole,
        ItemCountRole,     ///< folders: documents and folders in it
        SelectedRole,
        LastReadRole,      ///< when the document was last read in this app (null: never)
        LastPageRole,      ///< the page it was left at (-1: not known)
        /// Search: the pages with hits, [{ page, count, aspect }]
        HitPageListRole,
        /// Search: image URL of the pages with hits marked (append "/<page>")
        HitPageBaseRole,
        /// What the document is: "notes" (a .xopp alone), "pdf" (with or without its .xopp; also a hybrid PDF), "md",
        /// "image" (with or without its .xopp); folders: ""
        KindRole,
        /// A hybrid PDF with its .xopp export (one card that opens the PDF). A lone hybrid PDF is not looked into
        /// when listing: false.
        HybridRole,
        /// Search in a Markdown file: its passages with hits, [{ passage, count, headings }] (headings: the path of
        /// the headings above it, "Lecture 3 › Kalman filter")
        HitPassageListRole,
        /// Search: image URL of the snippet cards of those passages (append "/<passage>"), see MdSnippets.h
        HitPassageBaseRole,
        /// Size of the document's main file in bytes (folders: -1)
        SizeRole,
        /// A text or other file: the icon for its type ("xqt-file-spreadsheet", ...); else ""
        FileIconRole,
        /// Fuzzy search: the characters of the name that matched, [index, ...] (to highlight them)
        NameMarksRole,
    };

    explicit LibraryModel(QObject* parent = nullptr);
    ~LibraryModel() override;

    /// Show this library (nullptr: none).
    void setLibrary(std::unique_ptr<Library> library);
    const Library* library() const { return lib.get(); }
    LibraryIndex* searchIndex() const { return idx.get(); }
    /// Files were renamed, moved or trashed (open tabs, recent files follow).
    std::function<void(const DocumentFiles::Result&)> onFilesChanged;
    /// Files were renamed or moved elsewhere in the app (recent files): the search index keeps their entries.
    void filesMoved(const DocumentFiles::Result& r);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return lib != nullptr; }
    QString name() const;
    QString rootPath() const;
    bool temporary() const { return lib && lib->isTemporary(); }
    /// A folder in the Downloads folder (a target for copies that gets a warning).
    Q_INVOKABLE bool isTemporaryFolder(const QString& path) const;
    QString folder() const { return currentFolder; }
    void setFolder(const QString& folder);
    QVariantList breadcrumbs() const;
    bool flat() const { return flatView; }
    void setFlat(bool flat);
    bool namesOnly() const { return onlyNames; }
    void setNamesOnly(bool namesOnly);
    QString sortBy() const { return sortKey; }
    void setSortBy(const QString& key);
    QString searchQuery() const { return query; }
    void setSearchQuery(const QString& query);
    bool fuzzySearch() const { return fuzzy; }
    void setFuzzySearch(bool on);
    QString searchHint() const;
    bool indexing() const;
    int indexed() const;
    int indexTotal() const;
    int count() const { return static_cast<int>(rows.size()); }
    bool importing() const { return importJobs > 0; }
    int selectionCount() const { return static_cast<int>(selection.paths.size()); }
    bool cacheInAppCache() const;
    void setCacheInAppCache(bool inAppCache);
    QString appCachePath() const;
    double cacheBytes() const { return static_cast<double>(cacheUsage.bytes); }
    int cacheFiles() const { return cacheUsage.files; }
    bool cacheRemoved() const { return cachesRemoved; }
    /// Count what the cache takes (in the background; cacheChanged when done).
    Q_INVOKABLE void measureCache();
    /// Remove every cache folder of the library (only the files the app recognises as its own) and its folder in
    /// the app cache. The reading positions stay (they are in the config folder). Nothing is cached or indexed any
    /// more until the library is opened again: the app closes after this, so it does not build them again at once.
    /// Returns the bytes removed.
    Q_INVOKABLE qint64 removeCaches();

    // --- selection ---
    /// A click with modifiers: alone selects only this row, Ctrl toggles it, Shift selects a range.
    Q_INVOKABLE void select(int row, int modifiers);
    Q_INVOKABLE void toggleSelected(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    /// Selected paths (documents' main files, folders) in the order of the grid.
    Q_INVOKABLE QStringList selectedPaths() const;
    /// The row and, if the row is selected, the rest of the selection: what an action on this row applies to.
    Q_INVOKABLE QStringList pathsFor(int row) const;
    /// Documents in `paths` (folders left out), e.g. to open them.
    Q_INVOKABLE QStringList documentsIn(const QStringList& paths) const;

    /// Copy or move documents and folders (absolute paths: the selection, recent documents) into a folder of the
    /// library (relative). Copies are made in the background, like imports. A document keeps its PDF.
    Q_INVOKABLE bool transfer(const QStringList& paths, const QString& folder, bool copy);
    /// The same into any folder (absolute), e.g. of another library.
    Q_INVOKABLE bool transferTo(const QStringList& paths, const QString& folder, bool copy);
    /// The folders of a library (its root first): [{ folder (relative), name, depth, path (absolute) }], e.g. of
    /// another library as a target for "Move to" / "Copy to".
    Q_INVOKABLE QVariantList foldersOf(const QString& root) const;
    /// Move documents and folders to the trash.
    Q_INVOKABLE bool trashPaths(const QStringList& paths);

    const ShowFilter& showFilter() const { return filter; }
    /// Show these kinds of files (kept as the library's setting).
    void setShowFilter(const ShowFilter& f);
    QVariantMap show() const;
    bool showFiltered() const { return !filter.isDefault(); }
    /// Show a kind of files or not: "notes", "pdfs", "onlyPdfsWithNotes", "markdown", "images", "text", "other".
    Q_INVOKABLE void setShown(const QString& key, bool shown);
    /// The default filter again.
    Q_INVOKABLE void resetShown();
    /// The icon for a file that is not a document, by its type (MIME): "xqt-file-doc", "xqt-file-spreadsheet",
    /// "xqt-file-slides", "xqt-file-archive", "xqt-file-audio", "xqt-file-video", "xqt-file-image", "xqt-file-code",
    /// else "xqt-file".
    static QString fileIconOf(const fs::path& file);

    /// Scan the library again (after changes).
    Q_INVOKABLE void refresh();
    /// Documents were read meanwhile (when, and at which page): shown anew, sorted anew if by that.
    void placesChanged();
    Q_INVOKABLE void goUp();
    /// New folder in the current folder.
    Q_INVOKABLE bool createFolder(const QString& name);
    Q_INVOKABLE bool rename(int row, const QString& name);
    /// Move a document or folder into a folder (relative to the library).
    Q_INVOKABLE bool moveTo(int row, const QString& folder);
    Q_INVOKABLE bool trash(int row);
    /// Copy documents, or folders with their whole folder structure, into a folder of the library (relative; in the
    /// background). `imported` tells the number of documents.
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls, const QString& folder);
    /// All folders of the library: [{ folder, name, depth }], for "Move to".
    Q_INVOKABLE QVariantList folderList() const;
    /// A free path for a new document "<name>.xopp" in the current folder.
    Q_INVOKABLE QString newDocumentPath(const QString& name) const;
    /// The row shows this file (tests, QML)
    Q_INVOKABLE int rowOf(const QString& path) const;
    /// Whether a URL points into the library (drops from the grid itself move instead of copy).
    Q_INVOKABLE bool contains(const QUrl& url) const;
    /// Relative folder of an absolute path in the library.
    Q_INVOKABLE QString relativeFolder(const QString& path) const;

Q_SIGNALS:
    void libraryChanged();
    void folderChanged();
    void flatChanged();
    void namesOnlyChanged();
    void fuzzySearchChanged();
    void sortByChanged();
    void searchChanged();
    void indexChanged();
    void countChanged();
    void importingChanged();
    void selectionChanged();
    void cacheChanged();
    void showChanged();
    void error(const QString& text);
    /// An import finished: `count` documents were added.
    void imported(int count);

private:
    struct Row {
        bool isFolder = false;
        fs::path path;  ///< folder, or the document's main file
        DocumentItem item;
        QString name;
        QDateTime modified;
        int itemCount = 0;
        qint64 size = -1;
        QString icon;  ///< a text or other file: its type's icon
        LibraryIndex::Hit hit;
    };
    fs::path currentDir() const;
    /// The index and the previews, where the library keeps its cache.
    void openCache();
    /// The library and all its folders.
    std::vector<fs::path> allFolders() const;
    fs::path dirOf(const QString& relative) const;
    void rebuild();
    static Row itemRow(const DocumentItem& item);
    /// A folder, with the number of items in it that are shown
    Row folderRow(const fs::path& folder) const;
    /// The rows of a fuzzy search (a valid query)
    std::vector<Row> fuzzyRows(const FuzzyQuery& parsed);
    void updateSearch();
    void watchFolders(const std::vector<fs::path>& folders);
    void applyResult(const DocumentFiles::Result& r);
    /// Files moved by the app: the index, the reading places and the previews follow.
    void followMoves(const std::vector<std::pair<fs::path, fs::path>>& moves);
    void setRows(std::vector<Row> newRows);
    void copyInBackground(std::vector<fs::path> files, fs::path target);
    void selectionUpdated();

    std::unique_ptr<Library> lib;
    std::unique_ptr<LibraryIndex> idx;
    std::unique_ptr<QFileSystemWatcher> watcher;
    QTimer refreshTimer;  ///< changes on disk come in bursts
    QTimer searchTimer;   ///< search again while indexing
    std::vector<Row> rows;
    QString currentFolder;
    bool flatView = false;
    bool onlyNames = false;
    QString sortKey = "name";
    QString query;
    bool fuzzy = false;
    QString marks;  ///< what the pictures of the pages with hits mark: the query, or its terms (HitPages.h)
    int importJobs = 0;
    GridSelection selection;
    CacheFolders::Usage cacheUsage{-1, 0};
    quint64 cacheCounts = 0;  ///< (only the last count counts)
    bool cachesRemoved = false;
    ShowFilter filter;
};

}  // namespace xqt
