/*
 * xournal-qt: one plugin's JavaScript: its QJSEngine, the API module "xournal" and the bridge behind it
 * (qt/docs/decisions/0008-js-plugins.md).
 *
 * The engine is a plain QJSEngine: no extensions but a console that writes into the plugin's log, no file, network,
 * process or timer API. The API is made by a bootstrap script (api.js) as frozen objects whose functions call one
 * native object, the bridge; the bridge is captured by the bootstrap and never reachable from the plugin (every slot
 * of an exposed QObject is callable, deleteLater too). The bridge turns each call into an operation of the operations
 * layer (or a dialog of the window) for the command being run.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QJSEngine>
#include <QJSValue>
#include <QObject>
#include <QString>

#include "PluginHost.h"

namespace xqt::plugins {

/// What the bridge acts for during a call into the plugin
struct ActiveCall {
    const PluginInfo* info = nullptr;
    ops::Context* context = nullptr;
    Environment env;
    QString title;
    bool writes = true;  ///< false in a live dialog's change(): a preview changes nothing
};

class PluginBridge final: public QObject {
    Q_OBJECT
public:
    PluginBridge(PluginHost& host, QString pluginId);
    /// An API call: an operation's name and its arguments (a JS object). Throws a JS error when it fails.
    Q_INVOKABLE QJSValue call(const QString& op, const QJSValue& args);
    /// console.log & co: into the plugin's log (0 info, 1 warning, 2 error)
    Q_INVOKABLE void log(int level, const QString& text);

    ActiveCall* active = nullptr;

private:
    QJSValue fail(const QString& name, const QString& message);
    PluginHost& host;
    QString plugin;
};

class PluginScript {
public:
    PluginScript(PluginHost& host, const PluginInfo& info);
    ~PluginScript();
    /// Makes the engine, the API, and imports the main module (its imports checked first). False with `error`.
    bool load(QString& error);
    QJSEngine& engine() { return js; }
    PluginBridge& bridge() { return *bridgeObject; }
    /// An exported function of the main module (undefined when there is none)
    QJSValue exported(const QString& name) const { return module.property(name); }

private:
    PluginHost& host;
    PluginInfo info;
    QJSEngine js;
    PluginBridge* bridgeObject;
    QJSValue module;
};

}  // namespace xqt::plugins
