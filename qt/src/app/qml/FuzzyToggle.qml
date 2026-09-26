// The fuzzy search's toggle in a search field (library, tab overview, the search bar of a document): the search
// reads fzf's syntax - names fuzzy and ranked, words ANDed, | for or, ! for not, parentheses. An app-wide setting
// (app.library.fuzzySearch), off by default. A long press or a right click opens its help (FuzzyHelp.qml); the
// tooltip says so. The document's search bar shows the mode of its search instead (`fuzzy`, `setFuzzy`).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ToolButton {
    id: toggle
    text: qsTr("Fuzzy")
    checkable: true
    /// What it shows, and what a tap sets (default: the setting)
    property bool fuzzy: app.library.fuzzySearch
    property var setFuzzy: function(on) { app.library.fuzzySearch = on }
    checked: fuzzy
    onToggled: setFuzzy(checked)
    implicitHeight: 36
    leftPadding: 6
    rightPadding: 6
    font.pixelSize: 13
    font.weight: checked ? Font.DemiBold : Font.Normal
    Material.foreground: checked ? Material.accentColor : "#5f6368"
    ToolTip.visible: (hovered || pressed) && !help.visible
    ToolTip.delay: 600
    ToolTip.timeout: 6000
    ToolTip.text: (checked ? qsTr("Fuzzy search is on (like fzf) - tap for the plain search.")
                           : qsTr("Fuzzy search (like fzf): tap to turn it on."))
                  + " " + qsTr("Long press or right-click for help.")
    // The help (FuzzyHelp.qml): a long press (then the button is not toggled) or a right click
    onPressAndHold: help.open()
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: help.open()
    }
    FuzzyHelp {
        id: help
        objectName: toggle.objectName + "Help"
    }
    background: Rectangle {
        radius: 10
        color: toggle.checked ? "#e0e3f5" : (toggle.pressed ? "#e8e8e8" : "transparent")
    }
}
