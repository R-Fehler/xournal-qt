#include "PluginHost.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "session/DocumentSession.h"

#include "PluginScript.h"
#include "Watchdog.h"

namespace xqt::plugins {

namespace {
constexpr size_t LOG_LINES = 500;  ///< per plugin (the oldest go)

/// A plugin's permissions: the classes its manifest names, each allowed or denied by the user's answer, asked on
/// first use (the answer kept in the settings)
class PluginAuthority final: public ops::Authority {
public:
    PluginAuthority(PluginHost& host, const PluginInfo& info, PluginUi* ui, Watchdog& watchdog):
            host(host), info(info), ui(ui), watchdog(watchdog) {}
    ops::Decision decide(const ops::Principal&, const QString&, const QString& opClass) override {
        if (ops::isFreeClass(opClass)) {
            return ops::Decision::Allow;
        }
        if (!info.manifest.permissions.contains(opClass)) {
            return ops::Decision::Deny;
        }
        const QString answer = host.grant(info.manifest.id, opClass);
        if (answer == QLatin1String("allow")) {
            return ops::Decision::Allow;
        }
        return answer == QLatin1String("deny") ? ops::Decision::Deny : ops::Decision::Ask;
    }
    bool ask(const ops::Principal&, const QString&, const QString& opClass) override {
        if (!ui) {
            return false;
        }
        watchdog.pause();
        const bool yes = ui->askPermission(info, opClass, ops::describeClass(opClass));
        watchdog.resume();
        host.setGrant(info.manifest.id, opClass, yes ? QStringLiteral("allow") : QStringLiteral("deny"));
        return yes;
    }

private:
    PluginHost& host;
    const PluginInfo& info;
    PluginUi* ui;
    Watchdog& watchdog;
};
}  // namespace

QVariantList PluginUi::palette() const {
    // (headless: a palette of classic ink colors)
    return {QVariantMap{{"key", "black"}, {"name", "Black"}, {"color", "#000000"}},
            QVariantMap{{"key", "blue"}, {"name", "Blue"}, {"color", "#1a5fb4"}},
            QVariantMap{{"key", "red"}, {"name", "Red"}, {"color", "#c01c28"}},
            QVariantMap{{"key", "green"}, {"name", "Green"}, {"color", "#26a269"}},
            QVariantMap{{"key", "orange"}, {"name", "Orange"}, {"color", "#e66100"}},
            QVariantMap{{"key", "purple"}, {"name", "Purple"}, {"color", "#813d9c"}}};
}

// --- the host -----------------------------------------------------------------------------------------------------

PluginHost::PluginHost(QStringList folders, Load load, Store store, QObject* parent):
        QObject(parent),
        roots(std::move(folders)),
        loadFn(std::move(load)),
        storeFn(std::move(store)),
        watchdog(std::make_unique<Watchdog>()) {
    loadSettings();
    scan();
}

PluginHost::~PluginHost() { scripts.clear(); }

void PluginHost::loadSettings() {
    const QJsonObject o = QJsonDocument::fromJson(loadFn ? loadFn().toUtf8() : QByteArray()).object();
    const QJsonObject enabled = o.value("enabled").toObject();
    for (auto it = enabled.begin(); it != enabled.end(); ++it) {
        enabledSetting[it.key()] = it.value().toBool();
    }
    const QJsonObject g = o.value("grants").toObject();
    for (auto it = g.begin(); it != g.end(); ++it) {
        const QJsonObject classes = it.value().toObject();
        for (auto c = classes.begin(); c != classes.end(); ++c) {
            grants[it.key()][c.key()] = c.value().toString();
        }
    }
}

void PluginHost::storeSettings() {
    QJsonObject enabled;
    for (const auto& [id, on]: enabledSetting) {
        enabled.insert(id, on);
    }
    QJsonObject g;
    for (const auto& [id, classes]: grants) {
        QJsonObject c;
        for (const auto& [cls, answer]: classes) {
            if (!answer.isEmpty()) {
                c.insert(cls, answer);
            }
        }
        if (!c.isEmpty()) {
            g.insert(id, c);
        }
    }
    if (storeFn) {
        storeFn(QString::fromUtf8(QJsonDocument(QJsonObject{{"enabled", enabled}, {"grants", g}})
                                          .toJson(QJsonDocument::Compact)));
    }
}

void PluginHost::scan() {
    list.clear();
    for (int r = 0; r < roots.size(); ++r) {
        const QDir root(roots[r]);
        const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& f: folders) {
            const QString folder = root.filePath(f);
            QFile json(QDir(folder).filePath(QStringLiteral("plugin.json")));
            if (!json.exists()) {
                continue;
            }
            PluginInfo info;
            info.folder = QFileInfo(folder).absoluteFilePath();
            info.bundled = r == 0 && roots.size() > 1;
            QString error;
            std::optional<PluginManifest> m;
            if (!json.open(QIODevice::ReadOnly)) {
                error = QStringLiteral("plugin.json cannot be read");
            } else {
                m = PluginManifest::parse(json.read(256 * 1024), error);
            }
            if (m) {
                info.manifest = *m;
                if (!apiSupported(m->api)) {
                    info.error = QStringLiteral("needs the plugin API %1; this xournal-qt has %2.%3")
                                         .arg(m->api)
                                         .arg(API_MAJOR)
                                         .arg(API_MINOR);
                }
            } else {
                info.manifest.id = f;
                info.manifest.name = f;
                info.error = QStringLiteral("plugin.json: %1").arg(error);
            }
            const auto it = enabledSetting.find(info.manifest.id);
            info.enabled = it != enabledSetting.end() ? it->second : info.manifest.enabledByDefault;
            // (a user's plugin of the same id replaces a bundled one: development of a bundled plugin)
            auto same = std::find_if(list.begin(), list.end(),
                                     [&](const PluginInfo& p) { return p.manifest.id == info.manifest.id; });
            if (same != list.end()) {
                *same = info;
            } else {
                list.push_back(info);
            }
        }
    }
    std::sort(list.begin(), list.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return QString::localeAwareCompare(a.manifest.name, b.manifest.name) < 0;
    });
}

