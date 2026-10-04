#include "LibraryTodos.h"

#include <algorithm>

#include <QFileInfo>
#include <QLocale>

#include "DocumentPlaces.h"
#include "LibraryModel.h"

namespace xqt {

namespace {
fs::path toPath(const QString& s) { return fs::path(s.toStdString()); }

/// `file` is in `dir` or a folder of it
bool within(const fs::path& file, const fs::path& dir) {
    const fs::path rel = file.lexically_normal().lexically_relative(dir.lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}
}  // namespace

LibraryTodosModel::LibraryTodosModel(LibraryModel* library, QObject* parent):
        QAbstractListModel(parent), library(library) {
    poll.setInterval(1000);
    connect(&poll, &QTimer::timeout, this, &LibraryTodosModel::changedMaybe);
    if (library) {
        connect(library, &LibraryModel::libraryChanged, this, [this] {
            seen = ~quint64(0);
            pending.clear();
            changedMaybe();
        });
        connect(library, &LibraryModel::indexChanged, this, &LibraryTodosModel::changedMaybe);
        for (auto signal: {&LibraryModel::showChanged, &LibraryModel::favouritesOnlyChanged,
                           &LibraryModel::folderChanged}) {
            connect(library, signal, this, [this] {
                if (isActive) {
                    refresh();
                }
            });
        }
        connect(library, &LibraryModel::favouriteToggled, this, [this] {
            if (isActive) {
                refresh();
            }
        });
    }
}

void LibraryTodosModel::setActive(bool on) {
    if (on == isActive) {
        return;
    }
    isActive = on;
    Q_EMIT activeChanged();
    if (on) {
        poll.start();
        refresh();
    } else {
        poll.stop();
    }
}

void LibraryTodosModel::changedMaybe() {
    if (!isActive || !library) {
        return;
    }
    const LibraryIndex* idx = library->searchIndex();
    if (!idx || idx->todoChanges() != seen) {
        refresh();
    }
}

void LibraryTodosModel::filterChanged() {
    Q_EMIT filtersChanged();
    if (isActive) {
        refresh();
    }
}

void LibraryTodosModel::setGrouping(const QString& g) {
    const QString v = g == QLatin1String("folder") || g == QLatin1String("none") ? g : QStringLiteral("document");
    if (v != groupBy) {
        groupBy = v;
        filterChanged();
    }
}

void LibraryTodosModel::setSortBy(const QString& s) {
    const QString v = s == QLatin1String("document") || s == QLatin1String("changed") ? s : QStringLiteral("due");
    if (v != sortKey) {
        sortKey = v;
        filterChanged();
    }
}

void LibraryTodosModel::setStatus(const QString& s) {
    const QString v = s == QLatin1String("done") || s == QLatin1String("all") ? s : QStringLiteral("open");
    if (v != state) {
        state = v;
        filterChanged();
    }
}

void LibraryTodosModel::setDue(const QString& d) {
    const QString v = d == QLatin1String("overdue") || d == QLatin1String("today") || d == QLatin1String("week") ||
                                      d == QLatin1String("none")
                              ? d
                              : QStringLiteral("any");
    if (v != dueFilter) {
        dueFilter = v;
        filterChanged();
    }
}

void LibraryTodosModel::setQuery(const QString& q) {
    if (q != text) {
        text = q;
        filterChanged();
    }
}

void LibraryTodosModel::setFolderOnly(bool on) {
    if (on != inFolder) {
        inFolder = on;
        filterChanged();
    }
}

void LibraryTodosModel::setRules(const todos::Rules& r) {
    if (r.all == rules.all && r.marker == rules.marker) {
        return;
    }
    rules = r;
    Q_EMIT rulesChanged();
    if (isActive) {
        refresh();
    }
}

QDate LibraryTodosModel::today() const { return fixedToday.isValid() ? fixedToday : QDate::currentDate(); }

QString LibraryTodosModel::dueState(const QString& due, const QDate& today) {
    const QDate d = QDate::fromString(due, Qt::ISODate);
    if (!d.isValid()) {
        return {};
    }
    if (d < today) {
        return QStringLiteral("overdue");
    }
    if (d == today) {
        return QStringLiteral("today");
    }
    // This week: until the last day of the week of the locale (the day before its first day)
    const int first = static_cast<int>(QLocale().firstDayOfWeek());
    const int daysLeft = (first + 6 - today.dayOfWeek()) % 7;
    return d <= today.addDays(daysLeft) ? QStringLiteral("week") : QStringLiteral("later");
}

void LibraryTodosModel::setPending(const QString& path, const QString& rawText, int occurrence, bool done) {
    pending[{toPath(path), rawText, occurrence}] = done;
    for (size_t i = 0; i < rows.size(); ++i) {
        Row& r = rows[i];
        if (r.todo.file == toPath(path) && r.todo.text == rawText && r.todo.occurrence == occurrence) {
            r.todo.done = done;
            r.pending = true;
            const QModelIndex at = index(static_cast<int>(i));
            Q_EMIT dataChanged(at, at, {DoneRole, PendingRole});
        }
    }
}

void LibraryTodosModel::clearPending(const QString& path, const QString& rawText, int occurrence) {
    if (pending.erase({toPath(path), rawText, occurrence}) > 0 && isActive) {
        refresh();
    }
}

void LibraryTodosModel::refresh() {
    std::vector<Row> next;
    int listedCount = 0;
    const LibraryIndex* idx = library ? library->searchIndex() : nullptr;
    seen = idx ? idx->todoChanges() : 0;
    const QDate day = today();
    if (idx && library->library()) {
        const Library& lib = *library->library();
        const ShowFilter& filter = library->showFilter();
        const QString query = text.simplified();
        const fs::path folderDir = lib.root() / toPath(library->folder());
        struct Doc {
            bool shown = false;
            QString name, folder;
            qint64 changed = 0;
        };
        std::map<fs::path, Doc> docs;
        int order = 0;
        for (LibraryIndex::Todo& t: idx->todos()) {
            ++order;
            bool isPending = false;
            if (auto p = pending.find({t.file, t.text, t.occurrence}); p != pending.end()) {
                if (p->second == t.done) {
                    pending.erase(p);  // (the index has it now)
                } else {
                    t.done = p->second;
                    isPending = true;
                }
            }
            if (!todos::listed(t, rules)) {
                continue;
            }
            auto it = docs.find(t.file);
            if (it == docs.end()) {
                Doc d;
                DocumentItem item = DocumentFiles::itemOf(t.file, filter.include());
                if (!item.valid()) {
                    item = DocumentFiles::itemOf(t.file);  // (the index knows it, the filter decides below)
                }
                const bool askIndex = filter.kindMatters() && item.kind() == DocumentItem::Kind::Pdf;
                d.shown = item.valid() &&
                          filter.shows(item, askIndex ? idx->pdfKind(item.main()) : PdfKind::Unknown) &&
                          (!library->favouritesOnly() || DocumentPlaces::favourite(DocumentPlaces::keyOf(item)));
                d.name = QString::fromStdString(item.valid() ? item.name() : t.file.stem().string());
                d.folder = QString::fromStdString(lib.relative(t.file.parent_path()));
                d.changed = QFileInfo(QString::fromStdString(t.file.string())).lastModified().toMSecsSinceEpoch();
                it = docs.emplace(t.file, std::move(d)).first;
            }
            const Doc& d = it->second;
            if (!d.shown) {
                continue;
            }
            ++listedCount;
            if ((state == QLatin1String("open") && t.done) || (state == QLatin1String("done") && !t.done)) {
                continue;
            }
            const QString ds = dueState(t.due, day);
            if ((dueFilter == QLatin1String("overdue") && (ds != QLatin1String("overdue") || t.done)) ||
                (dueFilter == QLatin1String("today") && ds != QLatin1String("today")) ||
                (dueFilter == QLatin1String("week") && ds != QLatin1String("today") && ds != QLatin1String("week")) ||
                (dueFilter == QLatin1String("none") && !t.due.isEmpty())) {
                continue;
            }
            if (inFolder && !within(t.file, folderDir)) {
                continue;
            }
            Row r;
            r.shown = todos::shownText(t.text, rules.marker);
            if (!query.isEmpty() && !r.shown.contains(query, Qt::CaseInsensitive) &&
                !d.name.contains(query, Qt::CaseInsensitive) && !d.folder.contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            r.todo = std::move(t);
            r.name = d.name;
            r.folder = d.folder;
            r.changed = d.changed;
            r.dueState = ds;
            r.pending = isPending;
            r.order = order;
            next.push_back(std::move(r));
        }
        // Sorted (done ones after the open ones), then in groups in the order they first come in
        auto docLess = [](const Row& a, const Row& b) {
            if (a.todo.file != b.todo.file) {
                if (const int c = DocumentFiles::compareNames(a.name, b.name); c != 0) {
                    return c < 0;
                }
                return a.todo.file < b.todo.file;
            }
            return a.order < b.order;
        };
        std::stable_sort(next.begin(), next.end(), [&](const Row& a, const Row& b) {
            if (a.todo.done != b.todo.done) {
                return !a.todo.done;
            }
            if (sortKey == QLatin1String("due")) {
                if (a.todo.due.isEmpty() != b.todo.due.isEmpty()) {
                    return b.todo.due.isEmpty();
                }
                if (a.todo.due != b.todo.due) {
                    return a.todo.due < b.todo.due;
                }
            } else if (sortKey == QLatin1String("changed") && a.changed != b.changed) {
                return a.changed > b.changed;
            }
            return docLess(a, b);
        });
        if (groupBy != QLatin1String("none")) {
            std::vector<QString> groups;
            std::map<QString, std::vector<Row>> byGroup;
            for (Row& r: next) {
                const bool byFolder = groupBy == QLatin1String("folder");
                r.group = byFolder ? r.folder : QString::fromStdString(r.todo.file.string());
                r.groupLabel = byFolder ? (r.folder.isEmpty() ? library->name() : r.folder) : r.name;
                auto& in = byGroup[r.group];
                if (in.empty()) {
                    groups.push_back(r.group);
                }
                in.push_back(std::move(r));
            }
            next.clear();
            for (const QString& g: groups) {
                auto& in = byGroup[g];
                for (Row& r: in) {
                    r.groupCount = static_cast<int>(in.size());
                    next.push_back(std::move(r));
                }
            }
        }
    }
    beginResetModel();
    rows = std::move(next);
    totalCount = listedCount;
    endResetModel();
    Q_EMIT countChanged();
}

QVariantMap LibraryTodosModel::groupOf(const QString& group) const {
    for (const Row& r: rows) {
        if (r.group == group) {
            return {{"label", r.groupLabel},
                    {"folder", r.folder},
                    {"count", r.groupCount},
                    {"path", QString::fromStdString(r.todo.file.string())}};
        }
    }
    return {};
}

QVariantMap LibraryTodosModel::get(int row) const {
    QVariantMap m;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        m.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }
    return m;
}

QVariantList LibraryTodosModel::listed() const {
    QVariantList out;
    for (int i = 0; i < count(); ++i) {
        out.append(get(i));
    }
    return out;
}

int LibraryTodosModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant LibraryTodosModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows.size())) {
        return {};
    }
    const Row& r = rows[static_cast<size_t>(index.row())];
    switch (role) {
        case TextRole:
            return r.shown;
        case RawTextRole:
            return r.todo.text;
        case OccurrenceRole:
            return r.todo.occurrence;
        case DoneRole:
            return r.todo.done;
        case DueRole:
            return r.todo.due;
        case DueStateRole:
            return r.dueState;
        case PathRole:
            return QString::fromStdString(r.todo.file.string());
        case NameRole:
            return r.name;
        case FolderRole:
            return r.folder;
        case PageRole:
            return r.todo.page;
        case LineRole:
            return r.todo.line;
        case StampRole:
            return r.todo.stamp;
        case GroupRole:
            return r.group;
        case GroupLabelRole:
            return r.groupLabel;
        case GroupCountRole:
            return r.groupCount;
        case PendingRole:
            return r.pending;
        case BoxRole:
            return r.todo.box;
        case PlaceRole:
            return QVariantMap{{"x", r.todo.x}, {"y", r.todo.y}, {"size", r.todo.size}, {"pageWidth", r.todo.pageWidth}};
        default:
            return {};
    }
}

QHash<int, QByteArray> LibraryTodosModel::roleNames() const {
    return {{TextRole, "text"},           {RawTextRole, "rawText"},       {OccurrenceRole, "occurrence"},
            {DoneRole, "done"},           {DueRole, "due"},               {DueStateRole, "dueState"},
            {PathRole, "path"},           {NameRole, "name"},             {FolderRole, "folder"},
            {PageRole, "page"},           {LineRole, "line"},             {StampRole, "stamp"},
            {GroupRole, "group"},         {GroupLabelRole, "groupLabel"}, {GroupCountRole, "groupCount"},
            {PendingRole, "pending"},     {BoxRole, "box"},               {PlaceRole, "place"}};
}

}  // namespace xqt
