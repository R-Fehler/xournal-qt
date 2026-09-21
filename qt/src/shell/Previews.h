/*
 * xournal-qt: first-page previews of documents on disk (library and recent files grids).
 *
 * PreviewProvider is an asynchronous QML image provider ("image://preview/<id>", see url()). A preview is rendered
 * once (the document is loaded on a worker thread and its first page drawn like the page thumbnails) and stored as
 * PNG: for library documents in the library's metadata folder, else in the user's cache. The stored file's name
 * depends on the document's files (sizes, modification times), so changed documents get a new preview.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

#include "filesystem.h"
#include "DocumentFiles.h"

namespace xqt {

class PreviewCache {
public:
    static constexpr int WIDTH = 360;
    /// Documents in `root` keep their previews in `dir` (empty: no library).
    static void setLibrary(const fs::path& root, const fs::path& dir);
    /// Where the preview of a document is stored.
    static fs::path cacheFile(const DocumentItem& item);
    /// The stored preview, or render and store it (blocks: call on a worker thread).
    static QImage preview(const DocumentItem& item);
    /// Image URL for QML.
    static QString url(const DocumentItem& item);
    /// Delete stored library previews that belong to none of these documents.
    static void prune(const std::vector<DocumentItem>& items);
};

class PreviewProvider final: public QQuickAsyncImageProvider {
public:
    /// Let the workers finish (and drop what is still waiting): they draw with Qt, which must not happen while
    /// the application is going away.
    static void shutdown();

public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
};

}  // namespace xqt
