#include "Previews.h"

#include "DocumentPlaces.h"

#include <atomic>
#include <map>
#include <mutex>
#include <set>

#include <QBuffer>
#include <QCborMap>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QThreadPool>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "util/PathUtil.h"

#include "ImageFile.h"
#include "Library.h"
#include "MarkdownFile.h"
#include "Thumbnails.h"

namespace xqt {

const QString PreviewCache::PACK = QStringLiteral("previews");
const QString PreviewCache::STAMPS_PACK = QStringLiteral("preview-stamps");

namespace {
/// PNG kept in memory at most (the folders used least recently go first)
constexpr qint64 BUDGET = 48ll * 1024 * 1024;
/// A folder's pack of this size or more is not read on the UI thread just for a placeholder (stored())
constexpr qint64 BIG_PACK = 4ll * 1024 * 1024;

struct Stored {
    QString stamp;      ///< the version of the document it shows
    QByteArray png;
    QString packStamp;  ///< its stamp in previews.pack (older when a newer version looked the same: see STAMPS_PACK)
};
/// The previews of the documents directly in one folder, as in its pack.
struct Folder {
    std::map<QString, Stored> entries;  ///< by file name
    bool dirty = false;        ///< previews.pack is to be written
    bool stampsDirty = false;  ///< only the stamps pack is to be written
    qint64 bytes = 0;
    quint64 used = 0;
};
struct State {
    std::mutex mtx;
    CacheLocation location;
    std::map<fs::path, Folder> folders;
    qint64 bytes = 0;
    quint64 tick = 0;
    bool discarded = false;
    WriteScheduler* scheduler = nullptr;  ///< lives on the main thread (made by setLibrary)
    std::mutex writeMtx;
    std::atomic<int> reads{0}, writes{0}, stampWrites{0};
};
State& state() {
    static State s;
    return s;
}
QThreadPool& pool() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(std::max(1, std::min(3, QThread::idealThreadCount() / 2)));
        return tp;
    }();
    return *p;
}
/// Writes the packs (not held up by the previews being drawn)
QThreadPool& writer() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(1);
        return tp;
    }();
    return *p;
}

int titleOf(const DocumentItem& item) { return DocumentPlaces::titlePage(DocumentPlaces::keyOf(item)); }

/// What a stored preview shows: the document's files as they are, and its title page.
QString stampOf(const DocumentItem& item) {
    return documentStamp(item) + QStringLiteral("title=") + QString::number(titleOf(item));
}

void add(State& s, Folder& f, const QString& name, Stored stored) {
    if (auto it = f.entries.find(name); it != f.entries.end()) {
        f.bytes -= it->second.png.size();
        s.bytes -= it->second.png.size();
    }
    f.bytes += stored.png.size();
    s.bytes += stored.png.size();
    f.entries[name] = std::move(stored);
}

void take(State& s, Folder& f, std::map<QString, Stored>::iterator it) {
    f.bytes -= it->second.png.size();
    s.bytes -= it->second.png.size();
    f.entries.erase(it);
}

/// Keep the memory for previews under the budget: the folders used least recently go (not those still to be
/// written, nor `keep`). The lock is held.
void trim(State& s, const fs::path& keep) {
    while (s.bytes > BUDGET) {
        auto oldest = s.folders.end();
        for (auto it = s.folders.begin(); it != s.folders.end(); ++it) {
            if (!it->second.dirty && !it->second.stampsDirty && it->first != keep && (oldest == s.folders.end() || it->second.used < oldest->second.used)) {
                oldest = it;
            }
        }
        if (oldest == s.folders.end()) {
            return;
        }
        s.bytes -= oldest->second.bytes;
        s.folders.erase(oldest);
    }
}

void changed(State& s) {
    if (s.scheduler) {
        s.scheduler->changed();
    }
}