void PluginHost::reload() {
    for (const PluginInfo& p: list) {
        Q_EMIT liveDialogsClosed(p.manifest.id);
    }
    scripts.clear();
    scan();
    Q_EMIT pluginsChanged();
}

const PluginInfo* PluginHost::plugin(const QString& id) const {
    const auto it = std::find_if(list.begin(), list.end(), [&](const PluginInfo& p) { return p.manifest.id == id; });
    return it == list.end() ? nullptr : &*it;
}

bool PluginHost::setEnabled(const QString& id, bool on) {
    auto it = std::find_if(list.begin(), list.end(), [&](const PluginInfo& p) { return p.manifest.id == id; });
    if (it == list.end()) {
        return false;
    }
    enabledSetting[id] = on;
    it->enabled = on;
    if (!on) {
        Q_EMIT liveDialogsClosed(id);
        scripts.erase(id);  // (its engine goes)
    }
    storeSettings();
    Q_EMIT pluginsChanged();
    return true;
}

QString PluginHost::grant(const QString& id, const QString& opClass) const {
    const auto it = grants.find(id);
    if (it == grants.end()) {
        return {};
    }
    const auto c = it->second.find(opClass);
    return c == it->second.end() ? QString() : c->second;
}

void PluginHost::setGrant(const QString& id, const QString& opClass, const QString& answer) {
    if (answer.isEmpty()) {
        grants[id].erase(opClass);
    } else {
        grants[id][opClass] = answer;
    }
    storeSettings();
    Q_EMIT pluginsChanged();
}

const std::deque<LogLine>& PluginHost::log(const QString& id) const {
    static const std::deque<LogLine> none;
    const auto it = logs.find(id);
    return it == logs.end() ? none : it->second;
}

void PluginHost::addLog(const QString& id, int level, const QString& text) {
    auto& l = logs[id];
    l.push_back({QDateTime::currentDateTime(), level, text});
    while (l.size() > LOG_LINES) {
        l.pop_front();
    }
    Q_EMIT logChanged(id);
}

void PluginHost::clearLog(const QString& id) {
    logs.erase(id);
    Q_EMIT logChanged(id);
}

void PluginHost::pauseWatch() { watchdog->pause(); }
void PluginHost::resumeWatch() { watchdog->resume(); }

