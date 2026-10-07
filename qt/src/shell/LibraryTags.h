/*
 * xournal-qt: the "Tags" view of the library home (qt/docs/features/tags.md): the tags of the library's documents with
 * how many documents have each, nested tags folded under their parent (#course holds #course/math).
 *
 * It comes from the library's index (LibraryIndex::tagged: read into each folder's "notes" pack), so no document is
 * opened. It follows the library's "Show" filter and its Favourites filter, and, with folderOnly, the library's current
 * folder (and its subfolders). A parent counts the documents that have it or a tag inside it (each once). It is made
 * again when the index's tags change (while it is active) or when the filters do.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <set>
#include <vector>

#include <QAbstractListModel>
#include <QPointer>
#include <QStringList>
#include <QTimer>

namespace xqt {

class LibraryModel;

class LibraryTagsModel final: public QAbstractListModel {
    Q_OBJECT
    /// Rows shown (the folded ones are not)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Tags in the list (also folded ones)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    /// The view is shown: it follows the index (else it is not made at all)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// Only the documents of the library's current folder and its subfolders count
    Q_PROPERTY(bool folderOnly READ folderOnly WRITE setFolderOnly NOTIFY filtersChanged)
    /// Only tags containing this text (case ignored; their parents are shown with them)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filtersChanged)
public:
    enum Roles {
        TagRole = Qt::UserRole + 1,  ///< the whole tag ("course/math"; as first found)
        NameRole,                    ///< its last part ("math")
        DepthRole,                   ///< 0: a tag of its own, 1: inside one, ...
        CountRole,                   ///< documents with it (or a tag inside it)
        ChildrenRole,                ///< it has tags inside it
        ExpandedRole,                ///< they are shown
    };

    explicit LibraryTagsModel(LibraryModel* library, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(shown.size()); }
    int total() const { return static_cast<int>(all.size()); }
    bool active() const { return isActive; }
    void setActive(bool on);
    bool folderOnly() const { return onlyFolder; }
    void setFolderOnly(bool on);
    QString query() const { return text; }
    void setQuery(const QString& q);
    /// Show or hide the tags inside `tag`.
    Q_INVOKABLE void setExpanded(const QString& tag, bool on);
    Q_INVOKABLE void expandAll(bool on);
    /// Documents with `tag` (or one inside it) as counted now (-1: not listed)
    Q_INVOKABLE int countOf(const QString& tag) const;
    /// The tags of all indexed documents of the library, by how many documents have them (for suggestions)
    Q_INVOKABLE QStringList allTags() const;
    /// Read the index again (it is also done when its tags change).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void countChanged();
    void activeChanged();
    void filtersChanged();

private:
    struct Row {
        QString tag, name, key;
        int depth = 0;
        int count = 0;
        bool children = false;
    };
    void changedMaybe();
    void layOut();

    QPointer<LibraryModel> library;
    std::vector<Row> all;          ///< every tag, in tree order
    std::vector<size_t> shown;     ///< the rows shown (indexes into all)
    std::set<QString> expanded;    ///< keys of unfolded tags
    bool isActive = false;
    bool onlyFolder = false;
    QString text;
    quint64 seen = ~quint64(0);  ///< the index's tagChanges() when made
    QTimer poll;                 ///< (documents saved in the app change the index without a progress signal)
};

}  // namespace xqt
