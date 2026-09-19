/*
 * xournal-qt: recently opened documents (the "Recent" grid of the home screen).
 *
 * The list is shared by all windows (libraries) and stored as JSON in the config folder; every change reads it
 * again first, so windows do not drop each other's entries. The grid shows the documents that still exist, one
 * entry per document (a .xopp and its PDF are one).
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
        SelectedRole
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
    /// A file or folder has a new path.
    void remap(const fs::path& from, const fs::path& to);
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
    };
    std::vector<Entry> load() const;
    void store(const std::vector<Entry>& entries) const;
    struct Row {
        DocumentItem item;
        QDateTime opened;
    };

    void selectionUpdated();

    fs::path storeFile;
    std::vector<Row> rows;
    GridSelection selection;
};

}  // namespace xqt
