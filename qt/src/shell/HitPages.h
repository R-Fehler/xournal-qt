/*
 * xournal-qt: the pages with search hits of library documents (extended library search).
 *
 * HitPageProvider is an asynchronous QML image provider, "image://hitpage/<path>/<stamp>/<query>/<page>" (see
 * baseUrl()). A page is drawn like a page thumbnail, with the hits marked in the image itself (one texture per page,
 * no item per hit). For speed:
 *  - loaded documents are kept (the last 12 used): all pages of a document come from one load;
 *  - drawn pages are kept in memory (up to 128 MB) without the marks: a new search or scrolling back only marks them;
 *  - widths are rounded up to steps of 64 px, so there are few different images of a page;
 *  - a request that is not wanted any more (its page was scrolled away) is dropped before it is drawn;
 *  - different documents are drawn in parallel, the pages of one document one after the other (poppler's lock).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

#include "filesystem.h"
#include "DocumentFiles.h"

namespace xqt {

class HitPageProvider final: public QQuickAsyncImageProvider {
public:
    /// Let the workers finish before the application goes away (they draw with Qt).
    static void shutdown();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// URL of a document's pages for a search; append "/<page>" (0-based).
    static QString baseUrl(const DocumentItem& item, const QString& query);
    /// Draw a page `width` pixels wide (rounded up to 64) with the hits of `query` marked. Blocks; any thread.
    static QImage render(const fs::path& file, int page, const QString& query, int width);
    /// Forget the kept documents and images (tests).
    static void clearCaches();
    /// Pages drawn so far (tests: the cache works).
    static int renderCount();
};

}  // namespace xqt
