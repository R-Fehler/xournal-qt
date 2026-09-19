/*
 * xournal-qt: the pages of the current document for the sidebar and the page grid, optionally only those with
 * search hits ("show only pages with hits", to skim long PDFs).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QSortFilterProxyModel>

namespace xqt {

class PagesModel;

class PageFilterModel final: public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(bool onlySearchHits READ onlySearchHits WRITE setOnlySearchHits NOTIFY onlySearchHitsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit PageFilterModel(PagesModel& pages, QObject* parent = nullptr);

    bool onlySearchHits() const { return filterHits; }
    void setOnlySearchHits(bool only);
    int count() const { return rowCount(); }
    /// Row of a page in this model, or -1 if it is filtered out.
    Q_INVOKABLE int rowOf(int page) const;

Q_SIGNALS:
    void onlySearchHitsChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    bool filterHits = false;
};

}  // namespace xqt
