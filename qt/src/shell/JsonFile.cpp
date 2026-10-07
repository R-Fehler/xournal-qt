#include "JsonFile.h"

#include <QJsonDocument>

#include "session/FileIo.h"

namespace xqt {

QJsonObject readJsonObject(const fs::path& file) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(fileio::readFile(file))).object();
}

}  // namespace xqt
