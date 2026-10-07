/*
 * xournal-qt: first-page previews of documents on disk (library and recent files grids).
 *
 * PreviewProvider is an asynchronous QML image provider ("image://preview/<id>", see url()). A preview is rendered
 * once (the document is loaded on a worker thread and its title page drawn like the page thumbnails; a Markdown file
 * as it opens, see MarkdownFile.h; an image: scaled down) and stored as PNG, with a stamp of the document's files (sizes, modification times) and its title page, so a changed document
 * gets a new preview:
 *  - for the documents of the library in the "previews" pack of their folder's cache (see LibraryCache.h), by file
 *    name. A folder's pack is read when one of its previews is first wanted, and kept in memory (up to 48 MB of
 *    PNG, the folders used least recently go first). New previews are written a few seconds later, the whole pack.
 *    A document saved again (a new stamp) is drawn again and compared with its stored preview: when it looks the
 *    same (a later page was edited), previews.pack is not written; its new stamp goes into the folder's small
 *    "preview-stamps" pack instead, which is folded into previews.pack (and removed) the next time that is written;
 *  - for other documents (recent files) as PNG files in the user's cache.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QCborMap>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

#include "filesystem.h"
#include "DocumentFiles.h"
#include "LibraryCache.h"

namespace xqt {

class PreviewCache {
public:
    static constexpr int WIDTH = 360;
    /// Format of the stored previews (packs of another one are dropped).
    static constexpr int FORMAT = 1;
    static const QString PACK;
    /// Per document whose preview a newer version showed the same: that version's stamp, and the stamp in the
    /// "previews" pack it was compared with (tiny; written instead of the "previews" pack).
    static const QString STAMPS_PACK;
    /// The documents of the library at `location.root()` keep their previews in the caches of their folders
    /// (an invalid location: no library). What the library before has not written yet is written first.
    static void setLibrary(const CacheLocation& location);
    /// The stored preview, or render and store it (blocks: call on a worker thread).
    static QImage preview(const DocumentItem& item);
    /// The stored preview only (empty if there is none, or its folder's pack is big and not read yet).
    static QImage stored(const DocumentItem& item);
    /// Image URL for QML.
    static QString url(const DocumentItem& item);
    /// Forget the previews of documents that are not among these (in the folders read so far).
    static void prune(const std::vector<DocumentItem>& items);
    /// Remove the stored preview of a document, every version of it (in its folder's pack, written by the next flush,
    /// and outside the library): it was protected with a password (qt/docs/hybrid-pdf.md, "Encrypted PDFs").
    static void forget(const DocumentItem& item);
    /// The stored preview of a document of the library as it is now: its pack entry ("stamp", "png"; the stamp
    /// with the title page), else nothing (none, or of another version). Reads its folder's pack if needed. Any thread.
    static std::optional<QCborMap> storedEntry(const DocumentItem& item);
    /// What a stored preview shows, for these files as `stampOf` gives their stamps (sharing as a zip).
    static QString stampWith(const DocumentItem& item, const std::function<QString(const fs::path&)>& stampOf,
                             int titlePage);
    /// Files of a document got other stamps with the same content (LibraryIndex adopted its entry: copied, unzipped):
    /// its stored preview follows, if it showed them (`changes`: file stamp before, now).
    static void adopt(const DocumentItem& item, const std::vector<std::pair<QString, QString>>& changes);
    /// Files and folders renamed or moved by the app (old, new): their previews follow.
    static void moved(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// Write the changed packs now (else a few seconds after the last change). Returns whether all could be written.
    static bool flush();
    /// Forget what is not written yet and write nothing until the next setLibrary() (the cache is being removed).
    static void discard();
    static void setWriteDelays(int quietMs, int maxDelayMs);
    /// Where the preview of a document outside a library is stored.
    static fs::path outsideFile(const DocumentItem& item);
    /// Packs read and written so far (tests): "previews" packs; stamps packs.
    static int packsRead();
    static int packsWritten();
    static int stampPacksWritten();
};

class PreviewProvider final: public QQuickAsyncImageProvider {
public:
    /// Let the workers finish (and drop what is still waiting): they draw with Qt, which must not happen while
    /// the application is going away. Writes the previews not written yet.
    static void shutdown();

public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
};

}  // namespace xqt
