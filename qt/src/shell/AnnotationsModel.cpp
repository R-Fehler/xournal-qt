#include "AnnotationsModel.h"

#include <atomic>
#include <chrono>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <QColor>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include "model/Document.h"
#include "render/RenderService.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"

#include "Thumbnails.h"

namespace xqt {

namespace {
/// One worker, at low priority: the annotations are never urgent.
QThreadPool& makePool(QThreadPool*& pool) {
    pool = new QThreadPool;  // (never destroyed: see the post routine)
    pool->setMaxThreadCount(1);
    pool->setThreadPriority(QThread::LowPriority);
    return *pool;
}
QThreadPool& readPool() {
    static QThreadPool* pool = nullptr;
    static QThreadPool& p = [] {
        QThreadPool& made = makePool(pool);
        // Before the program's statics go: a task still running would release its poppler document while poppler
        // and glib are torn down
        qAddPostRoutine([] {
            pool->clear();
            pool->waitForDone();
        });
        return std::ref(made);
    }();
    return p;
}
QThreadPool& picturePool() {
    static QThreadPool* pool = nullptr;
    static QThreadPool& p = [] {
        QThreadPool& made = makePool(pool);
        qAddPostRoutine([] {
            pool->clear();
            pool->waitForDone();
        });
        return std::ref(made);
    }();
    return p;
}

unsigned bitOf(annotations::Kind kind) { return 1U << static_cast<unsigned>(annotations::groupOf(kind)); }

using annotations::pictureRect;
}  // namespace

/// What is kept per open document: the items of each page by its revision (read on the worker only), and the last
/// list shown (for switching back to the document).
struct AnnotationsModel::State {
    std::mutex mtx;
    struct Page {
        quint64 revision = 0;
        std::vector<annotations::Item> items;
    };
    std::unordered_map<quint64, Page> pages;  ///< by page id
    fs::path pdfFile;
    quint64 pdfNumbering = 0;
    std::unique_ptr<PdfLayoutReader> pdf;
    std::atomic<int> pagesRead{0};
    // UI thread
    std::vector<annotations::Item> shown;
    std::vector<quint64> shownRevisions;
};

AnnotationsModel::AnnotationsModel(QObject* parent): QAbstractListModel(parent) {
    timer.setSingleShot(true);
    timer.setInterval(600);
    connect(&timer, &QTimer::timeout, this, &AnnotationsModel::start);
}

AnnotationsModel::~AnnotationsModel() { setSession(nullptr); }

void AnnotationsModel::setSession(DocumentSession* s) {
    if (s == session) {
        return;
    }
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    timer.stop();
    ++generation;  // (a read of the other document running: not shown here)
    session = s;
    sessionId = 0;
    std::vector<annotations::Item> kept;
    std::vector<quint64> keptRevisions;
    if (session) {
        sessionId = ThumbnailProvider::registerSession(session);
        auto& state = states[session];
        if (!state) {
            state = std::make_shared<State>();
            // (kept while the document is open, also while another one is shown)
            connect(session, &QObject::destroyed, this, [this, s] {
                states.erase(s);
                if (session == s) {
                    setSession(nullptr);
                }
            });
        } else {
            kept = state->shown;
            keptRevisions = state->shownRevisions;
        }
        connections.push_back(connect(session, &DocumentSession::pageRevisionsChanged, this, &AnnotationsModel::schedule));
        connections.push_back(connect(session, &DocumentSession::filePathChanged, this, &AnnotationsModel::availableChanged));
    }
    apply(std::move(kept), std::move(keptRevisions), true);
    Q_EMIT availableChanged();
    stale = true;
    if (isActive && session) {
        start();
    }
}

void AnnotationsModel::setActive(bool on) {
    if (on == isActive) {
        return;
    }
    isActive = on;
    Q_EMIT activeChanged();
    if (isActive && stale) {
        start();
    }
}

int AnnotationsModel::pagesRead() const {
    auto it = states.find(session);
    return it != states.end() ? it->second->pagesRead.load() : 0;
}

bool AnnotationsModel::available() const { return session && !session->textFile(); }

void AnnotationsModel::schedule() {
    stale = true;
    if (!isActive && waiting.empty()) {
        return;  // read when the panel is shown
    }
    if (running) {
        dirty = true;
        return;
    }
    timer.start();
}

void AnnotationsModel::start() {
    timer.stop();
    if (!session) {
        return;
    }
    if (running) {
        dirty = true;
        return;
    }
    if (!available()) {
        stale = false;
        apply({}, {});
        const auto calls = std::move(waiting);
        waiting.clear();
        for (const auto& f: calls) {
            f();
        }
        return;
    }
    running = true;
    dirty = false;
    stale = false;
    Q_EMIT busyChanged();
    const std::shared_ptr<State> state = states[session];
    std::vector<DocumentSession::PageStamp> stamps = session->pageStamps();
    fs::path pdf;
    {
        std::shared_lock lock(*session->getDocument());
        pdf = session->getDocument()->getPdfFilepath();
    }
    const quint64 numbering = session->pdfNumbering();
    const quint64 id = sessionId;
    const quint64 gen = generation;
    QPointer<AnnotationsModel> self(this);
    readPool().start([self, state, stamps = std::move(stamps), pdf, numbering, id, gen] {
        std::vector<annotations::Item> all;
        std::vector<quint64> revisions;
        bool gone = false;
        {
            std::lock_guard lock(state->mtx);
            if (state->pdfFile != pdf || state->pdfNumbering != numbering) {
                // Another PDF, or its pages numbered anew: what came from it is read again
                state->pages.clear();
                state->pdf = pdf.empty() ? nullptr : std::make_unique<PdfLayoutReader>(pdf);
                state->pdfFile = pdf;
                state->pdfNumbering = numbering;
            }
            std::unordered_map<quint64, State::Page> next;
            for (size_t i = 0; i < stamps.size() && !gone; ++i) {
                const auto& stamp = stamps[i];
                State::Page page;
                if (auto it = state->pages.find(stamp.id); it != state->pages.end() && it->second.revision == stamp.revision) {
                    page = std::move(it->second);
                } else {
                    // Not while the canvas has pages in view to draw
                    RenderService::waitForVisiblePages(std::chrono::milliseconds(300));
                    DocumentSession* s = ThumbnailProvider::acquireSession(id);
                    if (!s) {
                        gone = true;  // closed meanwhile
                        break;
                    }
                    annotations::PageContent content;
                    {
                        std::shared_lock docLock(*s->getDocument());
                        content = annotations::read(*stamp.page);
                    }
                    ThumbnailProvider::releaseSession(id);
                    page.revision = stamp.revision;
                    page.items = annotations::itemsOf(content, i, state->pdf.get());
                    ++state->pagesRead;
                }
                for (annotations::Item item: page.items) {
                    item.page = i;
                    all.push_back(std::move(item));
                    revisions.push_back(stamp.revision);
                }
                next.emplace(stamp.id, std::move(page));
            }
            if (!gone) {
                state->pages = std::move(next);
            }
        }
        QMetaObject::invokeMethod(
                qApp,
                [self, state, gen, gone, all = std::move(all), revisions = std::move(revisions)]() mutable {
                    if (self) {
                        self->finished(state, gen, gone, std::move(all), std::move(revisions));
                    }
                },
                Qt::QueuedConnection);
    });
}

void AnnotationsModel::finished(const std::shared_ptr<State>& state, quint64 gen, bool gone,
                                std::vector<annotations::Item> result, std::vector<quint64> revs) {
    running = false;
    Q_EMIT busyChanged();
    if (!gone) {
        state->shown = result;
        state->shownRevisions = revs;
    }
    if (!gone && gen == generation && session && states[session] == state) {
        apply(std::move(result), std::move(revs));
    }
    if (!session) {
        return;
    }
    if (dirty || gen != generation) {
        // Changed meanwhile (or another document now): read again, at once for someone waiting
        if (!waiting.empty()) {
            start();
        } else if (isActive) {
            timer.start();
        }
        return;
    }
    const auto calls = std::move(waiting);
    waiting.clear();
    for (const auto& f: calls) {
        f();
    }
}

void AnnotationsModel::whenCurrent(std::function<void()> then) {
    if (!session) {
        then();
        return;
    }
    if (!stale && !running && !timer.isActive()) {
        then();
        return;
    }
    waiting.push_back(std::move(then));
    if (running) {
        dirty = true;
    } else {
        start();
    }
}

void AnnotationsModel::apply(std::vector<annotations::Item> result, std::vector<quint64> revs, bool reset) {
    std::vector<size_t> newRows;
    for (size_t i = 0; i < result.size(); ++i) {
        if (shown & bitOf(result[i].kind)) {
            newRows.push_back(i);
        }
    }
    if (reset) {  // (another document)
        beginResetModel();
        items = std::move(result);
        revisions = std::move(revs);
        rows = std::move(newRows);
        endResetModel();
        Q_EMIT countChanged();
        return;
    }
    const auto same = [&](size_t oldRow, size_t newRow) {
        const size_t a = rows[oldRow], b = newRows[newRow];
        return items[a] == result[b] && revisions[a] == revs[b];
    };
    // Only the rows that changed: the list stays where it was scrolled to
    size_t prefix = 0;
    while (prefix < rows.size() && prefix < newRows.size() && same(prefix, prefix)) {
        ++prefix;
    }
    size_t suffix = 0;
    while (suffix < rows.size() - prefix && suffix < newRows.size() - prefix &&
           same(rows.size() - 1 - suffix, newRows.size() - 1 - suffix)) {
        ++suffix;
    }
    if (rows.size() - prefix - suffix > 0) {
        beginRemoveRows({}, static_cast<int>(prefix), static_cast<int>(rows.size() - suffix - 1));
        rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(prefix),
                   rows.end() - static_cast<std::ptrdiff_t>(suffix));
        endRemoveRows();
    }
    const size_t inserted = newRows.size() - prefix - suffix;
    if (inserted > 0) {
        beginInsertRows({}, static_cast<int>(prefix), static_cast<int>(prefix + inserted - 1));
    }
    items = std::move(result);
    revisions = std::move(revs);
    rows = std::move(newRows);
    if (inserted > 0) {
        endInsertRows();
    }
    Q_EMIT countChanged();
}

