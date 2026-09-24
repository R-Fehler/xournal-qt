#include "HitPages.h"

#include <algorithm>
#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include <QCryptographicHash>
#include <QPainter>
#include <QThreadPool>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"

#include "Library.h"
#include "Thumbnails.h"

namespace xqt {

namespace {
constexpr size_t MAX_DOCUMENTS = 12;
constexpr qsizetype MAX_IMAGE_BYTES = 128 * 1024 * 1024;

/// A loaded document; its mutex lets one thread at a time draw or search it.
struct CachedDocument {
    std::mutex mtx;
    std::unique_ptr<Document> doc;
    bool loaded = false;
    std::unique_ptr<PdfLayoutReader> pdfText;  ///< the text of its PDF pages (terms of the fuzzy search)
};

struct Caches {
    std::mutex mtx;
    /// Most recently used first.
    std::list<std::pair<QString, std::shared_ptr<CachedDocument>>> documents;  ///< key: path + stamp
    std::list<std::pair<QString, QImage>> images;                               ///< key: document, page, width
    qsizetype imageBytes = 0;
    std::atomic<int> renders{0};

    std::shared_ptr<CachedDocument> document(const QString& key) {
        std::lock_guard lock(mtx);
        for (auto it = documents.begin(); it != documents.end(); ++it) {
            if (it->first == key) {
                documents.splice(documents.begin(), documents, it);
                return documents.front().second;
            }
        }
        documents.emplace_front(key, std::make_shared<CachedDocument>());
        while (documents.size() > MAX_DOCUMENTS) {
            documents.pop_back();  // still used by a running render: kept alive by its shared_ptr
        }
        return documents.front().second;
    }
    QImage image(const QString& key) {
        std::lock_guard lock(mtx);
        for (auto it = images.begin(); it != images.end(); ++it) {
            if (it->first == key) {
                images.splice(images.begin(), images, it);
                return images.front().second;
            }
        }
        return {};
    }
    void store(const QString& key, const QImage& img) {
        std::lock_guard lock(mtx);
        images.emplace_front(key, img);
        imageBytes += img.sizeInBytes();
        while (imageBytes > MAX_IMAGE_BYTES && images.size() > 1) {
            imageBytes -= images.back().second.sizeInBytes();
            images.pop_back();
        }
    }
    void clear() {
        std::lock_guard lock(mtx);
        documents.clear();
        images.clear();
        imageBytes = 0;
    }
};
Caches& caches() {
    static Caches c;
    return c;
}

QThreadPool& pool() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(std::max(2, std::min(4, QThread::idealThreadCount() / 2)));
        return tp;
    }();
    return *p;
}