/// Read the pack of a folder of the library into memory, if not done yet. Not a big one if `big` is false.
/// Returns whether it is in memory.
bool ensureLoaded(const fs::path& folder, bool big) {
    auto& s = state();
    CacheLocation location;
    {
        std::lock_guard lock(s.mtx);
        if (s.discarded || !s.location.contains(folder)) {
            return false;
        }
        if (auto it = s.folders.find(folder); it != s.folders.end()) {
            it->second.used = ++s.tick;
            return true;
        }
        location = s.location;
    }
    fs::path dir = location.dirOf(folder);
    std::error_code ec;
    if (!fs::exists(Packs::fileOf(dir, PreviewCache::PACK), ec)) {
        // where it may have been kept before (a folder that cannot be written now, the other cache location)
        const fs::path other = dir == location.inFolder(folder) ? location.mirrorOf(folder) : location.inFolder(folder);
        if (!other.empty() && fs::exists(Packs::fileOf(other, PreviewCache::PACK), ec)) {
            dir = other;
        }
    }
    if (!big) {
        const auto size = fs::file_size(Packs::fileOf(dir, PreviewCache::PACK), ec);
        if (!ec && static_cast<qint64>(size) >= BIG_PACK) {
            return false;
        }
    }
    Folder f;
    if (auto entries = Packs::read(dir, PreviewCache::PACK, PreviewCache::FORMAT)) {
        ++s.reads;
        for (auto it = entries->cbegin(); it != entries->cend(); ++it) {
            const QString name = it.key().toString();
            if (!fs::exists(folder / name.toStdString(), ec)) {
                f.dirty = true;  // its document is gone
                continue;
            }
            const QCborMap e = it.value().toMap();
            const QString stamp = e.value(QStringLiteral("stamp")).toString();
            Stored stored{stamp, e.value(QStringLiteral("png")).toByteArray(), stamp};
            f.bytes += stored.png.size();
            f.entries[name] = std::move(stored);
        }
    }
    // Previews that newer versions of their documents showed the same (only if previews.pack has what they were
    // compared with)
    if (auto stamps = Packs::read(dir, PreviewCache::STAMPS_PACK, PreviewCache::FORMAT)) {
        for (auto it = stamps->cbegin(); it != stamps->cend(); ++it) {
            const QCborMap e = it.value().toMap();
            auto entry = f.entries.find(it.key().toString());
            if (entry != f.entries.end() && entry->second.packStamp == e.value(QStringLiteral("of")).toString()) {
                entry->second.stamp = e.value(QStringLiteral("stamp")).toString();
            } else {
                f.stampsDirty = true;  // (left over: dropped)
            }
        }
    }
    std::lock_guard lock(s.mtx);
    if (s.discarded || s.location.root() != location.root() || s.location.mode() != location.mode()) {
        return false;  // another library meanwhile
    }
    auto [it, inserted] = s.folders.try_emplace(folder, std::move(f));
    if (inserted) {
        s.bytes += it->second.bytes;
        if (it->second.dirty || it->second.stampsDirty) {
            changed(s);
        }
    }
    it->second.used = ++s.tick;
    trim(s, folder);
    return true;
}

QByteArray lookup(const DocumentItem& item, const QString& stamp) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto f = s.folders.find(item.folder());
    if (f == s.folders.end()) {
        return {};
    }
    auto it = f->second.entries.find(QString::fromStdString(item.main().filename().string()));
    return it != f->second.entries.end() && it->second.stamp == stamp ? it->second.png : QByteArray();
}

/// The stored preview of a document, whatever version it shows.
QByteArray lookupAny(const DocumentItem& item) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto f = s.folders.find(item.folder());
    if (f == s.folders.end()) {
        return {};
    }
    auto it = f->second.entries.find(QString::fromStdString(item.main().filename().string()));
    return it != f->second.entries.end() ? it->second.png : QByteArray();
}

/// The stored PNG `stored` shows the same as `img` (just encoded as `png`).
bool samePicture(const QByteArray& stored, const QByteArray& png, const QImage& img) {
    if (stored.isEmpty()) {
        return false;
    }
    if (stored == png) {
        return true;
    }
    QImage old;
    return old.loadFromData(stored, "PNG") && old.size() == img.size() &&
           old.convertToFormat(QImage::Format_ARGB32) == img.convertToFormat(QImage::Format_ARGB32);
}

/// The stored preview `png` is right for this version of the document too: only the stamps pack is written (the
/// folder's previews.pack is left alone). Returns false if the preview changed meanwhile.
bool confirm(const DocumentItem& item, const QString& stamp, const QByteArray& png) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto f = s.folders.find(item.folder());
    if (f == s.folders.end() || s.discarded) {
        return true;  // (as store())
    }
    auto it = f->second.entries.find(QString::fromStdString(item.main().filename().string()));
    if (it == f->second.entries.end() || it->second.png != png) {
        return false;
    }
    if (it->second.stamp != stamp) {
        it->second.stamp = stamp;
        f->second.stampsDirty = true;
        changed(s);
    }
    f->second.used = ++s.tick;
    return true;
}

void store(const DocumentItem& item, const QString& stamp, const QByteArray& png) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto f = s.folders.find(item.folder());
    if (f == s.folders.end() || s.discarded) {
        return;  // (its pack is not in memory: it would be written without the others)
    }
    add(s, f->second, QString::fromStdString(item.main().filename().string()), {stamp, png, stamp});
    f->second.dirty = true;
    f->second.used = ++s.tick;
    trim(s, item.folder());
    changed(s);
}