void AnnotationsModel::refilter() {
    beginResetModel();
    rows.clear();
    for (size_t i = 0; i < items.size(); ++i) {
        if (shown & bitOf(items[i].kind)) {
            rows.push_back(i);
        }
    }
    endResetModel();
    Q_EMIT countChanged();
}

QStringList AnnotationsModel::shownKinds() const {
    QStringList out;
    for (int k = 0; k < annotations::KIND_COUNT; ++k) {
        const auto kind = static_cast<annotations::Kind>(k);
        if (annotations::groupOf(kind) == kind && (shown & bitOf(kind))) {
            out << annotations::nameOf(kind);
        }
    }
    return out;
}

void AnnotationsModel::setShownKinds(const QStringList& kinds) {
    unsigned bits = 0;
    for (int k = 0; k < annotations::KIND_COUNT; ++k) {
        const auto kind = static_cast<annotations::Kind>(k);
        if (kinds.contains(annotations::nameOf(annotations::groupOf(kind)))) {
            bits |= bitOf(kind);
        }
    }
    if (bits == shown) {
        return;
    }
    shown = bits;
    refilter();
    Q_EMIT filterChanged();
}

int AnnotationsModel::countOf(const QString& kind) const {
    int n = 0;
    for (const auto& item: items) {
        n += annotations::nameOf(annotations::groupOf(item.kind)) == kind ? 1 : 0;
    }
    return n;
}

int AnnotationsModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant AnnotationsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= count()) {
        return {};
    }
    const size_t i = rows[static_cast<size_t>(index.row())];
    const annotations::Item& item = items[i];
    switch (role) {
        case KindRole:
            return annotations::nameOf(item.kind);
        case PageRole:
            return static_cast<int>(item.page);
        case TextRole:
            return item.text;
        case CommentRole:
            return item.comment;
        case TargetRole:
            return item.target;
        case ColorRole:
            return QColor(QRgb(item.color)).name();
        case RectRole:
            return item.rect;
        case PictureRole: {
            if (item.kind != annotations::Kind::Ink) {
                return QString();
            }
            const QRectF r = pictureRect(item.rect);
            return QStringLiteral("image://annotation/%1/%2/%3,%4,%5,%6")
                    .arg(sessionId)
                    .arg(revisions[i])
                    .arg(r.x(), 0, 'f', 1)
                    .arg(r.y(), 0, 'f', 1)
                    .arg(r.width(), 0, 'f', 1)
                    .arg(r.height(), 0, 'f', 1);
        }
        case AspectRole: {
            const QRectF r = item.kind == annotations::Kind::Ink ? pictureRect(item.rect) : item.rect;
            return r.width() > 0 ? r.height() / r.width() : 1.0;
        }
        default:
            return {};
    }
}

