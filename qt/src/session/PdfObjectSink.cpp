/*
 * xournal-qt: where the objects a PDF write makes go (PdfObjectSink.h).
 *
 * @license GNU GPLv2 or later
 */
#include "PdfObjectSink.h"

#include <functional>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <qpdf/Pipeline.hh>

#include "FileIo.h"

namespace xqt {

namespace {
/// Gives a file to qpdf in pieces, when the stream is written.
std::function<void(Pipeline*)> fileProvider(const fs::path& file) {
    return [file](Pipeline* p) {
        QFile f(QString::fromStdU16String(file.u16string()));
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray chunk;
            while (!(chunk = f.read(1 << 16)).isEmpty()) {
                p->write(reinterpret_cast<const unsigned char*>(chunk.constData()), static_cast<size_t>(chunk.size()));
            }
        }
        p->finish();
    };
}
}  // namespace

QPDFObjectHandle FullSink::add(QPDFObjectHandle value) { return q.makeIndirectObject(value); }

QPDFObjectHandle FullSink::addStream(QPDFObjectHandle dict, const std::string& data) {
    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&q, data);
    for (const auto& k: dict.getKeys()) {
        stream.getDict().replaceKey(k, dict.getKey(k));
    }
    return stream;
}

QPDFObjectHandle FullSink::addFileStream(QPDFObjectHandle dict, const fs::path& file) {
    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&q);
    stream.replaceStreamData(fileProvider(file), QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    for (const auto& k: dict.getKeys()) {
        stream.getDict().replaceKey(k, dict.getKey(k));
    }
    return stream;
}

QPDFObjectHandle UpdateSink::addFileStream(QPDFObjectHandle dict, const fs::path& file) {
    return u.addStream(dict, fileio::readFile(file));
}

}  // namespace xqt