bool writeChanged() {
    auto& s = state();
    std::lock_guard writeLock(s.writeMtx);
    struct Job {
        fs::path folder;
        std::map<QString, Stored> entries;
        bool previews = false;  ///< previews.pack (else only the stamps pack)
    };
    std::vector<Job> jobs;
    CacheLocation location;
    {
        std::lock_guard lock(s.mtx);
        if (s.discarded) {
            return false;
        }
        location = s.location;
        for (auto& [folder, f]: s.folders) {
            if (!f.dirty && f.stampsDirty) {
                // Only newer versions that look the same: their stamps go into the small pack, unless previews.pack
                // is not where it is written (read from the other cache location: it moves now)
                std::error_code ec;
                f.dirty = !fs::exists(Packs::fileOf(location.dirOf(folder), PreviewCache::PACK), ec);
            }
            if (f.dirty) {
                for (auto& [name, stored]: f.entries) {
                    stored.packStamp = stored.stamp;  // (as written now)
                }
            }
            if (f.dirty || f.stampsDirty) {
                jobs.push_back({folder, f.entries, f.dirty});  // (the PNG data is shared, not copied)
                f.dirty = f.stampsDirty = false;
            }
        }
    }
    bool ok = true;
    for (const Job& job: jobs) {
        QCborMap pack, stamps;
        std::error_code ec;
        for (const auto& [name, stored]: job.entries) {
            if (!fs::exists(job.folder / name.toStdString(), ec)) {
                continue;  // (gone meanwhile: left out)
            }
            if (job.previews) {
                pack.insert(name, QCborMap{{QStringLiteral("stamp"), stored.stamp}, {QStringLiteral("png"), stored.png}});
            } else if (stored.stamp != stored.packStamp) {
                stamps.insert(name, QCborMap{{QStringLiteral("stamp"), stored.stamp}, {QStringLiteral("of"), stored.packStamp}});
            }
        }
        const fs::path dir = location.dirOf(job.folder);
        if (!job.previews) {
            // (a few bytes per document; previews.pack is left alone)
            ok = Packs::write(dir, PreviewCache::STAMPS_PACK, PreviewCache::FORMAT, stamps, false) && ok;
            ++s.stampWrites;
            continue;
        }
        if (pack.isEmpty()) {
            Packs::remove(dir, PreviewCache::PACK);
            Packs::remove(dir, PreviewCache::STAMPS_PACK);
            fs::remove(dir, ec);  // (only if nothing else is in it)
        } else {
            // (PNG: compressed already)
            ok = Packs::write(dir, PreviewCache::PACK, PreviewCache::FORMAT, pack, false) && ok;
            Packs::remove(dir, PreviewCache::STAMPS_PACK);  // (all stamps are in previews.pack now)
        }
        ++s.writes;
    }
    return ok;
}

/// The file name under which previews were stored as PNG files: from the path, the files and the title page (the
/// first page keeps the name it had before there were title pages).
QString pngName(const DocumentItem& item) {
    const int title = titleOf(item);
    const QByteArray titlePart = title > 0 ? QByteArray("\ntitle=") + QByteArray::number(title) : QByteArray();
    return QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::fromStdString(item.main().string()) + '\n' +
                                             documentStamp(item).toUtf8() + titlePart,
                                     QCryptographicHash::Sha1)
                    .toHex()
                    .left(24));
}

bool inLibrary(const DocumentItem& item) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    return s.location.contains(item.main());
}

QImage render(const DocumentItem& item) {
    if (item.xopp.empty() && !item.image.empty()) {
        return ImageFile::read(item.image, PreviewCache::WIDTH);  // an image alone: a thumbnail of it
    }
    if (!item.md.empty() || item.kind() == DocumentItem::Kind::Text) {
        // A Markdown file: its title page as it opens (enough of its text for the pages up to it); a text file the
        // same, as plain text
        const size_t title = static_cast<size_t>(std::max(0, titleOf(item)));
        const size_t bytes = std::min(MarkdownFile::MAX_BYTES, (title + 1) * 16384);
        auto doc = MarkdownFile::document(item.md.empty() ? MarkdownFile::readAsPlainText(item.other, bytes)
                                                          : MarkdownFile::read(item.md, bytes),
                                          title + 1);
        return ThumbnailProvider::renderDocument(*doc, std::min(title, doc->getPageCount() - 1), PreviewCache::WIDTH);
    }
    auto loaded = DocumentSession::loadFile(item.main());
    if (!loaded.document || loaded.document->getPageCount() == 0) {
        return {};
    }
    const size_t title = static_cast<size_t>(std::max(0, titleOf(item)));
    return ThumbnailProvider::renderDocument(*loaded.document, std::min(title, loaded.document->getPageCount() - 1),
                                             PreviewCache::WIDTH);
}

