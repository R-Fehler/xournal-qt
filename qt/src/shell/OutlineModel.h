/*
 * xournal-qt: the table of contents of the current document (the PDF outline, upstream's DocumentOutline), as a
 * flat list for the contents sidebar and the contents overview.
 *
 * Every entry has its level, its document page (the first page showing its PDF page; -1 if the document has no
 * such page) and, for the overview, its pages: from its page up to the next visible entry's page, so every page
 * appears once, under the most specific entry shown (a collapsed chapter has all pages of its sections). Pages
 * before the first entry get an entry "Beginning". Entries can be collapsed; they start as the PDF says.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "model/DocumentListener.h"

namespace xqt {

class DocumentSession;

class OutlineModel final: public QAbstractListModel, public DocumentListener {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// The document has a table of contents.
    Q_PROPERTY(bool available READ available NOTIFY countChanged)
    /// The visible entry the current page belongs to (-1: none)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        LevelRole,        ///< 0: top level
        PageRole,         ///< 0-based document page, -1: not in the document
        PageEndRole,      ///< its pages: [page, pageEnd)
        HasChildrenRole,
        ExpandedRole,
    };

    explicit OutlineModel(QObject* parent = nullptr);
    ~OutlineModel() override;

    void setSession(DocumentSession* session);
    /// Read the table of contents again (the outline of the PDF, else the chapters written in the document).
    void rebuild();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(visible.size()); }
    bool available() const { return !all.empty(); }
    int currentRow() const { return current; }

    /// Collapse / expand the entry in `row`.
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void expandAll(bool expand);

    // DocumentListener: the pages of the entries change with the document's pages
    void documentChanged(DocumentChangeType type) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;

Q_SIGNALS:
    void countChanged();
    void currentRowChanged();

private:
    struct Entry {
        QString title;
        int level = 0;
        size_t pdfPage = 0;
        int page = -1;
        bool hasChildren = false;
        bool expanded = true;
        bool synthetic = false;  ///< "Beginning"
    };
    /// Read the outline of the document (keeps the expanded state if it is the same outline).
    /// The document pages of the entries (after page changes).
    void updatePages();
    /// The visible entries and their page ranges.
    void relayout();
    void updateCurrent();

    DocumentSession* session = nullptr;
    std::vector<Entry> all;
    QMetaObject::Connection contentConnection;
    bool ownChapters = false;  ///< the chapters come from the document, not from a PDF outline       ///< document order (depth first)
    std::vector<size_t> visible;  ///< indices in `all`
    std::vector<int> ends;        ///< per visible row
    int current = -1;
    QMetaObject::Connection pageConnection;
};

}  // namespace xqt