QHash<int, QByteArray> AnnotationsModel::roleNames() const {
    return {{KindRole, "kind"},     {PageRole, "page"},       {TextRole, "itemText"},
            {CommentRole, "comment"}, {TargetRole, "target"}, {ColorRole, "color"},
            {RectRole, "rect"},     {PictureRole, "picture"}, {AspectRole, "aspect"}};
}

// --- pictures of handwriting -----------------------------------------------------------------------------------

namespace {
class PictureResponse final: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image); }
    void cancel() override { cancelled = true; }
    QImage image;
    std::atomic<bool> cancelled{false};
};

std::atomic<int> pictureRenders{0};

/// The pictures drawn last, by their id and width, up to PICTURE_CACHE_BYTES (the least recently used go first):
/// scrolling back in the panel shows them at once. Their ids hold the page's revision, so an edited page's pictures
/// are simply not asked for again.
class PictureCache {
public:
    QImage find(const QString& key) {
        std::lock_guard lock(mtx);
        auto it = index.find(key);
        if (it == index.end()) {
            return {};
        }
        entries.splice(entries.begin(), entries, it->second);
        return it->second->second;
    }
    void put(const QString& key, const QImage& img) {
        std::lock_guard lock(mtx);
        if (index.count(key)) {
            return;
        }
        entries.emplace_front(key, img);
        index[key] = entries.begin();
        bytes += img.sizeInBytes();
        while (bytes > AnnotationImageProvider::PICTURE_CACHE_BYTES && entries.size() > 1) {
            bytes -= entries.back().second.sizeInBytes();
            index.erase(entries.back().first);
            entries.pop_back();
        }
    }

private:
    std::mutex mtx;
    std::list<std::pair<QString, QImage>> entries;
    std::unordered_map<QString, std::list<std::pair<QString, QImage>>::iterator> index;
    qint64 bytes = 0;
};
PictureCache& pictureCache() {
    static PictureCache cache;
    return cache;
}
}  // namespace

int AnnotationImageProvider::renderCount() { return pictureRenders.load(); }

QQuickImageResponse* AnnotationImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new PictureResponse;
    // <session>/<revision>/<x>,<y>,<w>,<h>
    const QStringList parts = id.split(u'/');
    const quint64 sessionId = parts.value(0).toULongLong();
    const quint64 revision = parts.value(1).toULongLong();
    const QStringList r = parts.value(2).split(u',');
    const QRectF rect(r.value(0).toDouble(), r.value(1).toDouble(), r.value(2).toDouble(), r.value(3).toDouble());
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 240;
    const QString key = id + u'@' + QString::number(width);
    if (QImage kept = pictureCache().find(key); !kept.isNull()) {
        response->image = std::move(kept);
        QMetaObject::invokeMethod(response, &QQuickImageResponse::finished, Qt::QueuedConnection);
        return response;
    }
    // The one asked for last first: that is what is in view now (the rows scrolled past are cancelled)
    static std::atomic<int> order{0};
    picturePool().start(QRunnable::create([response, sessionId, revision, rect, width, key] {
        QImage img;
        if (!response->cancelled) {
            // Not while the canvas has pages in view to draw (the PDF is drawn with the document's instance)
            RenderService::waitForVisiblePages(std::chrono::milliseconds(300));
        }
        if (!response->cancelled && rect.width() > 0 && rect.height() > 0) {
            if (DocumentSession* s = ThumbnailProvider::acquireSession(sessionId)) {
                if (auto stamp = s->pageOfRevision(revision)) {
                    // (at most 4 pixels per point: a tiny dot is not drawn as a poster)
                    img = annotations::drawArea(*s->getDocument(), stamp->page, rect,
                                                std::min(4.0, width / rect.width()));
                    ++pictureRenders;
                    pictureCache().put(key, img);
                }
                ThumbnailProvider::releaseSession(sessionId);
            }
        }
        QMetaObject::invokeMethod(
                response,
                [response, img = std::move(img)]() mutable {
                    response->image = std::move(img);
                    Q_EMIT response->finished();
                },
                Qt::QueuedConnection);
    }), ++order);
    return response;
}

}  // namespace xqt
