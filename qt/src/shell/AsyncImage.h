/*
 * xournal-qt: what the asynchronous image providers share (thumbnails, covers, hit pages, Markdown snippets,
 * annotation pictures): the response QML waits for, a kept-images cache with a byte limit, and the encoding of paths
 * and queries in image URLs.
 *
 * A provider makes an AsyncImageResponse, and either finishes it at once with a kept image or hands it to
 * ImageWorkers::respond, which draws on a worker of its pool (ImageWorkers.h). A response that QML cancelled (its
 * item went away: scrolled past) before its worker began is not drawn.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include <QImage>
#include <QQuickImageResponse>
#include <QString>

#include "filesystem.h"

namespace xqt {

class AsyncImageResponse: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override;
    /// QML does not want it any more (its item went away): a worker that has not begun does not draw it
    void cancel() override { cancelled = true; }
    bool isCancelled() const { return cancelled.load(); }
    /// Hand over the image (any thread; null: none): `finished` follows on the response's thread.
    void finish(QImage image);

protected:
    QImage image;

private:
    std::atomic<bool> cancelled{false};
};

/// Images by key, the least recently used dropped beyond a limit of bytes. Keys are ordered, so a cache by (name,
/// width) finds the smallest kept width of a name (lowerBound). Thread-safe.
template <typename Key>
class LruImageCache {
public:
    /// `keep`: entries kept even beyond the limit (the ones used last), so a single big image is not dropped at once
    explicit LruImageCache(qint64 limit, size_t keep = 0): max(limit), minEntries(keep) {}

    /// The image of `key`, which counts as used (null: none)
    QImage find(const Key& key) {
        std::lock_guard lock(mtx);
        auto it = entries.find(key);
        if (it == entries.end()) {
            return {};
        }
        lru.splice(lru.begin(), lru, it->second.used);
        return it->second.image;
    }
    /// The first entry whose key is not less than `key`, if `matches` its key; it counts as used if `touch`
    template <typename Pred>
    std::optional<std::pair<Key, QImage>> lowerBound(const Key& key, Pred matches, bool touch) {
        std::lock_guard lock(mtx);
        auto it = entries.lower_bound(key);
        if (it == entries.end() || !matches(it->first)) {
            return std::nullopt;
        }
        if (touch) {
            lru.splice(lru.begin(), lru, it->second.used);
        }
        return std::make_pair(it->first, it->second.image);
    }
    /// Keep an image (a key kept already stays as it is); the least recently used go beyond the limit
    void put(const Key& key, const QImage& image) {
        if (image.isNull()) {
            return;
        }
        std::lock_guard lock(mtx);
        if (entries.count(key)) {
            return;
        }
        lru.push_front(key);
        entries.emplace(key, Entry{image, lru.begin()});
        used += image.sizeInBytes();
        shrink();
    }
    /// Drop the entries whose keys `drop` names
    template <typename Pred>
    void removeIf(Pred drop) {
        std::lock_guard lock(mtx);
        for (auto it = entries.begin(); it != entries.end();) {
            if (drop(it->first)) {
                used -= it->second.image.sizeInBytes();
                lru.erase(it->second.used);
                it = entries.erase(it);
            } else {
                ++it;
            }
        }
    }
    void clear() {
        std::lock_guard lock(mtx);
        entries.clear();
        lru.clear();
        used = 0;
    }
    void setLimit(qint64 limit) {
        std::lock_guard lock(mtx);
        max = std::max<qint64>(0, limit);
        shrink();
    }
    qint64 limit() const {
        std::lock_guard lock(mtx);
        return max;
    }
    qint64 bytes() const {
        std::lock_guard lock(mtx);
        return used;
    }

private:
    using Use = std::list<Key>;  ///< front: used last
    struct Entry {
        QImage image;
        typename Use::iterator used;
    };
    void shrink() {
        while (used > max && lru.size() > minEntries) {
            auto it = entries.find(lru.back());
            used -= it->second.image.sizeInBytes();
            entries.erase(it);
            lru.pop_back();
        }
    }
    mutable std::mutex mtx;
    std::map<Key, Entry> entries;
    Use lru;
    qint64 used = 0;
    qint64 max;
    size_t minEntries;
};

/// A query (or any text) as one part of an image URL (base64url: no '/'), and back.
QString urlEncode(const QString& s);
QString urlDecode(const QString& part);
/// The same for a path, byte for byte.
QString urlEncodePath(const fs::path& path);
fs::path urlDecodePath(const QString& part);
/// A short hash of a stamp (documentStamp: the files' sizes and times) for an image URL: QML keeps images by URL, so
/// the URL changes when the files do.
QString urlStamp(const QByteArray& stamp);

}  // namespace xqt
