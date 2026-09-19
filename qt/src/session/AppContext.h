/*
 * xournal-qt: process-wide application state shared by all document sessions (tabs).
 *
 * Upstream's GTK `Control` owns these once per window; here they are shared across tabs:
 * settings (same settings.xml format as upstream, in ~/.config/xournal-qt), the tool state (the same pen in every
 * tab), page templates and the render worker pool.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>

#include <QObject>

#include "control/ToolHandler.h"  // for ToolListener
#include "gui/GladeSearchpath.h"

#include "filesystem.h"

class PageTypeHandler;
class Settings;

namespace xqt {

class RenderService;

class AppContext: public QObject, public ToolListener {
    Q_OBJECT
public:
    /**
     * @param resourceDir directory with the runtime resources (pagetemplates.ini, palettes/)
     * @param settingsFile settings.xml to use; empty: the default in the user's config folder
     */
    explicit AppContext(fs::path resourceDir, fs::path settingsFile = {}, int renderThreads = 0);
    ~AppContext() override;

    /// XQT_RESOURCE_DIR, else the build tree's resources (development), else <prefix>/share/xournal-qt.
    static fs::path defaultResourceDir();
    /// Route Util::execInUiThread (used by the reused core) to the Qt event loop of the calling (GUI) thread.
    static void installQtUiThreadDispatcher();

    Settings* getSettings() const { return settings.get(); }
    ToolHandler* getToolHandler() const { return toolHandler.get(); }
    /// Make the tools of the pen and mouse buttons from the settings (port of Control::initButtonTool). Call it
    /// after changing a button's tool.
    void initButtonTools();
    PageTypeHandler* getPageTypes() const { return pageTypes.get(); }
    RenderService* getRenderService() const { return renderService.get(); }
    const fs::path& getResourceDir() const { return resourceDir; }

    // ToolListener (from the shared ToolHandler)
    void toolColorChanged() override;
    void changeColorOfSelection() override;
    void toolSizeChanged() override;
    void toolFillChanged() override;
    void toolLineStyleChanged() override;
    void toolChanged() override;

Q_SIGNALS:
    void activeToolChanged();
    void toolPropertiesChanged();
    /// The color of the active tool changed while a selection exists: the active session should recolor it.
    void selectionColorChangeRequested();
    /// Settings were changed from the settings screen (sessions re-read what they cache, e.g. autosave).
    void settingsChanged();

private:
    fs::path resourceDir;
    GladeSearchpath searchPath;
    std::unique_ptr<Settings> settings;
    std::unique_ptr<PageTypeHandler> pageTypes;
    std::unique_ptr<ToolHandler> toolHandler;
    std::unique_ptr<RenderService> renderService;
};

}  // namespace xqt
