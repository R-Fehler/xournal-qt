/*
 * xournal-qt: small previews ("sketches") of all pages of the open documents, drawn in advance.
 *
 * The page sidebar, the page grid and the tab overview show a page's sketch at once - a synchronous image provider,
 * "image://sketch/<session>/<page id>/<revision>", from memory only - and the sharp thumbnail on top of it when that
 * is drawn (ThumbnailProvider). Flying through hundreds of pages shows pages that get sharp, never blank ones.
 *
 * Sketches are drawn by two low-priority workers, so the canvas and the sharp thumbnails go first. Each draws the PDF
 * with an instance of its own (poppler draws one page of an instance at a time: the canvas does not wait for them),
 * kept while there is something to sketch. What comes first:
 *  - pages without a sketch before outdated ones; the document shown last first (in any window), then the others in
 *    the order they were shown; in a document the current page first, then outwards from it;
 *  - from a kept sharp thumbnail if there is one (scaled down), else drawn;
 *  - a changed page keeps its old sketch until the new one is there; it is sketched again once the edits paused.
 * They take a part of the memory for page previews (setBudget), with 16 bits per pixel. All have the same width:
 * 128 px while all pages of all open documents fit, else 96 or 64 px. If even those do not fit, the documents shown
 * last get theirs and the others none.
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

    /// Sessions come and go with ThumbnailProvider::registerSession / unregisterSession.
    void add(quint64 id, DocumentSession* session);
    void remove(quint64 id);
    /// Shown in a window now: its pages come first.
    void focus(quint64 id);

    /// URL of a page's sketch (for QML); empty while it has none.
    QString url(quint64 session, quint64 pageId) const;
    QImage image(quint64 session, quint64 pageId) const;
    /// The sketch of this revision of a page, if there is one (any thread).
    QImage imageOfRevision(quint64 session, quint64 revision) const;
    /// A sharp thumbnail was drawn (any thread): the page's sketch is made from it, if it needs one.
    void offer(quint64 session, quint64 pageId, quint64 revision, const QImage& sharp);

    void setBudget(qint64 bytes);
    qint64 bytes() const;
    /// Width of the sketches now
    int width() const { return level; }
    /// Nothing to do, nothing planned (tests)
    bool idle() const;
    /// Pages drawn for sketches (not scaled from another image) so far (tests)
    int drawCount() const { return draws; }
    /// How long to wait after a document was opened or shown, and after its last edit (ms)
    void setDelays(int shown, int edited);

Q_SIGNALS:
    /// Sketches of the session were added or replaced (collected for a moment).
    void changed(qulonglong session);

private:
    PageSketches();
    ~PageSketches() override;
    struct Sketch {
        quint64 revision = 0;
        QImage image;
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
    };
    static constexpr int WORKERS = 2;
    /// A PDF instance of the session's document for a worker (loaded if none is spare; null: use the document's)
    std::unique_ptr<XojPdfDocument> takePdf(quint64 session, const fs::path& path, size_t pages);
    void givePdf(quint64 session, const fs::path& path, std::unique_ptr<XojPdfDocument> pdf);
    void planSoon(int ms);
    void plan();
    void next();
    void store(quint64 session, quint64 pageId, quint64 revision, const QImage& image);
    void announce(quint64 session);

    // Worker and GUI thread
    mutable std::mutex mtx;
    std::map<quint64, std::unordered_map<quint64, Sketch>> sketches;  ///< session -> page id -> sketch
    qint64 used = 0;
    std::atomic<qint64> budget;
    std::atomic<int> level{WIDTHS[0]};
    std::atomic<int> draws{0};
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