class PreviewResponse final: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    QImage image;
};
}  // namespace

void PreviewCache::setLibrary(const CacheLocation& location) {
    auto& s = state();
    if (!s.scheduler && QCoreApplication::instance()) {
        s.scheduler = new WriteScheduler([] { writer().start([] { writeChanged(); }); });  // (lives as long as the app)
        s.scheduler->moveToThread(QCoreApplication::instance()->thread());
    }
    flush();  // what the library before has not written yet
    std::lock_guard lock(s.mtx);
    s.location = location;
    s.folders.clear();
    s.bytes = 0;
    s.discarded = false;
}

fs::path PreviewCache::outsideFile(const DocumentItem& item) {
    return Util::getCacheSubfolder("previews") / (pngName(item).toStdString() + ".png");
}

QImage PreviewCache::stored(const DocumentItem& item) {
    QImage img;
    if (!inLibrary(item)) {
        img.load(QString::fromStdString(outsideFile(item).string()), "PNG");
        return img;
    }
    if (ensureLoaded(item.folder(), false)) {
        if (const QByteArray png = lookup(item, stampOf(item)); !png.isEmpty()) {
            img.loadFromData(png, "PNG");
        }
    }
    return img;
}

QImage PreviewCache::preview(const DocumentItem& item) {
    if (!inLibrary(item)) {
        const QString cached = QString::fromStdString(outsideFile(item).string());
        QImage img;
        if (img.load(cached, "PNG")) {
            return img;
        }
        // Only one document is read and drawn at a time: reading a document (upstream's loader and poppler) is
        // not made for several threads, and two workers asked for the same preview would write the same file.
        static std::mutex outsideMutex;
        std::lock_guard renderLock(outsideMutex);
        if (img.load(cached, "PNG")) {
            return img;
        }
        img = render(item);
        const QString tmp = cached + ".part";  // (a reader never sees half a file)
        if (!img.isNull() && img.save(tmp, "PNG")) {
            QFile::remove(cached);
            QFile::rename(tmp, cached);
        }
        return img;
    }
    const QString stamp = stampOf(item);
    ensureLoaded(item.folder(), true);
    QImage img;
    if (const QByteArray png = lookup(item, stamp); !png.isEmpty() && img.loadFromData(png, "PNG")) {
        return img;
    }
    static std::mutex renderMutex;  // (as above)
    std::lock_guard renderLock(renderMutex);
    if (const QByteArray png = lookup(item, stamp); !png.isEmpty() && img.loadFromData(png, "PNG")) {
        return img;  // another worker made it while we waited
    }
    // The document changed (or its title page): drawn again. If it looks as before (e.g. a later page was
    // edited), the stored preview is kept and only marked valid for this version, so the folder's previews.pack
    // (big, uploaded whole by sync clients) is not written again.
    const QByteArray before = lookupAny(item);
    img = render(item);
    if (!img.isNull()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        if (!samePicture(before, png, img) || !confirm(item, stamp, before)) {
            store(item, stamp, png);
        }
    }
    return img;
}

QString PreviewCache::url(const DocumentItem& item) {
    const QByteArray path = QByteArray::fromStdString(item.main().string())
                                    .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    // The stamp only makes the URL change with the files (QML caches images by URL).
    const QString stamp = QString::fromLatin1(
            QCryptographicHash::hash(documentStamp(item).toUtf8() + '\n' + QByteArray::number(titleOf(item)),
                                     QCryptographicHash::Md5)
                    .toHex()
                    .left(8));
    return QStringLiteral("image://preview/") + QString::fromLatin1(path) + '/' + stamp;
}

void PreviewCache::prune(const std::vector<DocumentItem>& items) {
    std::set<fs::path> alive;
    for (const auto& item: items) {
        alive.insert(item.main());
    }
    auto& s = state();
    std::lock_guard lock(s.mtx);
    bool any = false;
    for (auto& [folder, f]: s.folders) {
        for (auto it = f.entries.begin(); it != f.entries.end();) {
            if (alive.count(folder / it->first.toStdString())) {
                ++it;
                continue;
            }
            auto gone = it++;
            take(s, f, gone);
            f.dirty = any = true;
        }
    }
    if (any) {
        changed(s);
    }
}

