#include "AppContext.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include "control/ToolHandler.h"
#include "control/pagetype/PageTypeHandler.h"
#include "control/settings/ButtonConfig.h"
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
    // The side buttons of the pen erase (upstream leaves them unused). Once: the settings screen can change it.
    if (bool set = false; !settings->getCustomElement("xournalQt").getBool("stylusButtonsSet", set) || !set) {
        for (const Button b: {Button::BUTTON_STYLUS_ONE, Button::BUTTON_STYLUS_TWO}) {
            if (settings->getButtonConfig(static_cast<unsigned int>(b))->getAction() == TOOL_NONE) {
                settings->getButtonConfig(static_cast<unsigned int>(b))->setAction(TOOL_ERASER);
            }
        }
        settings->getCustomElement("xournalQt").setBool("stylusButtonsSet", true);
        settings->customSettingsChanged();
    }
    initButtonTools();
    renderService = std::make_unique<RenderService>(renderThreads);
}

void AppContext::initButtonTools() {
    // Without this the buttons of the pen and mouse have no tool at all and do nothing.
    for (const Button b: {Button::BUTTON_ERASER, Button::BUTTON_STYLUS_ONE, Button::BUTTON_STYLUS_TWO,
                          Button::BUTTON_MOUSE_LEFT, Button::BUTTON_MOUSE_MIDDLE, Button::BUTTON_MOUSE_RIGHT,
                          Button::BUTTON_MOUSE_4, Button::BUTTON_MOUSE_5, Button::BUTTON_TOUCH}) {
        settings->getButtonConfig(static_cast<unsigned int>(b))->initButton(toolHandler.get(), b);
    }
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
    // Installed: <prefix>/bin/xournal-qt and <prefix>/share/xournal-qt; else the build tree.
    const auto appDir = fs::path(QCoreApplication::applicationDirPath().toStdString());
    const fs::path installed = appDir.parent_path() / "share" / "xournal-qt";
    if (std::error_code ec; fs::exists(installed / "pagetemplates.ini", ec)) {
        return installed;
    }
#ifdef XQT_BUILD_RESOURCE_DIR
    if (fs::exists(fs::path(XQT_BUILD_RESOURCE_DIR) / "pagetemplates.ini")) {
        return fs::path(XQT_BUILD_RESOURCE_DIR);
    }
#endif
    return installed;
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
