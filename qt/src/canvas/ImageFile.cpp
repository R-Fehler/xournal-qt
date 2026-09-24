#include "ImageFile.h"

#include <algorithm>

#include <QBuffer>
#include <QImageReader>
#include <QString>

#include <gio/gio.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/PageType.h"
#include "model/XojPage.h"

namespace xqt::ImageFile {

QImage read(const fs::path& file, int width) {
    QImageReader reader(QString::fromStdString(file.string()));
    reader.setAutoTransform(true);  // a photo's orientation tag
    if (width > 0) {
        QSize size = reader.size();  // (before it is turned)
        const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
        const int shownWidth = turned ? size.height() : size.width();
        if (size.isValid() && shownWidth > width) {
            const double f = static_cast<double>(width) / shownWidth;
            reader.setScaledSize(QSize(std::max(1, qRound(size.width() * f)), std::max(1, qRound(size.height() * f))));
        }
    }
    QImage img = reader.read();
    if (!img.isNull() && width > 0 && img.width() > width) {
        img = img.scaledToWidth(width, Qt::SmoothTransformation);  // (a format that cannot decode it smaller)
    }
    return img;
}

namespace {
/// Receives the events of the documents made here until a session owns them. It has no listeners.
DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

/// The image by its path (gdk-pixbuf reads it, as upstream's loader will). False if it cannot.
bool byPath(BackgroundImage& img, const fs::path& file) {
    GError* error = nullptr;
    img.loadFile(file, &error);
    if (error) {
        g_error_free(error);
        return false;
    }
    return img.getPixbuf() != nullptr;
}

/// The image as Qt reads it (turned upright), as a PNG stored with the document.
bool stored(BackgroundImage& img, const QImage& image, const fs::path& file) {
    QImage scaled = image;
    if (std::max(image.width(), image.height()) > MAX_STORED_SIDE) {
        scaled = image.scaled(MAX_STORED_SIDE, MAX_STORED_SIDE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!scaled.save(&buffer, "PNG")) {
        return false;
    }
    GBytes* bytes = g_bytes_new(png.constData(), static_cast<gsize>(png.size()));
    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    g_bytes_unref(bytes);
    GError* error = nullptr;
    fs::path name = file.filename();
    name.replace_extension(".png");
    img.loadFile(stream, name, &error);
    g_object_unref(stream);
    if (error) {
        g_error_free(error);
        return false;
    }
    img.setAttach(true);  // stored with the .xopp
    return img.getPixbuf() != nullptr;
}
}  // namespace

std::unique_ptr<Document> document(const fs::path& file, std::string& error) {
    QImageReader reader(QString::fromStdString(file.string()));
    reader.setAutoTransform(true);
    const QByteArray format = reader.format();
    const bool upright = reader.transformation() == QImageIOHandler::TransformationNone;
    BackgroundImage img;
    QSize size;
    if ((format == "png" || format == "jpeg") && upright && byPath(img, file)) {
        size = QSize(gdk_pixbuf_get_width(img.getPixbuf()), gdk_pixbuf_get_height(img.getPixbuf()));
    } else {
        const QImage image = reader.read();
        if (image.isNull()) {
            error = "Could not read the image \"" + file.filename().string() +
                    "\": " + reader.errorString().toStdString();
            return nullptr;
        }
        img = BackgroundImage();
        if (!stored(img, image, file)) {
            error = "Could not read the image \"" + file.filename().string() + "\".";
            return nullptr;
        }
        size = image.size();
    }
    const double scale = PAGE_SIDE / std::max(1, std::max(size.width(), size.height()));
    auto page = std::make_shared<XojPage>(std::max(1.0, size.width() * scale), std::max(1.0, size.height() * scale));
    page->setBackgroundImage(img);
    page->setBackgroundType(PageType(PageTypeFormat::Image));
    auto doc = std::make_unique<Document>(&handler());
    doc->addPage(std::move(page));
    return doc;
}

}  // namespace xqt::ImageFile
