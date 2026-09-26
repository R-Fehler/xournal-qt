// The emoji picker: a search (names, tags, descriptions: "heart", "happy", "flag") and the emoji by category. A tap
// (or Enter: the first found) picks one (`picked`); Escape or a tap outside closes it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import XournalQt.Canvas

Popup {
    id: picker
    objectName: "emojiPicker"
    signal picked(string emoji)
    width: 360
    height: 400
    padding: 8
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle { color: "#ffffff"; radius: 12; border.width: 1; border.color: "#d5d8dc" }

    property var results: Emoji.search("")
    function refresh() { results = Emoji.search(search.text) }
    onOpened: { search.text = ""; refresh(); search.forceActiveFocus() }

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
