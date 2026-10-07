// xournal-qt: how the library is sorted: the Sort button's menu and View → Sort of the home screen
// (HomeHeader.qml).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

AdaptiveMenu {
    id: sortChoices
    property string prefix: "sort"
    AdaptiveMenuItem {
        objectName: sortChoices.prefix + "ByName"
        text: qsTr("By name")
        checkable: true
        checked: app.library.sortBy === "name"
        onTriggered: app.library.sortBy = "name"
    }
    AdaptiveMenuItem {
        objectName: sortChoices.prefix + "ByModified"
        text: qsTr("Last modified first")
        checkable: true
        checked: app.library.sortBy === "modified"
        onTriggered: app.library.sortBy = "modified"
    }
    AdaptiveMenuItem {
        objectName: sortChoices.prefix + "ByRead"
        text: qsTr("Last read first")
        checkable: true
        checked: app.library.sortBy === "read"
        onTriggered: app.library.sortBy = "read"
    }
}
