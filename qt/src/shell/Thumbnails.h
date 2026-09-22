/*
 * xournal-qt: page thumbnails for the page sidebar, the page grid and the overview of open documents.
 *
 * ThumbnailProvider is an asynchronous QML image provider ("image://thumbnail/<session>/<page>/<revision>[/...]";
 * the revision is DocumentSession::pageRevision, which names the page itself: it stays with the page when pages
 * before it come or go; anything after it only makes QML ask again). Pages are rendered on worker threads with upstream's DocumentView (like
 * upstream's PreviewJob), under a shared document lock. Sessions are registered while they exist; unregistering
 * waits for their running renders, so a closed tab can be destroyed safely.
 *
 * Drawing a page (the PDF by poppler, then the ink) is what takes the time, so:
 *  - drawn thumbnails are kept in memory, up to a limit (Settings → Documents; the least recently used go first):
 *    scrolling back, switching tabs, opening the page grid or the overview again shows them at once; the sidebar,
 *    the page grid and the overview share them;
 *  - widths are rounded up to steps of 64 px (few different images of a page), and a smaller one is scaled down
 *    from a bigger one that is kept instead of being drawn;
 *  - the thumbnail asked for last is drawn first (what is in view now), and one that is not wanted any more (it was
 *    scrolled away) is not drawn at all;
 *  - one up to the width of the page's preview comes from it (PageSketches), and every drawn one gives the page its
 *    sketch (and preview).
 * The memory for page previews is shared: three quarters for these, one quarter for the sketches.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>

#include <QImage>
#include <QQuickAsyncImageProvider>

#include "model/PageRef.h"

class Document;
class XojPdfDocument;

namespace xqt {

class DocumentSession;

class ThumbnailProvider final: public QQuickAsyncImageProvider {
public:
    ThumbnailProvider() = default;
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// Make a session's pages available; returns its id for the image URLs.
    static quint64 registerSession(DocumentSession* session);
    /// Remove a session; waits until no thumbnail of it is being rendered.
    static void unregisterSession(DocumentSession* session);
    static quint64 idOf(const DocumentSession* session);
    /// A registered session to draw from on a worker: it stays until releaseSession (nullptr: none).
    static DocumentSession* acquireSession(quint64 id);
    static void releaseSession(quint64 id);

    /// How much memory the page previews may take (bytes): the kept thumbnails and the sketches.
    static void setCacheLimit(qint64 bytes);
    static qint64 cacheBytes();  ///< of the kept thumbnails
    /// The smallest kept thumbnail of this revision at least `width` wide (any thread; null: none).
    static QImage keptImage(quint64 session, quint64 revision, int width);
    /// How many pages were drawn so far (tests).
    static int renderCount();
    static constexpr int WIDTH_STEP = 64;
    static constexpr qint64 DEFAULT_CACHE_MB = 256;

    /// Renders one page `width` pixels wide (thread-safe; takes a shared document lock).
    static QImage render(DocumentSession& session, size_t page, int width);
    /// Render a page of any document (e.g. one loaded only for a preview). Takes a shared lock.
    static QImage renderDocument(Document& doc, size_t page, int width);
    /// Render this page (any thread). The PDF is drawn without the document lock (poppler has its own), so edits
    /// do not wait for it; only the ink is drawn under a shared lock. `pdf`: another instance of the document's PDF
    /// to draw it with (poppler draws one page of an instance at a time: the canvas need not wait for this one).
    static QImage renderPage(Document& doc, const PageRef& page, int width, const XojPdfDocument* pdf = nullptr);
};

}  // namespace xqt