PluginScript* PluginHost::scriptOf(const QString& id, QString& error) {
    const PluginInfo* info = plugin(id);
    if (!info) {
        error = QStringLiteral("there is no plugin \"%1\"").arg(id);
        return nullptr;
    }
    if (!info->error.isEmpty()) {
        error = info->error;
        return nullptr;
    }
    if (!info->enabled) {
        error = QStringLiteral("%1 is switched off (Settings → Plugins)").arg(info->manifest.name);
        return nullptr;
    }
    if (auto it = scripts.find(id); it != scripts.end()) {
        return it->second.get();
    }
    auto script = std::make_unique<PluginScript>(*this, *info);
    watchdog->arm(&script->engine(), timeLimit);  // (the main module's top level runs here)
    const bool ok = script->load(error);
    if (watchdog->disarm()) {
        error = QStringLiteral("loading took longer than %1 s").arg(timeLimit.count() / 1000.0);
    }
    if (!ok) {
        addLog(id, 2, QStringLiteral("not loaded: %1").arg(error));
        return nullptr;
    }
    return scripts.emplace(id, std::move(script)).first->second.get();
}

QString PluginHost::describeError(const PluginInfo& info, const QJSValue& error) const {
    QString where;
    const QString folderUrl = QUrl::fromLocalFile(info.folder).toString() + '/';
    auto place = [&](const QString& file, int line) {
        if (file.startsWith(folderUrl) && line > 0) {
            where = QStringLiteral("%1:%2").arg(file.mid(folderUrl.size())).arg(line);
        }
    };
    place(error.property(QStringLiteral("fileName")).toString(), error.property(QStringLiteral("lineNumber")).toInt());
    if (where.isEmpty()) {
        // (an error thrown by the API: the first frame of the plugin's own files)
        for (const QString& frame: error.property(QStringLiteral("stack")).toString().split('\n')) {
            const int at = frame.indexOf(folderUrl);
            const int colon = frame.lastIndexOf(':');
            if (at >= 0 && colon > at) {
                place(frame.mid(at, colon - at), frame.mid(colon + 1).toInt());
                if (!where.isEmpty()) {
                    break;
                }
            }
        }
    }
    const QString text = error.toString();
    return where.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, where);
}

PluginHost::Result PluginHost::call(const PluginInfo& info, QJSValue fn, const QJSValueList& args,
                                    const Environment& env, const QString& title, bool transaction, bool writes,
                                    QJSValue* returned) {
    Result result;
    PluginScript* script = nullptr;
    {
        QString error;
        script = scriptOf(info.manifest.id, error);
        if (!script) {
            result.error = QStringLiteral("%1: %2").arg(info.manifest.name, error);
            return result;
        }
    }
    if (!env.operations) {
        result.error = QStringLiteral("%1: nothing to act on").arg(info.manifest.name);
        return result;
    }
    PluginAuthority authority(*this, info, env.ui, *watchdog);
    ops::Context context(*env.operations, ops::Principal::plugin(info.manifest.id, info.manifest.name), authority,
                         env.document);
    context.window = env.window;
    ActiveCall active{&info, &context, env, title, writes};
    PluginBridge& bridge = script->bridge();
    ActiveCall* outer = bridge.active;
    bridge.active = &active;
    ++running;
    QString failure;
    try {
        if (transaction && env.document) {
            context.begin(title);
        }
        watchdog->arm(&script->engine(), timeLimit);
        const QJSValue value = fn.call(args);
        const bool interrupted = watchdog->disarm();
        if (interrupted) {
            failure = QStringLiteral("%1 took longer than %2 s and was stopped")
                              .arg(info.manifest.name)
                              .arg(timeLimit.count() / 1000.0);
        } else if (value.isError()) {
            failure = QStringLiteral("%1: %2").arg(info.manifest.name, describeError(info, value));
        } else if (returned) {
            *returned = value;
        }
    } catch (const ops::Error& e) {
        failure = QStringLiteral("%1: %2").arg(info.manifest.name, e.text);
    }
    if (failure.isEmpty()) {
        result.ok = true;
        result.changed = context.commit();
    } else {
        const bool hadChanges = context.changed();
        context.rollback();
        result.error = hadChanges ? failure + QStringLiteral(" (its changes were undone)") : failure;
        addLog(info.manifest.id, 2, result.error);
    }
    --running;
    bridge.active = outer;
    return result;
}

