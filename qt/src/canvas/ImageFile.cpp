#include "ImageFile.h"

#include <algorithm>

#include <QImageReader>
#include <QString>

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

}  // namespace xqt::ImageFile
