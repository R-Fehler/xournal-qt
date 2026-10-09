#include "PluginManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "Operations.h"

namespace xqt::plugins {

const PluginCommand* PluginManifest::command(const QString& commandId) const {
    for (const PluginCommand& c: commands) {
        if (c.id == commandId) {
            return &c;
        }
    }
    return nullptr;
}

bool apiSupported(const QString& api) {
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\.(\\d+)$"));
    const auto m = re.match(api);
    return m.hasMatch() && m.captured(1).toInt() == API_MAJOR && m.captured(2).toInt() <= API_MINOR;
}

std::optional<PluginManifest> PluginManifest::parse(const QByteArray& json, QString& error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        const int line = static_cast<int>(json.left(parseError.offset).count('\n')) + 1;
        error = QStringLiteral("line %1: %2").arg(line).arg(parseError.errorString());
        return std::nullopt;
    }
    if (!doc.isObject()) {
        error = QStringLiteral("not a JSON object");
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    PluginManifest m;
    m.id = o.value("id").toString();
    m.name = o.value("name").toString();
    m.version = o.value("version").toString();
    m.api = o.value("api").toString();
    m.author = o.value("author").toString();
    m.description = o.value("description").toString();
    m.main = o.value("main").toString(m.main);
    m.enabledByDefault = o.value("enabled").toBool(false);
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,99}$"));
    static const QRegularExpression commandRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,63}$"));
    static const QRegularExpression mainRe(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_./-]*\\.m?js$"));
    if (!idRe.match(m.id).hasMatch()) {
        error = QStringLiteral("\"id\" must be lower-case letters, digits, '.', '-' or '_' (like org.example.name)");
        return std::nullopt;
    }
    if (m.name.isEmpty()) {
        error = QStringLiteral("\"name\" is missing");
        return std::nullopt;
    }
    if (m.api.isEmpty()) {
        error = QStringLiteral("\"api\" is missing (\"1.0\")");
        return std::nullopt;
    }
    if (!mainRe.match(m.main).hasMatch() || m.main.contains(QLatin1String(".."))) {
        error = QStringLiteral("\"main\" must be a .mjs or .js file in the plugin's folder");
        return std::nullopt;
    }
    const QStringList known = ops::grantableClasses();
    for (const QJsonValue& v: o.value("permissions").toArray()) {
        const QString p = v.toString();
        if (!known.contains(p)) {
            error = QStringLiteral("unknown permission \"%1\" (%2)").arg(p, known.join(", "));
            return std::nullopt;
        }
        if (!m.permissions.contains(p)) {
            m.permissions << p;
        }
    }
    for (const QJsonValue& v: o.value("commands").toArray()) {
        const QJsonObject c = v.toObject();
        PluginCommand cmd;
        cmd.id = c.value("id").toString();
        cmd.title = c.value("title").toString();
        cmd.shortcut = c.value("shortcut").toString();
        cmd.icon = c.value("icon").toString();
        cmd.menu = c.value("menu").toBool(true);
        cmd.toolbox = c.value("toolbox").toBool(true);
        cmd.place = c.value("place").toString(cmd.place);
        cmd.when = c.value("when").toString(cmd.when);
        if (!commandRe.match(cmd.id).hasMatch()) {
            error = QStringLiteral("a command's \"id\" must be the name of an exported function (\"%1\")").arg(cmd.id);
            return std::nullopt;
        }
        if (m.command(cmd.id)) {
            error = QStringLiteral("the command \"%1\" is there twice").arg(cmd.id);
            return std::nullopt;
        }
        if (cmd.title.isEmpty()) {
            error = QStringLiteral("the command \"%1\" has no \"title\"").arg(cmd.id);
            return std::nullopt;
        }
        static const QRegularExpression keysRe(QStringLiteral("^((Ctrl|Shift|Alt|Meta)\\+)*[^+\\s]+$"));
        if (!cmd.shortcut.isEmpty() && !keysRe.match(cmd.shortcut).hasMatch()) {
            error = QStringLiteral("the command \"%1\" has no valid \"shortcut\" (\"%2\")").arg(cmd.id, cmd.shortcut);
            return std::nullopt;
        }
        if (cmd.when != QLatin1String("always") && cmd.when != QLatin1String("document") &&
            cmd.when != QLatin1String("selection")) {
            error = QStringLiteral("the command \"%1\": \"when\" is always, document or selection").arg(cmd.id);
            return std::nullopt;
        }
        if (cmd.place != QLatin1String("plugins") && cmd.place != QLatin1String("insert")) {
            error = QStringLiteral("the command \"%1\": \"place\" is plugins or insert").arg(cmd.id);
            return std::nullopt;
        }
        if (cmd.icon.contains(QLatin1String("..")) || cmd.icon.startsWith('/')) {
            error = QStringLiteral("the command \"%1\": \"icon\" must be in the plugin's folder").arg(cmd.id);
            return std::nullopt;
        }
        m.commands.push_back(cmd);
    }
    return m;
}

}  // namespace xqt::plugins