PluginHost::Result PluginHost::run(const QString& id, const QString& commandId, const Environment& env) {
    const PluginInfo* info = plugin(id);
    const PluginCommand* command = info ? info->manifest.command(commandId) : nullptr;
    Result result;
    auto attempt = [&]() -> Result {
        Result r;
        if (!info || !command) {
            r.error = QStringLiteral("there is no command \"%1\" of \"%2\"").arg(commandId, id);
            return r;
        }
        if (running > 0) {
            r.error = QStringLiteral("%1: another plugin command is running").arg(info->manifest.name);
            return r;
        }
        QString error;
        PluginScript* script = scriptOf(id, error);
        if (!script) {
            r.error = QStringLiteral("%1: %2").arg(info->manifest.name, error);
            return r;
        }
        const QJSValue fn = script->exported(commandId);
        if (!fn.isCallable()) {
            r.error = QStringLiteral("%1: its main module exports no function \"%2\"").arg(info->manifest.name,
                                                                                          commandId);
            addLog(id, 2, r.error);
            return r;
        }
        QJSValue ctx = script->engine().newObject();
        ctx.setProperty(QStringLiteral("command"), commandId);
        const PluginInfo copy = *info;  // (a reload during the call must not pull it away)
        return call(copy, fn, {ctx}, env, command->title, true, true);
    };
    result = attempt();
    if (env.ui) {
        if (!result.ok) {
            env.ui->notify(result.error, false);
        } else if (result.changed) {
            env.ui->notify(command->title, true);  // ("Plot a function…" · Undo)
        }
    }
    return result;
}

// --- live dialogs -------------------------------------------------------------------------------------------------

LiveDialog::LiveDialog(PluginHost& host, QString pluginId, QString title, QVariantMap spec, QJSValue change,
                       QJSValue insert, Environment env):
        host(host),
        plugin(std::move(pluginId)),
        commandTitle(std::move(title)),
        description(std::move(spec)),
        onChange(std::move(change)),
        onInsert(std::move(insert)),
        env(env),
        session(env.document) {}

LiveDialog::~LiveDialog() = default;

void LiveDialog::close() {
    open = false;
    onChange = QJSValue();
    onInsert = QJSValue();
}

QVariantMap LiveDialog::change(const QVariantMap& values, const QVariantMap& frame, const QString& action) {
    const PluginInfo* info = host.plugin(plugin);
    if (!open || !info) {
        return {{"error", QStringLiteral("the plugin is gone")}};
    }
    PluginScript* script = host.scripts.count(plugin) ? host.scripts.at(plugin).get() : nullptr;
    if (!script) {
        return {{"error", QStringLiteral("the plugin was reloaded")}};
    }
    QJSEngine& js = script->engine();
    QJSValue ctx = js.newObject();
    ctx.setProperty(QStringLiteral("frame"), js.toScriptValue(frame));
    ctx.setProperty(QStringLiteral("action"), action);
    Environment e = env;
    e.document = session.data();
    QJSValue returned;
    const PluginInfo copy = *info;
    const auto r = host.call(copy, onChange, {js.toScriptValue(values), ctx}, e, commandTitle, false, false, &returned);
    if (!r.ok) {
        return {{"error", r.error}};
    }
    return returned.isObject() ? returned.toVariant().toMap() : QVariantMap();
}

bool LiveDialog::insert(const QVariantMap& values, const QVariantMap& frame, QString* error) {
    const PluginInfo* info = host.plugin(plugin);
    PluginScript* script = info && host.scripts.count(plugin) ? host.scripts.at(plugin).get() : nullptr;
    if (!open || !script || !session) {
        if (error) {
            *error = QStringLiteral("the plugin or its document is gone");
        }
        return false;
    }
    QJSEngine& js = script->engine();
    QJSValue ctx = js.newObject();
    ctx.setProperty(QStringLiteral("frame"), js.toScriptValue(frame));
    Environment e = env;
    e.document = session.data();
    const PluginInfo copy = *info;
    const auto r = host.call(copy, onInsert, {js.toScriptValue(values), ctx}, e, commandTitle, true, true);
    if (!r.ok && error) {
        *error = r.error;
    }
    if (r.ok && e.ui) {
        if (r.changed) {
            e.ui->notify(commandTitle, true);
        }
    }
    return r.ok;
}

}  // namespace xqt::plugins
