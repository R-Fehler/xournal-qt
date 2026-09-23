/*
 * xournal-qt: small pictures of all pages of the open documents, drawn in advance, in two sizes:
 *  - sketches (128 px; 96 or 64 px when many pages are open): the page sidebar, the page grid and the tab overview
 *    show a page's sketch at once - a synchronous image provider, "image://sketch/<session>/<page id>/<revision>",
 *    from memory only - and the sharp thumbnail on top of it when that is drawn (ThumbnailProvider). Flying through
 *    hundreds of pages shows pages that get sharp, never blank ones;
 *  - previews (768 px; 512 or 384 px when many pages are open): the canvas shows a page's preview until the page is
 *    rendered, and thumbnails up to their width are scaled from them instead of being drawn.
 * Poppler costs nearly the same at any width (parsing, decoding images), so a page is drawn once at the preview width
 * and its sketch is scaled from that. Nothing is ever drawn in front of the canvas or of a sharp thumbnail.
 *
 * Pages are drawn by two low-priority workers, so the canvas and the sharp thumbnails go first; nothing new is
 * started while the canvas renders the pages in view (RenderService::visiblePagesBusy). Each draws the PDF
 * with an instance of its own (poppler draws one page of an instance at a time: the canvas does not wait for them),
 * kept while there is something to draw. What comes first:
 *  - pages without a sketch, then pages without a preview, then outdated ones; the document shown last first (in any
 *    window), then the others in the order they were shown; in a document the current page first, then outwards;
 *  - from a bigger picture of the page if there is one (scaled down), else drawn;
 *  - a changed page keeps its old pictures until the new ones are there; they are drawn again once the edits paused.
 * On disk: the previews of pages as they are in the document's file are stored (JPEG, in the user's cache, a folder
 * per document named by its files' sizes and times), so the next opening reads them instead of drawing: a document
 * opened before shows all its pages at once. Pages as saved are known from the opening, a saving, or an undo back to
 * the saved state; changed pages are not stored. The folders used longest ago go first beyond 1 GB. The title page
 * of a library document gets the library's stored preview right away (before anything is drawn or read).
 * Memory, with 16 bits per pixel: the sketches take a quarter of the memory for page previews (setBudget), the
 * previews a tenth of the memory for rendered pages (CanvasMemory::previewBudget). Each size is the biggest at which
 * all pages of all open documents fit; if even the smallest does not fit, the documents shown last get theirs.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QTimer>

#include "filesystem.h"

class XojPdfDocument;

namespace xqt {

class DocumentSession;

class PageSketches final: public QObject {
    Q_OBJECT
public:
    static PageSketches& instance();
    static constexpr std::array<int, 3> WIDTHS{128, 96, 64};
    static constexpr std::array<int, 3> PREVIEW_WIDTHS{768, 512, 384};

    /// Sessions come and go with ThumbnailProvider::registerSession / unregisterSession.
    void add(quint64 id, DocumentSession* session);
    void remove(quint64 id);
    /// Shown in a window now: its pages come first.
    void focus(quint64 id);

    /// URL of a page's sketch (for QML); empty while it has none.
    QString url(quint64 session, quint64 pageId) const;
    QImage image(quint64 session, quint64 pageId) const;
    /// The page's preview (of any revision: the canvas shows it until the page is rendered); null: none
    QImage preview(quint64 session, quint64 pageId) const;
    /// The biggest picture of this revision of a page, preview or sketch, if there is one (any thread).
    QImage imageOfRevision(quint64 session, quint64 revision) const;
    /// A sharp thumbnail was drawn (any thread): the page's sketch (and preview) is made from it, if it needs one.
    void offer(quint64 session, quint64 pageId, quint64 revision, const QImage& sharp);

    void setBudget(qint64 bytes);
    qint64 bytes() const;          ///< of the sketches
    qint64 previewBytes() const;
    /// Widths of the sketches and of the previews now
    int width() const { return sketches.level; }
    int previewWidth() const { return previews.level; }
    /// Nothing to do, nothing planned (tests)
    bool idle() const;
    /// Pages drawn (not scaled from another picture) so far (tests)
    int drawCount() const { return draws; }
    /// How long to wait after a document was opened or shown, and after its last edit (ms)
    void setDelays(int shown, int edited);
    /// Pages read from disk so far (tests)
    int readCount() const { return reads; }
    /// Folder of the stored previews of a session's document (empty: none, e.g. not saved or changed; tests)
    fs::path diskFolder(quint64 session) const;
    /// Delete the stored previews used longest ago beyond `bytes` (any thread; runs once after the start with 1 GB)
    static void trimDisk(qint64 bytes);
    static constexpr qint64 DISK_LIMIT = qint64(1024) * 1024 * 1024;

Q_SIGNALS:
    /// Pictures of the session were added or replaced (collected for a moment).
    void changed(qulonglong session);

private:
    PageSketches();
    ~PageSketches() override;
    struct Picture {
        quint64 revision = 0;
        QImage image;
    };
    /// One size: the pictures by session and page id, what they take, what they may take, their width
    struct Tier {
        std::map<quint64, std::unordered_map<quint64, Picture>> pictures;
        qint64 used = 0;
        std::atomic<qint64> budget{0};
        std::atomic<int> level{0};
        const Picture* find(quint64 session, quint64 pageId) const;
        void put(quint64 session, quint64 pageId, quint64 revision, QImage image);  ///< (session known)
        void dropSession(quint64 session);
        /// Drop the pictures of pages not in `keep`
        void keepOnly(quint64 session, const std::set<quint64>& keep);
    };
    struct Session {
        quint64 id = 0;
        QPointer<DocumentSession> session;
        quint64 shown = 0;  ///< when it was shown last (counter)
    };
    struct Job {
        quint64 session = 0;
        quint64 pageId = 0;
        quint64 revision = 0;
        bool preview = false;  ///< the page gets a preview too
        bool write = false;    ///< only store its preview on disk
    };
    /// A session's document as it is on disk: its folder of stored previews, the pages as saved (page id ->
    /// revision and place in the file) and the places stored or read already
    struct Disk {
        fs::path folder;
        std::unordered_map<quint64, std::pair<quint64, size_t>> saved;
        std::set<size_t> stored;
    };
    /// The document is as saved (opened, saved, undone back): remember its pages as they are (GUI thread)
    void capture(quint64 id);
    /// The library's stored preview of the title page, until the page is drawn (GUI thread)
    void seedTitlePage(quint64 id);
    /// File of a page's stored preview if the page is as saved (under mtx; empty: not)
    fs::path diskFile(quint64 session, quint64 pageId, quint64 revision) const;
    void markStored(quint64 session, quint64 pageId);
    static constexpr int WORKERS = 2;
    /// A PDF instance of the session's document for a worker (loaded if none is spare; null: use the document's)
    std::unique_ptr<XojPdfDocument> takePdf(quint64 session, const fs::path& path, size_t pages);
    void givePdf(quint64 session, const fs::path& path, std::unique_ptr<XojPdfDocument> pdf);
    void planSoon(int ms);
    void plan();
    void next();
    /// Store what was drawn (any thread): the preview if wanted, and the sketch scaled from it
    void store(quint64 session, quint64 pageId, quint64 revision, const QImage& image, bool preview);
    void announce(quint64 session);

    // Worker and GUI thread
    mutable std::mutex mtx;
    Tier sketches;
    Tier previews;
    std::atomic<int> draws{0};
    std::atomic<int> reads{0};
    std::map<quint64, Disk> disks;
    struct PdfCopies {
        fs::path path;
        std::vector<std::unique_ptr<XojPdfDocument>> spare;
    };
    std::map<quint64, PdfCopies> pdfCopies;

    // GUI thread
    std::vector<Session> sessions;
    quint64 shownCounter = 0;
    std::deque<Job> jobs;
    int running = 0;  ///< jobs the workers are on
    QTimer planTimer;
    QTimer editTimer;
    QTimer visibleTimer;  ///< the pages in view are being rendered: look again soon
    QTimer announceTimer;
    std::set<quint64> announced;
    int shownDelay = 400;
};

/// "image://sketch/<session>/<page id>/<revision>": the page's sketch as it is kept (synchronous; never draws).
class SketchProvider final: public QQuickImageProvider {
public:
    SketchProvider(): QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace xqt
