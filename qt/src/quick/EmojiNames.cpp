#include "EmojiNames.h"

#include <algorithm>
#include <string>

#include "EmojiData.h"
#include "Grapheme.h"

namespace xqt {

namespace {
QString firstAlias(const emoji::Emoji& e) {
    const std::string_view a(e.aliases);
    return QString::fromUtf8(a.data(), static_cast<qsizetype>(std::min(a.find(' '), a.size())));
}
}  // namespace

QString EmojiNames::typedShortcode(const QString& before) const {
    const std::string s = before.toStdString();
    const std::string_view name = emoji::typedShortcode(s);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QVariantList EmojiNames::completions(const QString& prefix, int limit) const {
    QVariantList out;
    for (const auto& c: emoji::complete(prefix.toStdString(), static_cast<size_t>(std::max(0, limit)))) {
        out.append(QVariantMap{{QStringLiteral("emoji"), QString::fromUtf8(c.emoji->emoji)},
                               {QStringLiteral("name"),
                                QString::fromUtf8(c.name.data(), static_cast<qsizetype>(c.name.size()))}});
    }
    return out;
}

QVariantList EmojiNames::search(const QString& query, int limit) const {
    QVariantList out;
    for (const emoji::Emoji* e: emoji::search(query.trimmed().toStdString(), static_cast<size_t>(std::max(0, limit)))) {
        out.append(QVariantMap{{QStringLiteral("emoji"), QString::fromUtf8(e->emoji)},
                               {QStringLiteral("name"), firstAlias(*e)},
                               {QStringLiteral("category"), static_cast<int>(e->category)}});
    }
    return out;
}

QStringList EmojiNames::categories() const {
    QStringList out;
    for (const char* c: emoji::categories()) {
        out << QString::fromUtf8(c);
    }
    return out;
}

int EmojiNames::graphemeStep(const QString& text, int pos, bool forward) const {
    pos = std::clamp(pos, 0, static_cast<int>(text.size()));
    const QByteArray utf8 = text.toUtf8();
    const size_t at = static_cast<size_t>(QStringView(text).left(pos).toUtf8().size());
    const size_t to = text::graphemeStep(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())), at, forward);
    return static_cast<int>(QString::fromUtf8(utf8.constData(), static_cast<qsizetype>(to)).size());
}

}  // namespace xqt
