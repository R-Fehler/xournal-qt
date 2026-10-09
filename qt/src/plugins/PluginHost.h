/*
 * xournal-qt: the plugins of the process (qt/docs/features/plugins.md, ADR 0008).
 *
 * The host finds the plugins (every folder with a plugin.json in the bundled plugins' folder and the user's), keeps
 * which are enabled and what the user allowed each (the settings), and runs their commands: one QJSEngine per plugin,
 * made on its first command, on the UI thread. A command runs for the window that asked (an Environment: its table of
 * operations, its document, its UI) as one transaction of the operations layer: one undo step, or rolled back when the
 * command throws, is refused a permission, or runs longer than the watchdog's 2 s.
 *
 * Headless: QtQml, QtCore and the session; the window is the Environment's PluginUi (the CLI and the tests have their
 * own).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <QDateTime>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include "Operations.h"
#include "PluginManifest.h"

namespace xqt {
class DocumentSession;
}

namespace xqt::plugins {

class PluginHost;
class PluginScript;
class Watchdog;

struct PluginInfo {
    PluginManifest manifest;  ///< (id: the folder's name when the manifest is broken)
    QString folder;
    bool bundled = false;
    bool enabled = false;
    QString error;  ///< why it cannot run ("" fine): a broken manifest, a newer API, a module that may not load
};

/// One line of a plugin's log
struct LogLine {
    QDateTime time;
    int level = 0;  ///< 0 info, 1 warning, 2 error
    QString text;
};

class LiveDialog;

/// What a window offers a plugin: questions, dialogs, notes. Methods that wait for the user run a nested event loop
/// (the watchdog does not count that time).
class PluginUi {
public:
    virtual ~PluginUi() = default;
    /// May `plugin` do what `opClass` allows (described as `what`)? Waits for the answer.
    virtual bool askPermission(const PluginInfo& plugin, const QString& opClass, const QString& what) = 0;
    /// A modal dialog described by `spec` (title, fields, …): the values, or nullopt when cancelled. Waits.
    virtual std::optional<QVariantMap> dialog(const PluginInfo& plugin, const QVariantMap& spec) = 0;
    /// Opens a live dialog (non-modal, with a preview on the page); false when the window cannot
    virtual bool openLive(const std::shared_ptr<LiveDialog>& dialog) = 0;
    /// A short note; `undoable`: with Undo (a command changed the document)
    virtual void notify(const QString& text, bool undoable) = 0;
    /// The colors a plugin offers (the palette chosen now): [{key, name, color}]
    virtual QVariantList palette() const;
};

/// The window a command runs for
struct Environment {
    ops::Operations* operations = nullptr;
    DocumentSession* document = nullptr;  ///< the current document (nullptr: none)
    void* window = nullptr;               ///< the window's own (for its operations)
    PluginUi* ui = nullptr;
};

/// A live dialog of a plugin while it is open: the window calls change() on every edit (and when the preview's frame
/// moves) and insert() on Insert; close() when it goes. Its callbacks run in the plugin's engine like a command.
class LiveDialog {
public:
    LiveDialog(PluginHost& host, QString pluginId, QString title, QVariantMap spec, QJSValue change, QJSValue insert,
               Environment env);
    ~LiveDialog();
    const QString& pluginId() const { return plugin; }
    const QString& title() const { return commandTitle; }
    /// The dialog as data: {title, fields, values, insertLabel, frame?, …}
    const QVariantMap& spec() const { return description; }
    DocumentSession* document() const { return env.document; }
    /// The values or the frame changed (`action`: a button's id, else ""): {shapes, errors, fields, values, frame,
    /// message}. On a failure: {error}.
    QVariantMap change(const QVariantMap& values, const QVariantMap& frame, const QString& action);
    /// Insert: the plugin's insert callback as one transaction (one undo step). True when it went through.
    bool insert(const QVariantMap& values, const QVariantMap& frame, QString* error = nullptr);
    bool isOpen() const { return open; }
    void close();

private:
    PluginHost& host;
    QString plugin;
    QString commandTitle;
    QVariantMap description;
    QJSValue onChange;
    QJSValue onInsert;
    Environment env;
    QPointer<DocumentSession> session;
    bool open = true;
};

class PluginHost final: public QObject {
    Q_OBJECT
public:
    using Load = std::function<QString()>;
    using Store = std::function<void(const QString&)>;
    /// `folders`: where plugins are (the bundled first; a user's plugin of the same id replaces a bundled one).
    /// `load`/`store`: the settings (JSON: which are enabled, what each was allowed).
    PluginHost(QStringList folders, Load load, Store store, QObject* parent = nullptr);
    ~PluginHost() override;

    /// Reads the folders again; every engine goes (made anew on the next command); open live dialogs close
    void reload();
    const std::vector<PluginInfo>& plugins() const { return list; }
    const PluginInfo* plugin(const QString& id) const;
    QStringList folders() const { return roots; }

    bool setEnabled(const QString& id, bool on);
    /// The user's answer for a permission class: "allow", "deny" or "" (not asked yet)
    QString grant(const QString& id, const QString& opClass) const;
    void setGrant(const QString& id, const QString& opClass, const QString& answer);

    const std::deque<LogLine>& log(const QString& id) const;
    void addLog(const QString& id, int level, const QString& text);
    void clearLog(const QString& id);

    struct Result {
        bool ok = false;
        bool changed = false;  ///< pushed an undo step
        QString error;         ///< for the user ("Function plotter: TypeError: … (main.mjs:12)")
    };
    /// Runs a command for a window (one transaction)
    Result run(const QString& id, const QString& commandId, const Environment& env);
    /// A plugin's command runs now (no other may start: the window's undo waits too)
    bool busy() const { return running > 0; }
    /// How long JavaScript may run in one call (default 2 s)
    void setTimeLimit(std::chrono::milliseconds limit) { timeLimit = limit; }

Q_SIGNALS:
    void pluginsChanged();
    void logChanged(const QString& id);
    /// A live dialog of a plugin must close (the plugin was reloaded or switched off)
    void liveDialogsClosed(const QString& id);

private:
    friend class LiveDialog;
    friend class PluginBridge;
    friend class PluginScript;
    struct Call;
    PluginScript* scriptOf(const QString& id, QString& error);
    /// Calls `fn` with `args` in the plugin's engine for `env`: the watchdog armed, a context set for the bridge,
    /// as one transaction titled `title` when `transaction`. Errors become Result::error (and the log).
    Result call(const PluginInfo& info, QJSValue fn, const QJSValueList& args, const Environment& env,
                const QString& title, bool transaction, bool writes, QJSValue* returned = nullptr);
    QString describeError(const PluginInfo& info, const QJSValue& error) const;
    void loadSettings();
    void storeSettings();
    void scan();

    QStringList roots;
    Load loadFn;
    Store storeFn;
    std::vector<PluginInfo> list;
    std::map<QString, bool> enabledSetting;
    std::map<QString, std::map<QString, QString>> grants;
    std::map<QString, std::deque<LogLine>> logs;
    std::map<QString, std::unique_ptr<PluginScript>> scripts;
    std::unique_ptr<Watchdog> watchdog;
    std::chrono::milliseconds timeLimit{2000};
    int running = 0;
};

}  // namespace xqt::plugins
