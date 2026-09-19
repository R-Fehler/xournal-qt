/*
 * xournal-qt: page thumbnails for the page sidebar.
 *
 * ThumbnailProvider is an asynchronous QML image provider ("image://thumbnail/<session>/<page>/<revision>").
 * Pages are rendered on a worker thread with upstream's DocumentView (like upstream's PreviewJob), under a shared
 * document lock. Sessions are registered while they exist; unregistering waits for their running renders, so a
 * closed tab can be destroyed safely.
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

    /// Renders one page `width` pixels wide (thread-safe; takes a shared document lock).
    static QImage render(DocumentSession& session, size_t page, int width);
    /// Render a page of any document (e.g. one loaded only for a preview). Takes a shared lock.
    static QImage renderDocument(Document& doc, size_t page, int width);
};

}  // namespace xqt
