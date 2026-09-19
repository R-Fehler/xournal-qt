#include "AppContext.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include "control/pagetype/PageTypeHandler.h"
#include "control/settings/Settings.h"
#include "render/RenderService.h"
#include "util/PathUtil.h"
#include "util/Util.h"

#include "config-dev.h"  // for SETTINGS_XML_FILE

namespace xqt {

AppContext::AppContext(fs::path resourceDir, fs::path settingsFile, int renderThreads):
        resourceDir(std::move(resourceDir)) {
    searchPath.addSearchDirectory(this->resourceDir);
    if (settingsFile.empty()) {
        settingsFile = Util::getConfigFile(SETTINGS_XML_FILE);
    }
    settings = std::make_unique<Settings>(std::move(settingsFile));
    settings->load();
    pageTypes = std::make_unique<PageTypeHandler>(&searchPath);
    // No GAction database in the Qt build: tool state changes are reported through ToolListener.
    toolHandler = std::make_unique<ToolHandler>(this, nullptr, settings.get());
    toolHandler->loadSettings();
    renderService = std::make_unique<RenderService>(renderThreads);
}

AppContext::~AppContext() {
    renderService.reset();  // joins the workers before the rest goes away
    if (toolHandler) {
        toolHandler->saveSettings();
    }
}

fs::path AppContext::defaultResourceDir() {
    if (auto env = qEnvironmentVariable("XQT_RESOURCE_DIR"); !env.isEmpty()) {
        return fs::path(env.toStdString());
    }
#ifdef XQT_BUILD_RESOURCE_DIR
    if (fs::exists(fs::path(XQT_BUILD_RESOURCE_DIR) / "pagetemplates.ini")) {
        return fs::path(XQT_BUILD_RESOURCE_DIR);
    }
#endif
    const auto appDir = fs::path(QCoreApplication::applicationDirPath().toStdString());
    return appDir.parent_path() / "share" / "xournal-qt";
}

void AppContext::installQtUiThreadDispatcher() {
    Util::setUiThreadDispatcher(+[](xoj::util::move_only_function<void()> callback, gint) {
        auto* app = QCoreApplication::instance();
        if (!app) {
            return;  // no event loop any more (shutdown)
        }
        // QMetaObject::invokeMethod needs a copyable functor.
        auto shared = std::make_shared<xoj::util::move_only_function<void()>>(std::move(callback));
        QMetaObject::invokeMethod(app, [shared]() { (*shared)(); }, Qt::QueuedConnection);
    });
}

void AppContext::toolColorChanged() { Q_EMIT toolPropertiesChanged(); }
void AppContext::changeColorOfSelection() { Q_EMIT selectionColorChangeRequested(); }
void AppContext::toolSizeChanged() { Q_EMIT toolPropertiesChanged(); }
void AppContext::toolFillChanged() { Q_EMIT toolPropertiesChanged(); }
void AppContext::toolLineStyleChanged() { Q_EMIT toolPropertiesChanged(); }
void AppContext::toolChanged() { Q_EMIT activeToolChanged(); }

}  // namespace xqt
