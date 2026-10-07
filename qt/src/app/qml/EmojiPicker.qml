// The emoji picker: a search (names, tags, descriptions: "heart", "happy", "flag") and the emoji by category. A tap
// (or Enter: the first found) picks one (`picked`); Escape or a tap outside closes it. In the phone classes it is a
// bottom sheet (the window's, above the soft keyboard while it is open); elsewhere it opens beside `owner`, at
// `ownerX`, `ownerY` in its coordinates.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import XournalQt.Canvas

Popup {
    id: picker
    // (open: Android's back key is its; Zen's Back waits, qt/top-bar)
    onOpenedChanged: if (typeof win !== "undefined" && win && win.takeBack !== undefined) win.takeBack(opened)
    objectName: "emojiPicker"
    signal picked(string emoji)
    /// The item it opens beside (not a sheet), and where in its coordinates
    property Item owner: null
    property real ownerX: 0
    property real ownerY: 0
    /// A bottom sheet in the phone classes (Main.qml's sheet geometry)
    readonly property bool asSheet: typeof win !== "undefined" && win !== null && win.layout.phoneLayout === true
    parent: asSheet ? Overlay.overlay : owner
    modal: asSheet
    dim: asSheet
    x: asSheet ? win.insets.sheetX : ownerX
    y: asSheet ? win.insets.sheetBottom - height : ownerY
    width: asSheet ? win.insets.sheetWidth : 360
    height: asSheet ? Math.min(420, Math.round((win.insets.sheetBottom - win.insets.top) * 0.85)) : 400
    // (kept inside the window, and less high in a phone's landscape: F13.4)
    margins: asSheet ? 0 : 8
    padding: 8
    bottomPadding: asSheet ? 8 + win.insets.sheetBottomPadding : 8
    focus: true
    closePolicy: asSheet ? Popup.CloseOnEscape | Popup.CloseOnPressOutside
                         : Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        color: "#ffffff"
        radius: picker.asSheet ? 16 : 12
        border.width: picker.asSheet ? 0 : 1
        border.color: "#d5d8dc"
        Rectangle {  // (a sheet: square at the bottom, where it rests on the window's edge or the keyboard)
            visible: picker.asSheet
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
    }
    // Android's back key closes it (Qt closes a popup on it only while the popup has the keys)
    Shortcut {
        sequence: "Back"
        enabled: picker.opened
        onActivated: picker.close()
    }

    property var results: Emoji.search("")
    function refresh() { results = Emoji.search(search.text) }
    // (on a phone the search does not take the keys at once: that would open the soft keyboard over the emoji)
    onOpened: { search.text = ""; refresh(); if (!asSheet) search.forceActiveFocus() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        TextField {
            id: search
            objectName: "emojiSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Search emoji (smile, heart, flag, ...)")
            onTextChanged: picker.refresh()
            Keys.onReturnPressed: if (picker.results.length > 0) picker.picked(picker.results[0].emoji)
            Keys.onEnterPressed: if (picker.results.length > 0) picker.picked(picker.results[0].emoji)
        }
        // Categories: a tap scrolls to the first of each (without a search)
        Flow {
            Layout.fillWidth: true
            visible: search.text === ""
            spacing: 2
            Repeater {
                model: ["😀", "👋", "🐶", "🍎", "🚗", "⚽", "💡", "❤️", "🏁"]
                delegate: ToolButton {
                    required property string modelData
                    required property int index
                    text: modelData
                    font.family: "Xournal Qt Emoji"
                    font.pixelSize: 16
                    implicitWidth: 36
                    implicitHeight: 32
                    focusPolicy: Qt.NoFocus
                    ToolTip.visible: hovered
                    ToolTip.text: Emoji.categories()[index]
                    ToolTip.delay: 600
                    onClicked: {
                        for (let i = 0; i < picker.results.length; ++i) {
                            if (picker.results[i].category === index) { grid.positionViewAtIndex(i, GridView.Beginning); break }
                        }
                    }
                }
            }
        }
        GridView {
            id: grid
            objectName: "emojiGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 42
            cellHeight: 42
            model: picker.results
            ScrollBar.vertical: ScrollBar {}
            delegate: ToolButton {
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                text: modelData.emoji
                font.family: "Xournal Qt Emoji"
                font.pixelSize: 24
                focusPolicy: Qt.NoFocus
                ToolTip.visible: hovered
                ToolTip.text: ":" + modelData.name + ":"
                ToolTip.delay: 400
                onClicked: picker.picked(modelData.emoji)
            }
        }
        Label {
            visible: picker.results.length === 0
            text: qsTr("No emoji found")
            color: "#5f6368"
        }
    }
}
