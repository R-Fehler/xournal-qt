#include "MdSnippets.h"

#include <algorithm>
#include <atomic>
#include <list>
#include <memory>
#include <mutex>

#include <QCryptographicHash>
#include <QPainter>
#include <QThreadPool>

#include <cairo.h>

#include "session/TextMatch.h"

#include "Library.h"
#include "MarkdownFile.h"
#include "MdLayout.h"
#include "MdPassages.h"

namespace xqt {

namespace {
constexpr size_t MAX_FILES = 8;
/// The width a card's text is laid out for (points), and the space around it
constexpr double TEXT_WIDTH = 220;
constexpr double PADDING = 6;

/// A parsed file.
struct Parsed {
    std::string source;
    md::Document doc;
    std::vector<md::Passage> passages;
};

struct Caches {
    std::mutex mtx;
    std::list<std::pair<QString, std::shared_ptr<const Parsed>>> files;  ///< most recently used first; key: path + stamp
    std::atomic<int> parses{0};

    std::shared_ptr<const Parsed> file(const fs::path& path) {
        const QString key = QString::fromStdString(path.string()) + '|' + fileStamp(path);
        {
            std::lock_guard lock(mtx);
            for (auto it = files.begin(); it != files.end(); ++it) {
                if (it->first == key) {
                    files.splice(files.begin(), files, it);
                    return files.front().second;
                }
            }
        }
        // Read as the index and the opened document read it
        auto parsed = std::make_shared<Parsed>();
        parsed->source = MarkdownFile::read(path);
        parsed->doc = md::parse(parsed->source);
        parsed->passages = md::passages(parsed->doc);
        ++parses;
        std::lock_guard lock(mtx);
        files.emplace_front(key, parsed);
        while (files.size() > MAX_FILES) {
            files.pop_back();
        }
        return parsed;
    }
    void clear() {
        std::lock_guard lock(mtx);
        files.clear();
    }
};
Caches& caches() {
    static Caches c;
    return c;
}

QThreadPool& pool() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(std::max(1, std::min(2, QThread::idealThreadCount() / 2)));
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

/// The places of the matches of `query` in a passage's text, as the index finds them (in its simplified text):
/// source ranges, in order.
std::vector<std::pair<size_t, size_t>> matchesIn(const md::Passage& p, const QString& query) {
    std::vector<std::pair<size_t, size_t>> out;
    const QString prepared = textmatch::prepare(LibraryIndex::simplified(query));
    if (prepared.isEmpty()) {
        return out;
    }
    // UTF-16 index -> byte of the passage's text
    const QString text = QString::fromStdString(p.text);
    std::vector<size_t> byteOf;
    byteOf.reserve(static_cast<size_t>(text.size()) + 1);
    size_t byte = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        byteOf.push_back(byte);
        const char16_t c = text.at(i).unicode();
        if (QChar::isLowSurrogate(c)) {
            continue;  // (the pair's 4 bytes were counted at its first half)
        }
        byte += c < 0x80 ? 1 : c < 0x800 ? 2 : QChar::isHighSurrogate(c) ? 4 : 3;
    }
    byteOf.push_back(byte);
    const textmatch::Simplified simple = textmatch::simplify(text);
    for (const textmatch::Span& m: textmatch::find(simple.text, prepared)) {
        const auto from = static_cast<size_t>(simple.origin[static_cast<size_t>(m.start)]);
        const auto to = static_cast<size_t>(simple.origin[static_cast<size_t>(m.end - 1)]) + 1;
        const auto range = md::sourceRange(p, byteOf[std::min(from, byteOf.size() - 1)],
                                           byteOf[std::min(to, byteOf.size() - 1)]);
        if (range.first != md::NO_SOURCE) {
            out.push_back(range);
        }
    }
    return out;
}

class SnippetResponse final: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    void cancel() override { cancelled = true; }
    QImage image;
    std::atomic<bool> cancelled{false};
};
}  // namespace

QString MdSnippetProvider::baseUrl(const DocumentItem& item, const QString& query) {
    const QString stamp = QString::fromLatin1(
            QCryptographicHash::hash(documentStamp(item).toUtf8(), QCryptographicHash::Md5).toHex().left(8));
    return QStringLiteral("image://mdsnippet/") + encode(QString::fromStdString(item.main().string())) + '/' + stamp +
           '/' + encode(LibraryIndex::simplified(query).trimmed());
}

QImage MdSnippetProvider::render(const fs::path& file, int passage, const QString& query, int width, int maxHeight) {
    const auto parsed = caches().file(file);
    if (passage < 0 || static_cast<size_t>(passage) >= parsed->passages.size() || width <= 0) {
        return {};
    }
    const md::Passage& p = parsed->passages[static_cast<size_t>(passage)];
    md::Style style = MarkdownFile::style();
    style.width = TEXT_WIDTH;
    const md::Layout layout = md::layout(md::snippet(parsed->doc, p), style);
    const double scale = width / (TEXT_WIDTH + 2 * PADDING);

    // The hits (where they are drawn), and the part shown: all of it, or a few lines around the first hit
    std::vector<std::vector<md::Rect>> marks;
    for (const auto& [begin, end]: matchesIn(p, query)) {
        marks.push_back(md::sourceRects(layout, begin, end));
    }
    double top = 0;
    double height = layout.height;
    if (maxHeight > 0) {
        const double most = std::max(20.0, maxHeight / scale - 2 * PADDING);
        if (height > most) {
            double hit = 0;
            if (!marks.empty() && !marks.front().empty()) {
                hit = marks.front().front().y;
            }
            top = std::clamp(hit - most / 3, 0.0, layout.height - most);
            height = most;
        }
    }
    QImage img(width, std::max(1, static_cast<int>(std::ceil((height + 2 * PADDING) * scale))),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(img.bits(), CAIRO_FORMAT_ARGB32, img.width(),
                                                                   img.height(), static_cast<int>(img.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, PADDING, PADDING - top);
    md::draw(cr, layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    // Marked like the hits on the pages (multiplied, so the text stays readable); the first one is the current hit
    // when the card is opened
    QPainter painter(&img);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (size_t i = 0; i < marks.size(); ++i) {
        const QColor color = i == 0 ? QColor(255, 160, 40) : QColor(255, 214, 0);
        for (const md::Rect& r: marks[i]) {
            painter.fillRect(QRectF((r.x + PADDING) * scale - 1, (r.y + PADDING - top) * scale - 1,
                                    r.width * scale + 2, r.height * scale + 2),
                             color);
        }
    }
    return img;
}

void MdSnippetProvider::clearCaches() { caches().clear(); }

int MdSnippetProvider::parseCount() { return caches().parses; }

void MdSnippetProvider::shutdown() {
    pool().clear();
    pool().waitForDone();
}

QQuickImageResponse* MdSnippetProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new SnippetResponse;
    // id: <path>/<stamp>/<query>/<passage>
    const QStringList parts = id.split('/');
    const fs::path file(decode(parts.value(0)).toStdString());
    const QString query = decode(parts.value(2));
    const int passage = parts.value(3).toInt();
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 240;
    const int maxHeight = std::max(0, requestedSize.height());
    pool().start([response, file, query, passage, width, maxHeight] {
        QImage img;
        if (!response->cancelled) {  // scrolled away meanwhile: not drawn
            img = render(file, passage, query, width, maxHeight);
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
