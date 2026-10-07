#include "LibraryTags.h"

#include <algorithm>
#include <map>

#include "session/Tags.h"

#include "DocumentPlaces.h"
#include "LibraryIndex.h"
#include "LibraryModel.h"

namespace xqt {

LibraryTagsModel::LibraryTagsModel(LibraryModel* library, QObject* parent):
        QAbstractListModel(parent), library(library) {
    poll.setInterval(1000);
    connect(&poll, &QTimer::timeout, this, &LibraryTagsModel::changedMaybe);
    if (library) {
        connect(library, &LibraryModel::libraryChanged, this, [this] {
            seen = ~quint64(0);
            changedMaybe();
        });
        connect(library, &LibraryModel::indexChanged, this, &LibraryTagsModel::changedMaybe);
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

void LibraryTagsModel::setActive(bool on) {
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

void LibraryTagsModel::setFolderOnly(bool on) {
    if (on != onlyFolder) {
        onlyFolder = on;
        Q_EMIT filtersChanged();
        refresh();
    }
}

void LibraryTagsModel::setQuery(const QString& q) {
    if (q != text) {
        text = q;
        Q_EMIT filtersChanged();
        layOut();
    }
}

void LibraryTagsModel::changedMaybe() {
    if (!isActive || !library) {
        return;
    }
    const LibraryIndex* idx = library->searchIndex();
    if (!idx || idx->tagChanges() != seen) {
        refresh();
    }
}

void LibraryTagsModel::refresh() {
    std::vector<Row> next;
    const LibraryIndex* idx = library ? library->searchIndex() : nullptr;
    seen = idx ? idx->tagChanges() : 0;
    if (idx && library->library()) {
        const ShowFilter& filter = library->showFilter();
        const fs::path folder = library->library()->root() / fs::path(library->folder().toStdString());
        // Per tag (and each parent of it): the documents with it, and how it is spelled
        struct Count {
            QString tag;
            std::set<fs::path> docs;
        };
        std::map<QString, Count> byKey;
        for (const auto& d: idx->tagged()) {
            if (onlyFolder) {
                const fs::path rel = d.file.parent_path().lexically_relative(folder);
                if (rel.empty() || *rel.begin() == "..") {
                    continue;
                }
            }
            DocumentItem item = DocumentFiles::itemOf(d.file, filter.include());
            const bool askIndex = filter.kindMatters() && item.kind() == DocumentItem::Kind::Pdf;
            if (!item.valid() || !filter.shows(item, askIndex ? idx->pdfKind(item.main()) : PdfKind::Unknown) ||
                (library->favouritesOnly() && !DocumentPlaces::favourite(DocumentPlaces::keyOf(item)))) {
                continue;
            }
            for (const QString& tag: d.tags) {
                const QStringList parts = tag.split(u'/');
                for (qsizetype n = 1; n <= parts.size(); ++n) {
                    const QString sub = parts.mid(0, n).join(u'/');
                    Count& c = byKey[tags::key(sub)];
                    if (c.tag.isEmpty()) {
                        c.tag = sub;
                    }
                    c.docs.insert(d.file);
                }
            }
        }
        // In tree order: a parent before the tags inside it, siblings by name (case ignored)
        std::vector<std::pair<QString, const Count*>> ordered;
        for (const auto& [key, c]: byKey) {
            ordered.emplace_back(key, &c);
        }
        auto sortKey = [](const QString& key) {
            QString k = key;
            return k.replace(u'/', QChar(1));  // (a parent's children right after it)
        };
        std::sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) {
            return sortKey(a.first) < sortKey(b.first);  // (the keys are case folded)
        });
        for (size_t i = 0; i < ordered.size(); ++i) {
            const auto& [key, c] = ordered[i];
            Row r;
            r.tag = c->tag;
            r.key = key;
            r.depth = static_cast<int>(c->tag.count(u'/'));
            r.name = c->tag.section(u'/', -1);
            r.count = static_cast<int>(c->docs.size());
            r.children = i + 1 < ordered.size() && ordered[i + 1].first.startsWith(key + u'/');
            next.push_back(std::move(r));
        }
    }
    all = std::move(next);
    layOut();
}

void LibraryTagsModel::layOut() {
    std::vector<size_t> next;
    for (size_t i = 0; i < all.size(); ++i) {
        const Row& r = all[i];
        if (!text.isEmpty()) {
            // Searched: the tags that contain the text, with their parents (all unfolded)
            const bool hit = std::any_of(all.begin() + static_cast<std::ptrdiff_t>(i), all.end(), [&](const Row& o) {
                return (o.key == r.key || o.key.startsWith(r.key + u'/')) && o.tag.contains(text, Qt::CaseInsensitive);
            });
            if (hit) {
                next.push_back(i);
            }
            continue;
        }
        // Folded: shown when every parent is unfolded
        bool visible = true;
        for (qsizetype slash = r.key.indexOf(u'/'); slash >= 0 && visible; slash = r.key.indexOf(u'/', slash + 1)) {
            visible = expanded.count(r.key.left(slash)) > 0;
        }
        if (visible) {
            next.push_back(i);
        }
    }
    beginResetModel();
    shown = std::move(next);
    endResetModel();
    Q_EMIT countChanged();
}

void LibraryTagsModel::setExpanded(const QString& tag, bool on) {
    const QString key = tags::key(tag);
    if ((expanded.count(key) > 0) == on) {
        return;
    }
    if (on) {
        expanded.insert(key);
    } else {
        expanded.erase(key);
    }
    layOut();
}

void LibraryTagsModel::expandAll(bool on) {
    expanded.clear();
    if (on) {
        for (const Row& r: all) {
            if (r.children) {
                expanded.insert(r.key);
            }
        }
    }
    layOut();
}

int LibraryTagsModel::countOf(const QString& tag) const {
    const QString key = tags::key(tag);
    for (const Row& r: all) {
        if (r.key == key) {
            return r.count;
        }
    }
    return -1;
}

QStringList LibraryTagsModel::allTags() const {
    const LibraryIndex* idx = library ? library->searchIndex() : nullptr;
    if (!idx) {
        return {};
    }
    std::map<QString, std::pair<QString, int>> byKey;
    for (const auto& d: idx->tagged()) {
        for (const QString& t: d.tags) {
            auto& [spelling, n] = byKey[tags::key(t)];
            if (spelling.isEmpty()) {
                spelling = t;
            }
            ++n;
        }
    }
    std::vector<std::pair<QString, int>> list;
    for (const auto& [key, v]: byKey) {
        list.push_back(v);
    }
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QStringList out;
    for (const auto& [t, n]: list) {
        out << t;
    }
    return out;
}

int LibraryTagsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(shown.size());
}

QVariant LibraryTagsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(shown.size())) {
        return {};
    }
    const Row& r = all[shown[static_cast<size_t>(index.row())]];
    switch (role) {
        case TagRole:
            return r.tag;
        case NameRole:
            return r.name;
        case DepthRole:
            return r.depth;
        case CountRole:
            return r.count;
        case ChildrenRole:
            return r.children;
        case ExpandedRole:
            return !text.isEmpty() || expanded.count(r.key) > 0;
        default:
            return {};
    }
}

QHash<int, QByteArray> LibraryTagsModel::roleNames() const {
    return {{TagRole, "tag"},       {NameRole, "name"},         {DepthRole, "depth"},
            {CountRole, "count"},   {ChildrenRole, "children"}, {ExpandedRole, "expanded"}};
}

}  // namespace xqt
