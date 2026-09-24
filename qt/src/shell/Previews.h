/*
 * xournal-qt: first-page previews of documents on disk (library and recent files grids).
 *
 * PreviewProvider is an asynchronous QML image provider ("image://preview/<id>", see url()). A preview is rendered
 * once (the document is loaded on a worker thread and its title page drawn like the page thumbnails; a Markdown file
 * as it opens, see MarkdownFile.h; an image: scaled down) and stored as PNG, with a stamp of the document's files (sizes, modification times) and its title page, so a changed document
 * gets a new preview:
 *  - for the documents of the library in the "previews" pack of their folder's cache (see LibraryCache.h), by file
 *    name. A folder's pack is read when one of its previews is first wanted, and kept in memory (up to 48 MB of
 *    PNG, the folders used least recently go first). New previews are written a few seconds later, the whole pack;
 *  - for other documents (recent files) as PNG files in the user's cache.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <utility>
#include <vector>

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
    /// Files and folders renamed or moved by the app (old, new): their previews follow.
    static void moved(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// Write the changed packs now (else a few seconds after the last change). Returns whether all could be written.
    static bool flush();
    /// Previews stored as PNG files in `dir` (the layout before the packs, named like outsideFile()) that belong
    /// to these documents as they are now: into the packs of their folders (written later). Returns how many.
    static int convertOldFiles(const fs::path& dir, const std::vector<DocumentItem>& items);
    /// Forget what is not written yet and write nothing until the next setLibrary() (the cache is being removed).
    static void discard();
    static void setWriteDelays(int quietMs, int maxDelayMs);
    /// Where the preview of a document outside a library is stored.
    static fs::path outsideFile(const DocumentItem& item);
    /// Packs read and written so far (tests).
    static int packsRead();
    static int packsWritten();
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
