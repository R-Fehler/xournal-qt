#include "MdImageDecoder.h"

#include <algorithm>
#include <cstring>

#include <QImage>
#include <QImageReader>
#include <QString>

#include "util/PathUtil.h"

#include "MdImages.h"

namespace xqt::MdImageDecoder {

namespace {
QString qpath(const std::string& utf8) { return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size())); }

bool size(const std::string& path, int& width, int& height) {
    QImageReader reader(qpath(path));
    reader.setDecideFormatFromContent(true);  // (a fetched web picture has no suffix)
    reader.setAutoTransform(true);
    QSize s = reader.size();
    if (!s.isValid() || s.isEmpty()) {
        return false;
    }
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
        s.transpose();  // (a photo taken upright: shown turned)
    }
    width = s.width();
    height = s.height();
    return true;
}

std::shared_ptr<md::images::Pixels> decode(const std::string& path, int width, int height) {
    QImageReader reader(qpath(path));
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    QSize natural = reader.size();
    if (!natural.isValid()) {
        return nullptr;
    }
    // The size to decode at is the picture's upright size: before it is turned, width and height swap
    const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
    const QSize want = turned ? QSize(height, width) : QSize(width, height);
    if (want != natural) {
        reader.setScaledSize(want);  // (JPEG decodes smaller at once; SVG is drawn at that size)
    }
    QImage img = reader.read();
    if (img.isNull()) {
        return nullptr;
    }
    if (img.width() != width || img.height() != height) {
        img = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);  // (Cairo's ARGB32, native byte order)
    auto out = std::make_shared<md::images::Pixels>();
    out->width = img.width();
    out->height = img.height();
    out->stride = static_cast<int>(img.bytesPerLine());
    out->data.resize(static_cast<size_t>(img.sizeInBytes()));
    std::memcpy(out->data.data(), img.constBits(), out->data.size());
    return out;
}
}  // namespace

void install() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    md::images::setDecoder({size, decode});
    const std::u8string web = Util::getCacheSubfolder("web-images").u8string();
    md::images::setWebCacheDir(std::string(web.begin(), web.end()));
}

}  // namespace xqt::MdImageDecoder
