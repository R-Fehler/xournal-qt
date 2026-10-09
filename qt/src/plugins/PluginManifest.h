/*
 * xournal-qt: a plugin's manifest, plugin.json (qt/docs/features/plugins.md, "The manifest").
 *
 *   {"id": "org.example.color-cycle", "name": "Color cycle", "version": "1.0.0", "api": "1.0",
 *    "author": "…", "description": "…", "main": "main.mjs", "enabled": false,
 *    "permissions": ["tools"],
 *    "commands": [{"id": "cycle", "title": "Cycle the pen's color", "shortcut": "Alt+C", "icon": "cycle.svg",
 *                  "menu": true, "toolbox": true, "place": "plugins", "when": "document"}]}
 *
 * A command's id is the name of the function the main module exports. `place`: where it is offered besides Plugins
 * ("insert": with the commands that put things on the page); `when`: "always", "document" (a document is open),
 * "selection" (elements are selected). `enabled`: whether it is on before the user switched it.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

namespace xqt::plugins {

/// The API this app serves (a plugin needs the same major and a minor not newer)
constexpr int API_MAJOR = 1;
constexpr int API_MINOR = 0;

struct PluginCommand {
    QString id;
    QString title;
    QString shortcut;  ///< the default keys (Qt's portable text); "" none
    QString icon;      ///< a file in the plugin's folder, or an icon name of the app ("xqt-…"); "" none
    bool menu = true;
    bool toolbox = true;
    QString place = QStringLiteral("plugins");
    QString when = QStringLiteral("document");
};

struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString api;
    QString author;
    QString description;
    QString main = QStringLiteral("main.mjs");
    QStringList permissions;  ///< permission classes (ops: edit, pages, tools, files)
    bool enabledByDefault = false;
    std::vector<PluginCommand> commands;

    const PluginCommand* command(const QString& commandId) const;

    /// Reads plugin.json's bytes; nullopt with `error` ("line 3: …") when it is not a valid manifest
    static std::optional<PluginManifest> parse(const QByteArray& json, QString& error);
};

/// Whether an `api` ("1.0") can run here
bool apiSupported(const QString& api);

}  // namespace xqt::plugins
