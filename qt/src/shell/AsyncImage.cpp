#include "AsyncImage.h"

#include <QCryptographicHash>
#include <QMetaObject>
#include <QQuickTextureFactory>

namespace xqt {

QQuickTextureFactory* AsyncImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(image);
}

void AsyncImageResponse::finish(QImage img) {
    QMetaObject::invokeMethod(
            this,
            [this, img = std::move(img)]() mutable {
                image = std::move(img);
                Q_EMIT finished();
            },
            Qt::QueuedConnection);
}

namespace {
constexpr auto BASE64URL = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
}

QString urlEncode(const QString& s) { return QString::fromLatin1(s.toUtf8().toBase64(BASE64URL)); }

QString urlDecode(const QString& part) { return QString::fromUtf8(QByteArray::fromBase64(part.toLatin1(), BASE64URL)); }

QString urlEncodePath(const fs::path& path) {
    return QString::fromLatin1(QByteArray::fromStdString(path.string()).toBase64(BASE64URL));
}

fs::path urlDecodePath(const QString& part) {
    return fs::path(QByteArray::fromBase64(part.toLatin1(), BASE64URL).toStdString());
}

QString urlStamp(const QByteArray& stamp) {
    return QString::fromLatin1(QCryptographicHash::hash(stamp, QCryptographicHash::Md5).toHex().left(8));
}

}  // namespace xqt
