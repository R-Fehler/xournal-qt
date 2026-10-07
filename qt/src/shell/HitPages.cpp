#include "HitPages.h"

#include <algorithm>
#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include <QPainter>

#include "model/Document.h"
#include "model/XojPage.h"
#include "render/RegionImage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentImages.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"

#include "AsyncImage.h"
#include "FileStamps.h"
#include "ImageMemory.h"
#include "ImageWorkers.h"
#include "LibraryIndex.h"
#include "MarkdownFile.h"
#include "MdImages.h"
#include "Thumbnails.h"

namespace xqt {

namespace {

/// A loaded document; its mutex lets one thread at a time draw or search it.
struct CachedDocument {
    std::mutex mtx;
    std::unique_ptr<Document> doc;
    bool loaded = false;
    std::unique_ptr<PdfLayoutReader> pdfText;  ///< the text of its PDF pages (terms of the fuzzy search)
    std::unique_ptr<md::images::RootHandle> pictures;  ///< a Markdown file: where its pictures are
};

struct Caches {
    std::mutex mtx;
    /// Most recently used first.
    std::list<std::pair<QString, std::shared_ptr<CachedDocument>>> documents;  ///< key: path + stamp
    /// Drawn pages without marks, by document, page and width (the one drawn last stays even if it is bigger)
    LruImageCache<QString> images{ImageMemory::HIT_PAGE_BYTES, 1};
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
        while (documents.size() > ImageMemory::HIT_PAGE_DOCUMENTS) {
            documents.pop_back();  // still used by a running render: kept alive by its shared_ptr
        }
        return documents.front().second;
    }
    void clear() {
        {
            std::lock_guard lock(mtx);
            documents.clear();
        }
        images.clear();
    }
};
Caches& caches() {
    static Caches c;
    return c;
}

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
    return QStringLiteral("image://hitpage/") + urlEncodePath(item.main()) + '/' + urlStamp(documentStamp(item).toUtf8()) +
           '/' + urlEncode(marks.startsWith(TERMS) ? marks : LibraryIndex::simplified(marks).trimmed());
}

namespace {
/// The kept document of `item` (loaded if needed; the caller holds its mutex)
void load(CachedDocument& cached, const DocumentItem& item) {
    if (!cached.loaded) {
        cached.loaded = true;
        // One document is read at a time: the loader (and poppler behind it) is not made for several threads.
        static std::mutex loading;
        std::lock_guard loadLock(loading);
        if (!item.md.empty()) {
            // A Markdown file (its bookmarks, qt/docs/features/bookmarks.md): its pages as it opens, with its pictures
            cached.pictures = std::make_unique<md::images::RootHandle>(DocumentImages::markdownRoot(item.md));
            cached.doc = MarkdownFile::document(MarkdownFile::read(item.md));
        } else {
            cached.doc = DocumentSession::loadFile(item.main()).document;
        }
    }
}
}  // namespace

QString HitPageProvider::areaUrl(const QString& base, int page, const QRectF& area) {
    return base + '/' + QString::number(page) + QStringLiteral("/area/") +
           QStringLiteral("%1,%2,%3,%4").arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
}

QImage HitPageProvider::renderArea(const fs::path& file, int pageNo, const QRectF& area, int width) {
    const DocumentItem item = DocumentFiles::itemOf(file);
    if (!item.valid() || pageNo < 0 || area.width() <= 0 || area.height() <= 0) {
        return {};
    }
    width = std::clamp(width, 16, 2048);
    const QString docKey = QString::fromStdString(item.main().string()) + '|' + documentStamp(item);
    auto cached = caches().document(docKey);
    std::lock_guard lock(cached->mtx);
    load(*cached, item);
    Document* doc = cached->doc.get();
    if (!doc) {
        return {};
    }
    PageRef page;
    {
        std::shared_lock docLock(*doc);
        if (static_cast<size_t>(pageNo) >= doc->getPageCount()) {
            return {};
        }
        page = doc->getPage(static_cast<size_t>(pageNo));
    }
    region::Request r;
    r.area = {area.x(), area.y(), area.width(), area.height()};
    r.scale = width / area.width();
    r.forScreen = false;
    ++caches().renders;
    return region::renderImage(*doc, page, r);
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
    load(*cached, item);
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
    QImage img = caches().images.find(imageKey);
    if (img.isNull()) {
        img = ThumbnailProvider::renderDocument(*doc, static_cast<size_t>(pageNo), width);
        ++caches().renders;
        caches().images.put(imageKey, img);
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

QQuickImageResponse* HitPageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new AsyncImageResponse;
    // id: <path>/<stamp>/<query>/<page>
    const QStringList parts = id.split('/');
    const fs::path file = urlDecodePath(parts.value(0));
    const QString query = urlDecode(parts.value(2));
    const int page = parts.value(3).toInt();
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 200;
    // (…/<page>/area/<x>,<y>,<w>,<h>: an area of the page, areaUrl)
    const QStringList area = parts.value(4) == QLatin1String("area") ? parts.value(5).split(',') : QStringList();
    const QRectF rect = area.size() == 4 ? QRectF(area[0].toDouble(), area[1].toDouble(), area[2].toDouble(),
                                                  area[3].toDouble())
                                         : QRectF();
    ImageWorkers::respond(ImageWorkers::Pool::HitPages, response, [file, query, page, width, rect] {
        return rect.isValid() ? renderArea(file, page, rect, width) : render(file, page, query, width);
    });
    return response;
}

}  // namespace xqt
