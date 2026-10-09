#include "ElementData.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>

#include "model/Element.h"

namespace xqt::elementdata {

std::string clipboardData(const std::vector<const Element*>& elements) {
    if (std::all_of(elements.begin(), elements.end(), [](const Element* e) { return e->getData().empty(); })) {
        return {};
    }
    QJsonArray all;
    for (const Element* e: elements) {
        all.append(QString::fromStdString(e->getData()));
    }
    return QJsonDocument(all).toJson(QJsonDocument::Compact).toStdString();
}

std::vector<std::string> fromClipboard(const std::string& text, size_t count) {
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text));
    const QJsonArray all = doc.array();
    if (!doc.isArray() || static_cast<size_t>(all.size()) != count) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(count);
    for (const QJsonValue& v: all) {
        if (!v.isString()) {
            return {};
        }
        out.push_back(v.toString().toStdString());
    }
    return out;
}

}  // namespace xqt::elementdata
