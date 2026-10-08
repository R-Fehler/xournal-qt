#include "ModelInfo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace xqt::hwr {

ModelInfo ModelInfo::read(const QString& folder) {
    ModelInfo m;
    m.folder = folder;
    QFile f(QDir(folder).filePath(QStringLiteral("model.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        m.error = QStringLiteral("No model in %1").arg(folder);
        return m;
    }
    const QByteArray bytes = f.readAll();
    QJsonParseError parsed{};
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parsed);
    if (parsed.error != QJsonParseError::NoError || !json.isObject()) {
        m.error = QStringLiteral("The model's manifest in %1 cannot be read").arg(folder);
        return m;
    }
    const QJsonObject o = json.object();
    m.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("trocr"));
    m.name = o.value(QStringLiteral("name")).toString();
    for (const QJsonValue& v: o.value(QStringLiteral("languages")).toArray()) {
        if (const QString l = v.toString(); !l.isEmpty()) {
            m.languages << l;
        }
    }
    if (m.languages.isEmpty() && m.kind == QLatin1String("trocr")) {
        m.languages << QStringLiteral("en");  // (the first manifests: TrOCR-small handwritten, English)
    }
    m.version = o.value(QStringLiteral("version")).toString(o.value(QStringLiteral("revision")).toString());
    m.licence = o.value(QStringLiteral("licence")).toString(o.value(QStringLiteral("license")).toString());
    m.noncommercial = o.value(QStringLiteral("noncommercial")).toBool(false);
    m.hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).left(12);
    if (m.kind != QLatin1String("trocr") && m.kind != QLatin1String("ctc")) {
        m.error = QStringLiteral("The model in %1 is of a kind this app does not read (%2)").arg(folder, m.kind);
    } else if (m.name.isEmpty()) {
        m.error = QStringLiteral("The model's manifest in %1 has no name").arg(folder);
    } else if (m.languages.isEmpty()) {
        m.error = QStringLiteral("The model's manifest in %1 names no language").arg(folder);
    }
    return m;
}

}  // namespace xqt::hwr
