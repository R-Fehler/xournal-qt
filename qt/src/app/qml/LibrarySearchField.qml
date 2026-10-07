// xournal-qt: the search in the whole library and the extended search: in the header, or in a row of its own.
// Part of HomeView.qml (the home screen, qt/docs/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

RowLayout {
    id: searchGroup
    // (what the other parts use)
    readonly property alias searchField: searchField
    readonly property alias searchTyping: searchTyping
    parent: home.searchOwnRow ? narrowSearchSlot : homeHeader.searchSlot
    anchors.fill: parent
    spacing: 6
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        Layout.minimumWidth: 0
        radius: 22
        color: "#ffffff"
        border.width: searchField.activeFocus ? 2 : 1
        border.color: searchField.activeFocus ? Material.accentColor : "#c9ccd1"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 2
            spacing: 2
            ToolButton {
                objectName: "librarySearchButton"
                implicitWidth: 40; implicitHeight: 40
                icon.source: app.iconUrl("xqt-search")
                icon.color: "#3c4043"
                display: AbstractButton.IconOnly
                onClicked: { searchTyping.stop(); home.lib.searchQuery = searchField.text }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Search (Enter)")
                ToolTip.delay: 600
            }
            TextField {
                id: searchField
                objectName: "librarySearchField"
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                background: null
                selectByMouse: true
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.leftPadding
                    width: parent.width - parent.leftPadding - parent.rightPadding  // (a narrow field: …)
                    elide: Text.ElideRight
                    visible: parent.text === "" && parent.preeditText === ""
                    text: parent.width < 170 ? qsTr("Search") : qsTr("Search documents and folders")
                    color: "#8a8d91"
                }
                onTextEdited: home.typed()
                Keys.onReturnPressed: { searchTyping.stop(); home.lib.searchQuery = text; libraryPage.libraryGrid.forceActiveFocus() }
                Keys.onEnterPressed: { searchTyping.stop(); home.lib.searchQuery = text; libraryPage.libraryGrid.forceActiveFocus() }
                Keys.onDownPressed: libraryPage.libraryGrid.forceActiveFocus()
                Keys.onEscapePressed: { text = ""; home.lib.searchQuery = ""; libraryPage.libraryGrid.forceActiveFocus() }
            }
            Label {
                objectName: "librarySearchHint"
                visible: searchField.text !== "" && searchField.text.length < 4 && searchField.text !== home.lib.searchQuery
                text: qsTr("Enter ↵")
                color: "#6b6f75"
                font.pixelSize: 12
            }
            // Fuzzy search: an expression that is not valid is searched as plain text, and says why
            Label {
                objectName: "librarySyntaxHint"
                visible: home.lib.searchHint !== ""
                Layout.maximumWidth: 150
                text: home.lib.searchHint
                elide: Text.ElideRight
                color: "#b3261e"
                font.pixelSize: 12
                ToolTip.visible: hintHover.hovered
                ToolTip.text: qsTr("%1 - searched as plain text").arg(home.lib.searchHint)
                ToolTip.delay: 300
                HoverHandler { id: hintHover }
            }
            FuzzyToggle { objectName: "librarySearchFuzzy"; implicitHeight: home.minTarget }
            // The reduced search: names only (of documents, and of folders unless the list is flat)
            ToolButton {
                id: namesOnly
                objectName: "searchNamesOnly"
                text: qsTr("Names")
                checkable: true
                checked: home.lib.namesOnly
                onToggled: home.lib.namesOnly = checked
                implicitHeight: home.minTarget
                leftPadding: 6
                rightPadding: 6
                font.pixelSize: 13
                font.weight: checked ? Font.DemiBold : Font.Normal
                Material.foreground: checked ? Material.accentColor : "#5f6368"
                ToolTip.visible: hovered
                ToolTip.text: checked ? qsTr("Searching names only - tap to search the text of the documents too")
                                      : qsTr("Search names only: of documents, and of folders when they are shown")
                ToolTip.delay: 600
                background: Rectangle {
                    radius: 10
                    color: namesOnly.checked ? "#e0e3f5" : (namesOnly.pressed ? "#e8e8e8" : "transparent")
                }
            }
            ToolButton {
                objectName: "librarySearchClear"
                visible: searchField.text !== ""
                implicitWidth: 40; implicitHeight: 40
                icon.source: app.iconUrl("xqt-close")
                icon.color: "#3c4043"
                display: AbstractButton.IconOnly
                Accessible.name: qsTr("Clear the search")
                onClicked: { searchField.text = ""; searchTyping.stop(); home.lib.searchQuery = "" }
            }
        }
        Timer {
            id: searchTyping
            interval: 300
            onTriggered: home.lib.searchQuery = searchField.text
        }
    }
    IconButton {
        objectName: "extendedSearchButton"
        label: qsTr("Extended search")
        iconName: "xqt-pages-grid"
        tip: qsTr("Extended search: show the pages with hits of every result")
        checked: home.extended
        onClicked: home.extended = !home.extended
    }
}
