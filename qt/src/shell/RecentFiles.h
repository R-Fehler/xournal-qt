/*
 * xournal-qt: recently opened documents (the "Recent" grid of the home screen).
 *
 * The list is shared by all windows (libraries) and stored as JSON in the config folder; every change reads it
 * again first, so windows do not drop each other's entries. The grid shows the documents that still exist, one
 * entry per document (a .xopp and its PDF are one), and the folders opened as a library outside the standard
 * folder of libraries that still exist, all by when they were opened. A library row is opened, shown in the file
 * manager or removed from the list, never selected, renamed, moved or trashed from here.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <vector>

#include <QAbstractListModel>
#include <QDateTime>

#include "filesystem.h"
#include "DocumentFiles.h"
#include "GridSelection.h"

namespace xqt {

class RecentFiles final: public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        LocationRole,
        PreviewRole,
        OpenedRole,
        HasPdfRole,
        HasXoppRole,
        SelectedRole,
        LastPageRole,  ///< the page the document was left at (-1: not known)
        KindRole,      ///< "notes", "pdf", "md", "image", "text" (DocumentItem::kindName); a library: "library"
        IsLibraryRole, ///< a folder opened as a library
        PdfKindRole,   ///< a document whose file is a PDF: what it is, where the library knows it (pdfKindName; else "")
    };
    static constexpr int MAX_ENTRIES = 100;

    explicit RecentFiles(fs::path storeFile, QObject* parent = nullptr);
    static fs::path defaultStoreFile();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(rows.size()); }
    int selectionCount() const { return static_cast<int>(selection.paths.size()); }

    /// A document was opened or saved.
    void add(const fs::path& file);
    /// A folder was opened as a library.
    void addLibrary(const fs::path& folder);
    /// A file or folder has a new path.
    void remap(const fs::path& from, const fs::path& to);
    /// What a PDF is, where it is known (the library index: set by the controller; not set: not known).
    void setPdfKinds(std::function<PdfKind(const fs::path&)> lookup);
    /// The kinds known may have changed (the index read more): the cards show them anew.
    void pdfKindsChanged();
    /// Files were renamed, moved or trashed (open tabs follow).
    std::function<void(const DocumentFiles::Result&)> onFilesChanged;

    /// Read the list again and check which documents still exist.
    Q_INVOKABLE void refresh();
    /// Remove from the list (the files stay).
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void clear();
    /// Rename the document (its .xopp and PDF).
    Q_INVOKABLE bool rename(int row, const QString& name);
    Q_INVOKABLE bool trash(int row);

    // --- selection (see LibraryModel) ---
    Q_INVOKABLE void select(int row, int modifiers);
    Q_INVOKABLE void toggleSelected(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QStringList selectedPaths() const;
    Q_INVOKABLE QStringList pathsFor(int row) const;
    /// Remove documents from the list (the files stay).
    Q_INVOKABLE void removePaths(const QStringList& paths);

Q_SIGNALS:
    void countChanged();
    void selectionChanged();
    void error(const QString& text);

private:
    struct Entry {
        fs::path path;
        QDateTime opened;
        bool library = false;  ///< a folder opened as a library
    };
    std::vector<Entry> load() const;
    void store(const std::vector<Entry>& entries) const;
    struct Row {
        DocumentItem item;
        QDateTime opened;
        fs::path library;  ///< a library row: its folder
        const fs::path& path() const { return library.empty() ? item.main() : library; }
    };

    void selectionUpdated();
    /// Library rows are never selected.
    void dropLibraries();

    fs::path storeFile;
    std::function<PdfKind(const fs::path&)> pdfKinds;
    std::vector<Row> rows;
    GridSelection selection;
};

}  // namespace xqt
