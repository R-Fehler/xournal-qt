/*
 * xournal-qt: the "Bookmarks" view of the library home (qt/docs/bookmarks.md): the bookmarked pages of all documents
 * of the library, grouped by document.
 *
 * It comes from the library's index (LibraryIndex::bookmarks: read into each folder's "notes" pack when a document is
 * indexed), so no document is opened to list them; the pictures of the pages are drawn by HitPageProvider (as the
 * pages with search hits are). It follows the library's "Show" filter, its Favourites filter and its search text
 * (a bookmark is listed when its label or its document's name contains it). It is made again when the index's
 * bookmarks change (while it is active) or when the filters do.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

#include "filesystem.h"
#include "DocumentFiles.h"

namespace xqt {

class LibraryModel;

class LibraryBookmarksModel final: public QAbstractListModel {
    Q_OBJECT
    /// Documents listed
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Bookmarks listed (in all documents)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    /// The view is shown: it follows the index (else it is not made at all)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,  ///< the document's name
        PathRole,                     ///< its main file (what a card opens)
        FolderRole,                   ///< its folder relative to the library ("": the top)
        /// Its bookmarks: [{ page (0-based), label (as shown), aspect (height / width, 0: unknown) }]
        MarksRole,
        /// Image URL of its pages (append "/<page>"; HitPageProvider)
        PageBaseRole,
        FavouriteRole,
    };

    explicit LibraryBookmarksModel(LibraryModel* library, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(rows.size()); }
    int total() const;
    bool active() const { return isActive; }
    void setActive(bool on);
    /// Read the index again (it is also done when its bookmarks change).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void countChanged();
    void activeChanged();

private:
    struct Mark {
        int page = 0;
        QString label;
        double aspect = 0;
    };
    struct Row {
        DocumentItem item;
        QString name, folder;
        std::vector<Mark> marks;
    };
    void changedMaybe();

    QPointer<LibraryModel> library;
    std::vector<Row> rows;
    bool isActive = false;
    quint64 seen = ~quint64(0);  ///< the index's bookmarkChanges() when made
    QTimer poll;                 ///< (documents saved in the app change the index without a progress signal)
};

}  // namespace xqt
