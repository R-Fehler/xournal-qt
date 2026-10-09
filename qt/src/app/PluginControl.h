/*
 * xournal-qt: plugins in a window (qt/docs/features/plugins.md), the QML object `app.plugins`.
 *
 * The process's PluginHost (AppServices) runs a plugin's command for the window that asked; this is that window's
 * side: its commands for the menus, the toolbox and the keys; the window's operations (the selection, the tool in
 * hand, the view, file dialogs) beside the document's; and the plugin's UI (PluginUi): the permission question and
 * the modal dialogs (QML answers them while a nested event loop waits), the notes (the snackbar, with Undo after a
 * command that changed the document), and the live dialog with its preview on the page (CanvasView::setPluginPreview)
 * in a frame the user moves and sizes.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <optional>

#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "Operations.h"
#include "PluginHost.h"
#include "WindowContext.h"

class AppController;

namespace xqt {

class PluginControl final: public QObject, public plugins::PluginUi {
    Q_OBJECT
    /// The commands of the enabled plugins: [{key ("plugin:<id>/<command>"), plugin, pluginName, command, title,
    /// icon, menu, toolbox, place, when, shortcut}] (key is also the shortcut's and the toolbox item's name)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed)
    /// Every plugin found, for Settings → Plugins: [{id, name, version, author, description, enabled, bundled, error,
    /// folder, permissions: [{class, text, answer}], commands: [title, …]}]
    Q_PROPERTY(QVariantList plugins READ pluginList NOTIFY changed)
    /// Where the user's plugins go
    Q_PROPERTY(QString userFolder READ userFolder CONSTANT)
    /// Counts the lines written into the plugins' logs (bindings that show a log read it)
    Q_PROPERTY(int logRevision READ logRevision NOTIFY logChanged)
    /// A command runs (the window's undo waits)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// --- the questions and dialogs the window shows (while a command waits for them) ---
    Q_PROPERTY(QVariantMap question READ question NOTIFY questionChanged)
    Q_PROPERTY(QVariantMap dialog READ dialogSpec NOTIFY dialogChanged)
    /// --- the live dialog (one at a time) ---
    Q_PROPERTY(bool liveOpen READ liveOpen NOTIFY liveChanged)
    Q_PROPERTY(QVariantMap liveSpec READ liveSpec NOTIFY liveChanged)
    /// What the plugin answered last: {fields, errors, values, message, error}
    Q_PROPERTY(QVariantMap liveState READ liveState NOTIFY liveStateChanged)
    /// The preview's frame on the page: {page, x, y, width, height, resizable} (page coordinates)
    Q_PROPERTY(QVariantMap liveFrame READ liveFrame NOTIFY liveFrameChanged)

public:
    PluginControl(const WindowContext& window, AppController& controller, QObject* parent = nullptr);
    ~PluginControl() override;

    QVariantList commands() const;
    QVariantList pluginList() const;
    QString userFolder() const;
    bool busy() const;
    int logRevision() const { return logRev; }
    QVariantMap question() const { return asking; }
    QVariantMap dialogSpec() const { return dialogShown; }
    bool liveOpen() const { return live != nullptr; }
    QVariantMap liveSpec() const;
    QVariantMap liveState() const { return state; }
    QVariantMap liveFrame() const { return frame; }

    /// Runs a command by its key ("plugin:<id>/<command>")
    Q_INVOKABLE bool run(const QString& key);
    Q_INVOKABLE void setEnabled(const QString& id, bool on);
    /// "allow", "deny" or "" (asked again)
    Q_INVOKABLE void setGrant(const QString& id, const QString& opClass, const QString& answer);
    Q_INVOKABLE QString logText(const QString& id) const;
    Q_INVOKABLE void clearLog(const QString& id);
    Q_INVOKABLE void reload();
    /// The icon of a command (a file of the plugin, or an app icon): a URL for an Image
    Q_INVOKABLE QUrl iconOf(const QString& key) const;

    // --- the window's answers ---
    Q_INVOKABLE void answerQuestion(bool allow);
    /// `values` (a map), or nothing: cancelled
    Q_INVOKABLE void answerDialog(const QVariant& values);
    Q_INVOKABLE void answerFile(const QUrl& file);
    /// The live dialog: a field changed (or a button was pressed: `action`), the frame moved, Insert, Cancel
    Q_INVOKABLE void liveEdited(const QVariantMap& values, const QString& action = QString());
    Q_INVOKABLE void moveLiveFrame(double x, double y, double width, double height);
    Q_INVOKABLE bool liveInsert(const QVariantMap& values);
    Q_INVOKABLE void liveCancel();

    // --- PluginUi ---
    bool askPermission(const plugins::PluginInfo& plugin, const QString& opClass, const QString& what) override;
    std::optional<QVariantMap> dialog(const plugins::PluginInfo& plugin, const QVariantMap& spec) override;
    bool openLive(const std::shared_ptr<plugins::LiveDialog>& dialog) override;
    void notify(const QString& text, bool undoable) override;
    QVariantList palette() const override;

    /// The document changed under the live dialog (another tab): it closes
    void currentChanged();

Q_SIGNALS:
    void changed();
    void busyChanged();
    void logChanged();
    void questionChanged();
    void dialogChanged();
    void liveChanged();
    void liveStateChanged();
    void liveFrameChanged();
    /// A note for the snackbar (`undoable`: with Undo)
    void note(const QString& text, bool undoable);
    /// A file dialog: open or save, its title, a proposed name, filters ("PDF (*.pdf)")
    void fileRequested(bool save, const QString& title, const QString& name, const QStringList& filters);

private:
    plugins::PluginHost& host() const;
    plugins::Environment environment();
    void addWindowOperations();
    /// Waits in a nested loop until `answered` (the watchdog was paused by the host)
    void wait(bool& answered);
    void applyLiveResult(const QVariantMap& result);
    void showPreview(const QVariantList& shapes);
    void clearPreview();
    void closeLive();
    QVariantMap defaultFrame(const QVariantMap& wanted) const;

    WindowContext window;
    AppController& app;
    ops::Operations operations;
    // the question and the dialog shown now (empty: none)
    QVariantMap asking;
    bool askAnswered = false;
    bool askAllowed = false;
    QVariantMap dialogShown;
    bool dialogAnswered = false;
    std::optional<QVariantMap> dialogValues;
    bool fileAnswered = false;
    QUrl fileChosen;
    std::map<QString, QUrl> fileHandles;
    QEventLoop* loop = nullptr;
    // the live dialog
    std::shared_ptr<plugins::LiveDialog> live;
    QVariantMap state;
    QVariantMap frame;
    QVariantMap lastValues;
    int logRev = 0;
};

}  // namespace xqt
