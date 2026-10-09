// xournal-qt: a plugin's command as an entry of a menu (qt/docs/features/plugins.md); `modelData` is one of
// app.plugins.commands. Offered when the command can run now (its `when`: a document is open, elements are selected).
import QtQuick
import QtQuick.Controls

AdaptiveMenuItem {
    required property var modelData
    objectName: "pluginCommand_" + modelData.key
    offered: modelData.when === "always" || (!app.homeVisible && (modelData.when !== "selection" || app.edit.hasSelection))
    enabled: !app.plugins.busy
    text: modelData.title + win.keyNote(modelData.key)
    icon.source: modelData.icon
    icon.color: "transparent"
    onTriggered: {
        const key = modelData.key
        toolboxMenus.afterMenus(function() { app.plugins.run(key) })
    }
}
