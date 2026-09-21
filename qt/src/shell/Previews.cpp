#include "Previews.h"

#include "DocumentPlaces.h"

#include <mutex>
#include <set>

#include <QCryptographicHash>
#include <QFile>
#include <QThreadPool>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "util/PathUtil.h"

#include "Library.h"
#include "Thumbnails.h"

namespace xqt {

namespace {
struct State {
    std::mutex mtx;
    fs::path root, dir;
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

class PreviewResponse final: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    QImage image;
};
}  // namespace

void PreviewCache::setLibrary(const fs::path& root, const fs::path& dir) {
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.root = root;
    s.dir = dir;
}

fs::path PreviewCache::cacheFile(const DocumentItem& item) {
    const std::string main = item.main().string();
    // The preview shows the title page; another one than the first is part of the name (the first keeps the names
    // of the previews stored before there were title pages)
    const int title = DocumentPlaces::titlePage(DocumentPlaces::keyOf(item));
    const QByteArray titlePart = title > 0 ? QByteArray("\ntitle=") + QByteArray::number(title) : QByteArray();
    const QString name = QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::fromStdString(main) + '\n' + documentStamp(item).toUtf8() + titlePart,
                                     QCryptographicHash::Sha1)
                    .toHex()
                    .left(24));
    auto& s = state();
    std::lock_guard lock(s.mtx);
    if (!s.dir.empty() && DocumentFiles::remap(item.main(), s.root, "/") != item.main()) {
        return s.dir / (name.toStdString() + ".png");
    }
    return Util::getCacheSubfolder("previews") / (name.toStdString() + ".png");
}

QImage PreviewCache::preview(const DocumentItem& item) {
    const fs::path file = cacheFile(item);
    const QString cached = QString::fromStdString(file.string());
    QImage img;
    if (img.load(cached, "PNG")) {
        return img;
    }
    // Only one document is read and drawn at a time: reading a document (upstream's loader and poppler) is not
    // made for several threads, and two workers asked for the same preview would write the same file twice.
    static std::mutex renderMutex;
    std::lock_guard renderLock(renderMutex);
    if (img.load(cached, "PNG")) {
        return img;  // another worker made it while we waited
    }
    auto loaded = DocumentSession::loadFile(item.main());
    if (!loaded.document || loaded.document->getPageCount() == 0) {
        return {};
    }
    const size_t title = static_cast<size_t>(DocumentPlaces::titlePage(DocumentPlaces::keyOf(item)));
    img = ThumbnailProvider::renderDocument(*loaded.document, std::min(title, loaded.document->getPageCount() - 1), WIDTH);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // Written under another name first: a reader never sees half a file.
    const QString tmp = cached + ".part";
    if (img.save(tmp, "PNG")) {
        QFile::remove(cached);
        QFile::rename(tmp, cached);
    }
    return img;
}

QString PreviewCache::url(const DocumentItem& item) {
    const QByteArray path = QByteArray::fromStdString(item.main().string())
                                    .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    // The stamp only makes the URL change with the files (QML caches images by URL).
    const QString stamp = QString::fromLatin1(
            QCryptographicHash::hash(documentStamp(item).toUtf8() + '\n' +
                                             QByteArray::number(DocumentPlaces::titlePage(DocumentPlaces::keyOf(item))),
                                     QCryptographicHash::Md5)
                    .toHex()
                    .left(8));
    return QStringLiteral("image://preview/") + QString::fromLatin1(path) + '/' + stamp;
}

void PreviewCache::prune(const std::vector<DocumentItem>& items) {
    fs::path dir;
    {
        auto& s = state();
        std::lock_guard lock(s.mtx);
        dir = s.dir;
    }
    if (dir.empty()) {
        return;
    }
    std::set<std::string> keep;
    for (const auto& item: items) {
        keep.insert(cacheFile(item).filename().string());
    }
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() == ".png" && !keep.count(it->path().filename().string())) {
            std::error_code rec;
            fs::remove(it->path(), rec);
        }
    }
}

void PreviewProvider::shutdown() {
    pool().clear();
    pool().waitForDone();
}

QQuickImageResponse* PreviewProvider::requestImageResponse(const QString& id, const QSize& /*requestedSize*/) {
    auto* response = new PreviewResponse;
    const fs::path file(QByteArray::fromBase64(id.section('/', 0, 0).toLatin1(),
                                               QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
                                .toStdString());
    pool().start([response, file] {
        QImage img;
        if (const DocumentItem item = DocumentFiles::itemOf(file); item.valid()) {
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