QString encode(const QString& s) {
    return QString::fromLatin1(s.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QString decode(const QString& s) {
    return QString::fromUtf8(
            QByteArray::fromBase64(s.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

class HitPageResponse final: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    void cancel() override { cancelled = true; }
    QImage image;
    std::atomic<bool> cancelled{false};
};
}  // namespace

namespace {
constexpr QChar TERMS(0x1f);  // marks that are terms of the fuzzy search
}

QString HitPageProvider::marksOf(const std::vector<textmatch::Term>& terms) { return TERMS + textmatch::encode(terms); }

std::vector<textmatch::Term> HitPageProvider::termsOf(const QString& marks) {
    if (marks.startsWith(TERMS)) {
        return textmatch::decode(QStringView(marks).sliced(1));
    }
    const QString prepared = textmatch::prepare(LibraryIndex::simplified(marks));
    return prepared.isEmpty() ? std::vector<textmatch::Term>() : std::vector<textmatch::Term>{{prepared}};
}

QString HitPageProvider::baseUrl(const DocumentItem& item, const QString& marks) {
    const QString stamp = QString::fromLatin1(
            QCryptographicHash::hash(documentStamp(item).toUtf8(), QCryptographicHash::Md5).toHex().left(8));
    return QStringLiteral("image://hitpage/") + encode(QString::fromStdString(item.main().string())) + '/' + stamp +
           '/' + encode(marks.startsWith(TERMS) ? marks : LibraryIndex::simplified(marks).trimmed());
}

QImage HitPageProvider::render(const fs::path& file, int pageNo, const QString& query, int width) {
    const DocumentItem item = DocumentFiles::itemOf(file);
    if (!item.valid() || pageNo < 0) {
        return {};
    }
    width = std::clamp((width + 63) / 64 * 64, 64, 2048);
    const QString docKey = QString::fromStdString(item.main().string()) + '|' + documentStamp(item);
    auto cached = caches().document(docKey);

    std::lock_guard lock(cached->mtx);
    if (!cached->loaded) {
        cached->loaded = true;
        // One document is read at a time: the loader (and poppler behind it) is not made for several threads.
        static std::mutex loading;
        std::lock_guard loadLock(loading);
        cached->doc = DocumentSession::loadFile(item.main()).document;
    }
    Document* doc = cached->doc.get();
    if (!doc) {
        return {};
    }
    double pageWidth = 0;
    {
        std::shared_lock docLock(*doc);
        if (static_cast<size_t>(pageNo) >= doc->getPageCount()) {
            return {};
        }
        pageWidth = doc->getPage(static_cast<size_t>(pageNo))->getWidth();
    }
    const QString imageKey = docKey + '|' + QString::number(pageNo) + '|' + QString::number(width);
    QImage img = caches().image(imageKey);
    if (img.isNull()) {
        img = ThumbnailProvider::renderDocument(*doc, static_cast<size_t>(pageNo), width);
        ++caches().renders;
        caches().store(imageKey, img);
    }
    if (pageWidth <= 0) {
        return img;
    }
    std::vector<QRectF> rects;
    if (query.startsWith(TERMS)) {
        // Found in the page's text as the search of an open document finds them (word bounds, fuzzy words: the whole
        // word), where that search marks them
        if (!cached->pdfText && !doc->getPdfFilepath().empty()) {
            cached->pdfText = std::make_unique<PdfLayoutReader>(doc->getPdfFilepath());
        }
        std::shared_lock docLock(*doc);
        rects = termRects(*doc->getPage(static_cast<size_t>(pageNo)), cached->pdfText.get(),
                          textmatch::decode(QStringView(query).sliced(1)));
    } else if (const QString q = LibraryIndex::simplified(query).trimmed(); !q.isEmpty()) {
        rects = DocumentSearch::findOnPage(*doc, static_cast<size_t>(pageNo), q.toStdString());
    }
    if (rects.empty()) {
        return img;
    }
    // Marked like the hits on the canvas; multiplied, so the text stays readable.
    QPainter p(&img);  // (a copy: the kept image stays without marks)
    p.setCompositionMode(QPainter::CompositionMode_Multiply);
    p.setRenderHint(QPainter::Antialiasing, false);
    const double scale = img.width() / pageWidth;
    for (const QRectF& r: rects) {
        p.fillRect(QRectF(r.x() * scale - 1, r.y() * scale - 1, r.width() * scale + 2, r.height() * scale + 2),
                   QColor(255, 214, 0));
    }
    return img;
}

void HitPageProvider::clearCaches() { caches().clear(); }

int HitPageProvider::renderCount() { return caches().renders; }

void HitPageProvider::shutdown() {
    pool().clear();
    pool().waitForDone();
}

QQuickImageResponse* HitPageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new HitPageResponse;
    // id: <path>/<stamp>/<query>/<page>
    const QStringList parts = id.split('/');
    const fs::path file(decode(parts.value(0)).toStdString());
    const QString query = decode(parts.value(2));
    const int page = parts.value(3).toInt();
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 200;
    pool().start([response, file, query, page, width] {
        QImage img;
        if (!response->cancelled) {  // scrolled away meanwhile: not drawn
            img = render(file, page, query, width);
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
