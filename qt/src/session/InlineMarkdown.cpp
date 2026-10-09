#include "InlineMarkdown.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "model/Text.h"

namespace xqt::md {

bool isInlineMarkdown(const Text& text) {
    const std::string& data = text.getData();
    if (data.find(INLINE_KEY) == std::string::npos) {
        return false;  // (most texts: no parse)
    }
    return QJsonDocument::fromJson(QByteArray::fromStdString(data)).object().value(INLINE_KEY).toBool();
}

std::string withInlineMark(const std::string& data) {
    QJsonObject all = QJsonDocument::fromJson(QByteArray::fromStdString(data)).object();
    all.insert(INLINE_KEY, true);
    return QJsonDocument(all).toJson(QJsonDocument::Compact).toStdString();
}

}  // namespace xqt::md
