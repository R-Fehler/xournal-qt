#include "FileStamps.h"

#include <functional>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace xqt {

QString fileStamp(const fs::path& file) {
    if (file.empty()) {
        return {};
    }
    const QFileInfo info(QString::fromStdString(file.string()));
    if (!info.exists()) {
        return {};
    }
    return QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
}

QString documentStamp(const DocumentItem& item) {
    return documentStamp(item, [](const fs::path& f) { return fileStamp(f); });
}

QString documentStamp(const DocumentItem& item, const std::function<QString(const fs::path&)>& stampOf) {
    QString stamp;
    const fs::path none;
    for (const fs::path& f: {item.xopp, item.pdf, item.xopp.empty() ? none : DocumentFiles::attachmentOf(item.xopp),
                             item.xopp.empty() ? none : DocumentFiles::pagesOf(item.xopp), item.md, item.image,
                             item.other}) {
        if (!f.empty()) {
            stamp += stampOf(f) + ';';
        }
    }
    return stamp;
}

QString contentHash(const fs::path& file) {
    QFile f(QString::fromStdU16String(file.u16string()));
    if (file.empty() || !f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Blake2b_256);
    if (!hash.addData(&f)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

fs::path ownFileOf(const DocumentItem& item) {
    if (!item.xopp.empty()) {
        return item.xopp;
    }
    return item.pdf.empty() ? item.main() : fs::path();
}

/// A hash of the start and end of a file (all of a small one), with its size: two files with the same size and time
/// are not taken for each other unless it is the same too ("": cannot be read).
QString contentSample(const fs::path& file) {
    constexpr qint64 PART = 64 * 1024;
    QFile f(QString::fromStdString(file.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const qint64 size = f.size();
    hash.addData(QByteArray::number(size));
    if (size <= 2 * PART) {
        hash.addData(f.readAll());
    } else {
        hash.addData(f.read(PART));
        if (!f.seek(size - PART)) {
            return {};
        }
        hash.addData(f.read(PART));
    }
    return QString::fromLatin1(hash.result().toBase64(QByteArray::OmitTrailingEquals));
}

}  // namespace xqt
