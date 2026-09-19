/*
 * xournal-qt: application controller exposed to QML.
 *
 * Owns the shared AppContext and the tabs (TabManager). The document properties (title, undo state, zoom, pages,
 * view, ...) always refer to the current tab. The tool state is upstream's ToolHandler, shared by all tabs.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <QColor>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include "filesystem.h"

namespace xqt {
class AppContext;
class CanvasView;
class DocumentSession;
class TabManager;
class PagesModel;
class SettingsModel;
class SessionRecovery;
}  // namespace xqt
class Palette;

class AppController: public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* tabs READ tabsModel CONSTANT)
    /// Pages of the current tab (for the page sidebar).
    Q_PROPERTY(QObject* pages READ pagesModel CONSTANT)
    Q_PROPERTY(QObject* settings READ settingsModel CONSTANT)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY documentChanged)
    Q_PROPERTY(QObject* view READ view NOTIFY documentChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool hasFilePath READ hasFilePath NOTIFY titleChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color NOTIFY toolChanged)
    Q_PROPERTY(int size READ size NOTIFY toolChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY zoomChanged)
    Q_PROPERTY(int pageNumber READ pageNumber NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    /// Documents of a crashed previous run that can be recovered: [{ title, time }]. Empty when there are none.
    Q_PROPERTY(QVariantList recoveryItems READ recoveryItems NOTIFY recoveryChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QObject* tabsModel() const;
    QObject* pagesModel() const;
    QObject* settingsModel() const;
    int currentTab() const;
    void setCurrentTab(int index);
    QObject* view() const;
    QString title() const;
    bool modified() const;
    bool hasFilePath() const;
    bool canUndo() const;
    bool canRedo() const;
    QString tool() const;
    QColor color() const;
    int size() const;
    QVariantList palette() const;
    int zoomPercent() const;
    int pageNumber() const;
    int pageCount() const;
    QVariantList recoveryItems() const;

    // --- start and recovery ---
    /// Start of the app: offers recovery after a crash (recoveryItems), else reopens the last tabs (setting), then
    /// opens `files`. Starts protecting the open documents (journal, emergency saves).
    void startSession(const QStringList& files);
    /// Answer to the recovery offer: reopen the tabs of the crashed run, with the recovered changes (accept) or as
    /// last saved (discard; the recovery files are deleted).
    Q_INVOKABLE void recover(bool accept);

    // --- tabs ---
    /// New empty document in a new tab.
    Q_INVOKABLE void newDocument();
    /// Open a file in a tab: switches to it if it is already open, replaces an untouched new document, else adds a
    /// tab. Returns false (and reports the error) if it cannot be loaded.
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool openPath(const QString& path);
    /// Open several files (e.g. from the command line or another instance).
    Q_INVOKABLE void openPaths(const QStringList& paths);
    Q_INVOKABLE void openUrls(const QList<QUrl>& urls);
    /// Close a tab without asking (QML asks about unsaved changes first). The last tab is replaced by a new one.
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void moveTab(int from, int to);
    Q_INVOKABLE void nextTab();
    Q_INVOKABLE void previousTab();
    Q_INVOKABLE int tabCount() const;
    Q_INVOKABLE bool tabModified(int index) const;
    Q_INVOKABLE QString tabTitle(int index) const;
    /// Indices of tabs with unsaved changes.
    Q_INVOKABLE QVariantList modifiedTabs() const;

    // --- current document ---
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void fitWidth();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void addPageAfterCurrent();

    // --- pages of the current document (index: 0-based page) ---
    Q_INVOKABLE void goToPage(int index);
    Q_INVOKABLE void insertPageBefore(int index);
    Q_INVOKABLE void insertPageAfter(int index);
    Q_INVOKABLE void duplicatePage(int index);
    Q_INVOKABLE void deletePage(int index);
    Q_INVOKABLE void movePageUp(int index);
    Q_INVOKABLE void movePageDown(int index);

    // --- tools (shared by all tabs) ---
    /// "pen", "highlighter", "eraser", "hand"
    Q_INVOKABLE void selectTool(const QString& tool);
    Q_INVOKABLE void setColor(const QColor& color);
    /// 0 = very fine ... 4 = very thick (upstream ToolSize)
    Q_INVOKABLE void setSize(int size);

    // --- misc ---
    Q_INVOKABLE QUrl iconUrl(const QString& name) const;
    /// Folder for the Open dialog: the current document's folder, else the last folder a file was opened from.
    Q_INVOKABLE QUrl openFolder() const;
    /// Suggestion for "Save as" (upstream Control::saveImpl): for an annotated PDF the .xopp next to the PDF.
    Q_INVOKABLE QUrl suggestedSaveFile() const;
    /// Call before quitting: writes settings.
    Q_INVOKABLE void shutdown();

    xqt::AppContext& context() const { return *app; }
    xqt::TabManager& tabManager() const { return *tabs; }

Q_SIGNALS:
    void documentChanged();
    void titleChanged();
    void modifiedChanged();
    void undoRedoChanged();
    void toolChanged();
    void zoomChanged();
    void pageChanged();
    /// Messages from the core (XojMsgBox) and file errors, shown by QML.
    void message(const QString& title, const QString& text, bool error);
    /// The window should come to the front (e.g. another instance handed over files).
    void raiseRequested();
    void recoveryChanged();

private:
    xqt::DocumentSession* session() const;
    xqt::CanvasView* canvas() const;
    void currentTabChanged();
    /// Reopens the tabs of a journal; `recovered`: tab index -> recovery file to load instead of the file.
    void reopenTabs(const std::vector<std::pair<fs::path, int>>& tabs, int current,
                    const std::map<size_t, std::pair<fs::path, fs::path>>& recovered);

    std::unique_ptr<xqt::AppContext> app;
    std::unique_ptr<Palette> colors;
    std::unique_ptr<xqt::TabManager> tabs;
    std::unique_ptr<xqt::PagesModel> pages;
    std::unique_ptr<xqt::SettingsModel> settingsView;
    std::unique_ptr<xqt::SessionRecovery> recovery;  // after `tabs`: destroyed first
    bool recoveryPending = false;
    std::vector<QMetaObject::Connection> currentConnections;
};
