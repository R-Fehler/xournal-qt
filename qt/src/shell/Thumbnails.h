/*
 * xournal-qt: page thumbnails for the page sidebar, the page grid and the overview of open documents.
 *
 * ThumbnailProvider is an asynchronous QML image provider ("image://thumbnail/<session>/<page>/<revision>[/...]";
 * the revision is DocumentSession::pageRevision, anything after it only makes QML ask again). Pages are rendered on worker threads with upstream's DocumentView (like
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
 *    scrolled away) is not drawn at all.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>

#include <QImage>
#include <QQuickAsyncImageProvider>

class Document;

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

    /// How much memory the kept thumbnails may take (bytes).
    static void setCacheLimit(qint64 bytes);
    static qint64 cacheBytes();
    /// How many pages were drawn so far (tests).
    static int renderCount();
    static constexpr int WIDTH_STEP = 64;
    static constexpr qint64 DEFAULT_CACHE_MB = 256;

    /// Renders one page `width` pixels wide (thread-safe; takes a shared document lock).
    static QImage render(DocumentSession& session, size_t page, int width);
    /// Render a page of any document (e.g. one loaded only for a preview). Takes a shared lock.
    static QImage renderDocument(Document& doc, size_t page, int width);
};

}  // namespace xqt
