#include "PageFilterModel.h"

#include "PagesModel.h"

namespace xqt {

PageFilterModel::PageFilterModel(PagesModel& pages, QObject* parent): QSortFilterProxyModel(parent) {
    setSourceModel(&pages);
    // New search results: filter again (only when filtering, the rest is plain data changes).
    connect(&pages, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                if (filterHits && roles.contains(PagesModel::SearchHitCountRole)) {
                    invalidateRowsFilter();
                }
            });
    for (auto sig: {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(this, sig, this, &PageFilterModel::countChanged);
    }
    connect(this, &QAbstractItemModel::modelReset, this, &PageFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &PageFilterModel::countChanged);
}

void PageFilterModel::setOnlySearchHits(bool only) {
    if (only == filterHits) {
        return;
    }
    filterHits = only;
    invalidateRowsFilter();
    Q_EMIT onlySearchHitsChanged();
    Q_EMIT countChanged();
}

int PageFilterModel::rowOf(int page) const {
    if (!sourceModel() || page < 0 || page >= sourceModel()->rowCount()) {
        return -1;
    }
    const QModelIndex i = mapFromSource(sourceModel()->index(page, 0));
    return i.isValid() ? i.row() : -1;
}

bool PageFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (!filterHits) {
        return true;
    }
    return sourceModel()->index(sourceRow, 0, sourceParent).data(PagesModel::SearchHitCountRole).toInt() > 0;
}

}  // namespace xqt
