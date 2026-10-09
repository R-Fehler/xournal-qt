#include "PluginScript.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSysInfo>
#include <QUrl>

#include "ImportCheck.h"
#include "Watchdog.h"

namespace xqt::plugins {

namespace {
QString platformName() {
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_IOS)
    return QStringLiteral("ios");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

/// Operations the bridge serves itself (dialogs need the plugin's functions; they are no document change)
bool isUiCall(const QString& op) { return op.startsWith(QLatin1String("ui.")); }
}  // namespace

// --- the bridge ---------------------------------------------------------------------------------------------------

PluginBridge::PluginBridge(PluginHost& host, QString pluginId): host(host), plugin(std::move(pluginId)) {}

QJSValue PluginBridge::fail(const QString& name, const QString& message) {
    QJSEngine* engine = qjsEngine(this);
    QJSValue error = engine->newErrorObject(QJSValue::GenericError, message);
    error.setProperty(QStringLiteral("name"), name);
    engine->throwError(error);
    return {};
}

void PluginBridge::log(int level, const QString& text) { host.addLog(plugin, level, text); }

QJSValue PluginBridge::call(const QString& op, const QJSValue& args) {
    QJSEngine* engine = qjsEngine(this);
    if (!active || !active->context) {
        return fail(QStringLiteral("Error"),
                    QStringLiteral("%1: the API is there while a command or a dialog of the plugin runs").arg(op));
    }
    ActiveCall& a = *active;
    PluginUi* ui = a.env.ui;
    if (isUiCall(op)) {
        if (op == QLatin1String("ui.notify")) {
            if (ui) {
                ui->notify(QStringLiteral("%1: %2").arg(a.info->manifest.name, args.property("text").toString()), false);
            }
            return QJSValue(true);
        }
        if (op == QLatin1String("ui.palette")) {
            return engine->toScriptValue(ui ? ui->palette() : QVariantList());
        }
        if (op == QLatin1String("ui.dialog")) {
            if (!ui) {
                return QJSValue(QJSValue::NullValue);
            }
            host.watchdog->pause();
            const auto values = ui->dialog(*a.info, args.toVariant().toMap());
            host.watchdog->resume();
            return values ? engine->toScriptValue(*values) : QJSValue(QJSValue::NullValue);
        }
        if (op == QLatin1String("ui.form")) {
            const QJSValue change = args.property(QStringLiteral("change"));
            const QJSValue insert = args.property(QStringLiteral("insert"));
            if (!change.isCallable() || !insert.isCallable()) {
                return fail(QStringLiteral("TypeError"), QStringLiteral("ui.form needs the functions change and insert"));
            }
            QVariantMap spec = args.toVariant().toMap();
            spec.remove(QStringLiteral("change"));
            spec.remove(QStringLiteral("insert"));
            if (!ui) {
                return QJSValue(false);
            }
            auto dialog = std::make_shared<LiveDialog>(host, plugin, a.title, spec, change, insert, a.env);
            return QJSValue(ui->openLive(dialog));
        }
        return fail(QStringLiteral("ReferenceError"), QStringLiteral("no operation \"%1\"").arg(op));
    }
    try {
        if (!a.writes) {
            const ops::Operations::Info* info = a.context->operations.find(op);
            if (info && info->writes) {
                return fail(QStringLiteral("Error"),
                            QStringLiteral("%1: a preview cannot change the document (do it in insert)").arg(op));
            }
        }
        const QVariant result = a.context->apply(op, args.isUndefined() || args.isNull() ? QVariantMap()
                                                                                         : args.toVariant().toMap());
        return engine->toScriptValue(result);
    } catch (const ops::Error& e) {
        return fail(e.name(), e.text);
    } catch (const std::exception& e) {
        return fail(QStringLiteral("Error"), QStringLiteral("%1: %2").arg(op, QString::fromUtf8(e.what())));
    }
}

// --- the engine ---------------------------------------------------------------------------------------------------

PluginScript::PluginScript(PluginHost& host, const PluginInfo& info):
        host(host), info(info), bridgeObject(new PluginBridge(host, info.manifest.id)) {
    QJSEngine::setObjectOwnership(bridgeObject, QJSEngine::CppOwnership);
}

PluginScript::~PluginScript() {
    module = QJSValue();
    delete bridgeObject;
}

bool PluginScript::load(QString& error) {
    error = checkImports(info.folder, info.manifest.main);
    if (!error.isEmpty()) {
        return false;
    }
    QFile bootstrap(QStringLiteral(":/xqt-plugins/api.js"));
    if (!bootstrap.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("the plugin API is missing from the app");
        return false;
    }
    const QJSValue make = js.evaluate(QString::fromUtf8(bootstrap.readAll()), QStringLiteral("xournal:api.js"));
    if (make.isError() || !make.isCallable()) {
        error = QStringLiteral("the plugin API does not load: %1").arg(make.toString());
        return false;
    }
    const QLocale locale;
    QJSValue facts = js.newObject();
    facts.setProperty(QStringLiteral("apiVersion"), QStringLiteral("%1.%2").arg(API_MAJOR).arg(API_MINOR));
    facts.setProperty(QStringLiteral("platform"), platformName());
    facts.setProperty(QStringLiteral("locale"), locale.name());
    facts.setProperty(QStringLiteral("decimalPoint"), locale.decimalPoint());
    QJSValue self = js.newObject();
    self.setProperty(QStringLiteral("id"), info.manifest.id);
    self.setProperty(QStringLiteral("name"), info.manifest.name);
    self.setProperty(QStringLiteral("version"), info.manifest.version);
    facts.setProperty(QStringLiteral("plugin"), self);
    const QJSValue api = make.call({js.newQObject(bridgeObject), facts});
    if (api.isError()) {
        error = QStringLiteral("the plugin API does not load: %1").arg(api.toString());
        return false;
    }
    js.globalObject().setProperty(QStringLiteral("console"), api.property(QStringLiteral("console")));
    js.registerModule(QStringLiteral("xournal"), api);
    // (the main module runs here: its top level may not use the API yet, no command runs)
    module = js.importModule(QDir(info.folder).filePath(info.manifest.main));
    if (module.isError()) {
        error = host.describeError(info, module);
        module = QJSValue();
        return false;
    }
    return true;
}

}  // namespace xqt::plugins
