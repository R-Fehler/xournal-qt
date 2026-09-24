#include "DocumentPlaces.h"

#include <algorithm>
#include <mutex>

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "util/PathUtil.h"

#include "DocumentFiles.h"

namespace xqt::DocumentPlaces {

namespace {
struct Store {
    fs::path file;
    QJsonObject entries;
    bool loaded = false;

    QJsonObject& load() {
        if (!loaded) {
            loaded = true;
            QFile f(QString::fromStdString(file.string()));
            if (!file.empty() && f.open(QIODevice::ReadOnly)) {
                entries = QJsonDocument::fromJson(f.readAll()).object();
            }
        }
        return entries;
    }
    void save() {
        if (file.empty()) {
            return;
        }
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        QSaveFile f(QString::fromStdString(file.string()));  // (written in full or not at all)
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(entries).toJson(QJsonDocument::Compact));
            f.commit();
        }
    }
};

struct State {
    std::mutex mtx;
    fs::path root;
    Store library;
    Store outside;
    bool outsideSet = false;
};
State& state() {
    static State s;
    return s;
}

/// The store of a document and its key there (the lock is held by the caller).
std::pair<Store*, QString> find(State& s, const fs::path& document) {
    if (!s.root.empty() && DocumentFiles::remap(document, s.root, "/") != document) {
        return {&s.library, QString::fromStdString(document.lexically_normal().lexically_relative(s.root).string())};
    }
    if (!s.outsideSet) {
        s.outside.file = Util::getCacheSubfolder("documents") / "pages.json";
        s.outsideSet = true;
    }
    return {&s.outside, QString::fromStdString(document.lexically_normal().string())};
}

qint64 get(const fs::path& document, const char* what, qint64 fallback) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto [store, key] = find(s, document);
    const QJsonValue v = store->load().value(key).toObject().value(QLatin1String(what));
    return v.isDouble() ? static_cast<qint64>(v.toDouble()) : fallback;
}

void set(const fs::path& document, const char* what, qint64 value, qint64 fallback) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto [store, key] = find(s, document);
    QJsonObject& entries = store->load();
    QJsonObject entry = entries.value(key).toObject();
    const QJsonValue old = entry.value(QLatin1String(what));
    if (value == fallback) {
        if (old.isUndefined()) {
            return;
        }
        entry.remove(QLatin1String(what));
    } else {
        if (old.isDouble() && static_cast<qint64>(old.toDouble()) == value) {
            return;
        }
        entry.insert(QLatin1String(what), static_cast<double>(value));
    }
    if (entry.isEmpty()) {
        entries.remove(key);
    } else {
        entries.insert(key, entry);
    }
    store->save();
}
}  // namespace

fs::path keyOf(const DocumentItem& item) {
    // A PDF or an image with the .xopp that annotates it: the PDF / image (it keeps its place when the .xopp comes)
    return !item.pdf.empty() ? item.pdf : !item.image.empty() ? item.image : item.main();
}

fs::path keyOf(const fs::path& file) {
    const DocumentItem item = DocumentFiles::itemOf(file);
    return item.valid() ? keyOf(item) : file;
}

void setLibrary(const fs::path& root, const fs::path& file) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.root = root;
    s.library = Store{file, {}, false};
}

void setOutsideFile(const fs::path& file) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.outside = Store{file, {}, false};
    s.outsideSet = true;
}

int titlePage(const fs::path& document) { return static_cast<int>(get(document, "title", 0)); }
void setTitlePage(const fs::path& document, int page) { set(document, "title", std::max(0, page), 0); }
int lastPage(const fs::path& document) { return static_cast<int>(get(document, "last", -1)); }
void setLastPage(const fs::path& document, int page) { set(document, "last", page, -1); }
qint64 lastRead(const fs::path& document) { return get(document, "read", -1); }
void setRead(const fs::path& document, qint64 when) {
    set(document, "read", when >= 0 ? when : QDateTime::currentSecsSinceEpoch(), -1);
}

void moved(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    for (const auto& [from, to]: moves) {
        auto& s = state();
        std::lock_guard lock(s.mtx);
        // A document, or a folder with documents in it: every entry at or below `from`
        auto [fromStore, fromKey] = find(s, from);
        auto [toStore, toKey] = find(s, to);
        QJsonObject& source = fromStore->load();
        QJsonObject& target = toStore->load();
        std::vector<std::pair<QString, QJsonValue>> taken;
        for (auto it = source.begin(); it != source.end(); ++it) {
            if (it.key() == fromKey || it.key().startsWith(fromKey + '/')) {
                taken.emplace_back(toKey + it.key().mid(fromKey.size()), it.value());
            }
        }
        if (taken.empty()) {
            continue;
        }
        for (const auto& [key, value]: taken) {
            source.remove(fromKey + key.mid(toKey.size()));
        }
        for (const auto& [key, value]: taken) {
            target.insert(key, value);
        }
        fromStore->save();
        if (toStore != fromStore) {
            toStore->save();
        }
    }
}

}  // namespace xqt::DocumentPlaces