void PreviewCache::moved(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    auto& s = state();
    for (const auto& [from, to]: moves) {
        std::error_code ec;
        if (fs::is_directory(to, ec)) {
            // A folder: its packs came along; the previews in memory get their new folders
            std::lock_guard lock(s.mtx);
            std::vector<fs::path> moved;
            for (const auto& [folder, f]: s.folders) {
                if (DocumentFiles::remap(folder, from, to) != folder) {
                    moved.push_back(folder);
                }
            }
            for (const fs::path& folder: moved) {
                Folder f = std::move(s.folders[folder]);
                s.folders.erase(folder);
                const fs::path target = DocumentFiles::remap(folder, from, to);
                if (s.location.contains(target)) {
                    s.folders[target] = std::move(f);
                } else {
                    s.bytes -= f.bytes;
                }
            }
            continue;
        }
        const DocumentItem item = DocumentFiles::itemOf(to, DocumentFiles::TextFiles);
        if (!item.valid() || item.main() != to) {
            continue;
        }
        // A document: its preview goes with it into its new folder (still right if its files kept their size and
        // time), if both packs are in memory or small
        if (!ensureLoaded(from.parent_path(), false) || !ensureLoaded(to.parent_path(), false)) {
            continue;
        }
        std::lock_guard lock(s.mtx);
        auto source = s.folders.find(from.parent_path());
        auto target = s.folders.find(to.parent_path());
        if (source == s.folders.end() || target == s.folders.end()) {
            continue;
        }
        auto it = source->second.entries.find(QString::fromStdString(from.filename().string()));
        if (it == source->second.entries.end()) {
            continue;
        }
        Stored stored = it->second;
        take(s, source->second, it);
        source->second.dirty = true;
        // (the stamp has the size and time: a .xopp written again with its PDF's new path gets a new preview)
        add(s, target->second, QString::fromStdString(to.filename().string()), std::move(stored));
        target->second.dirty = true;
        changed(s);
    }
}

bool PreviewCache::flush() {
    auto& s = state();
    writer().waitForDone();
    if (s.scheduler && QThread::currentThread() == s.scheduler->thread()) {
        s.scheduler->cancel();
    }
    return writeChanged();
}

int PreviewCache::convertOldFiles(const fs::path& dir, const std::vector<DocumentItem>& items) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return 0;
    }
    int count = 0;
    for (const auto& item: items) {
        const fs::path png = dir / (pngName(item).toStdString() + ".png");
        if (!inLibrary(item) || !fs::exists(png, ec) || !ensureLoaded(item.folder(), true)) {
            continue;
        }
        const QString stamp = stampOf(item);
        if (!lookup(item, stamp).isEmpty()) {
            continue;  // (made since)
        }
        QFile f(QString::fromStdString(png.string()));
        if (f.open(QIODevice::ReadOnly)) {
            store(item, stamp, f.readAll());
            ++count;
        }
    }
    return count;
}

void PreviewCache::discard() {
    auto& s = state();
    writer().waitForDone();
    if (s.scheduler) {
        s.scheduler->cancel();
    }
    std::lock_guard lock(s.mtx);
    s.discarded = true;
    s.folders.clear();
    s.bytes = 0;
}

void PreviewCache::setWriteDelays(int quietMs, int maxDelayMs) {
    if (auto* scheduler = state().scheduler) {
        scheduler->setDelays(quietMs, maxDelayMs);
    }
}

int PreviewCache::packsRead() { return state().reads.load(); }
int PreviewCache::packsWritten() { return state().writes.load(); }
int PreviewCache::stampPacksWritten() { return state().stampWrites.load(); }

void PreviewProvider::shutdown() {
    pool().clear();
    pool().waitForDone();
    PreviewCache::flush();
}

QQuickImageResponse* PreviewProvider::requestImageResponse(const QString& id, const QSize& /*requestedSize*/) {
    auto* response = new PreviewResponse;
    const fs::path file(QByteArray::fromBase64(id.section('/', 0, 0).toLatin1(),
                                               QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
                                .toStdString());
    pool().start([response, file] {
        QImage img;
        if (const DocumentItem item = DocumentFiles::itemOf(file, DocumentFiles::TextFiles); item.valid()) {
            img = PreviewCache::preview(item);
        }
        QMetaObject::invokeMethod(
                response,
                [response, img = std::move(img)]() mutable {
                    response->image = std::move(img);
                    Q_EMIT response->finished();
                },
                Qt::QueuedConnection);
    });
    return response;
}

}  // namespace xqt
