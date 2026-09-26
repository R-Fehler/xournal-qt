// The emoji suggested for a shortcode being typed (":smi" -> 😄 smile, 😃 smiley, ...): a list below the cursor
// (above it when there is no room). Up / Down / Enter are handled by the editor; a tap chooses. It never takes the
// focus, so typing goes on.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Rectangle {
    id: list
    /// {emoji, name} each
    property var model: []
    property int current: 0
    /// The cursor, in the parent's coordinates: the list goes below it
    property rect cursor: Qt.rect(0, 0, 0, 0)
    signal chosen(int index)

    readonly property int rowHeight: 34
    visible: model.length > 0
    z: 100
    width: 240
    height: model.length * rowHeight + 8
    x: Math.max(0, Math.min(cursor.x, parent.width - width))
    y: cursor.y + cursor.height + 4 + height <= parent.height ? cursor.y + cursor.height + 4
                                                              : Math.max(0, cursor.y - height - 4)
    radius: 8
    color: "#ffffff"
    border.color: "#d5d8dc"
    border.width: 1

    Column {
        anchors.fill: parent
        anchors.margins: 4
        Repeater {
            model: list.model
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "emojiSuggestion" + index
                width: list.width - 8
                height: list.rowHeight
                radius: 6
                color: index === list.current ? "#e0e3f5" : (tap.hovered ? "#f1f3f4" : "transparent")
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 10
                    Text {
                        text: row.modelData.emoji
                        font.family: "Xournal Qt Emoji"  // (the app's, EmojiFont.h; elsewhere the system's)
                        font.pixelSize: 20
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: ":" + row.modelData.name + ":"
                        color: "#303030"
                        elide: Text.ElideRight
                        width: list.width - 70
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                HoverHandler { id: tap }
                TapHandler { onTapped: list.chosen(row.index) }
            }
        }
    }
}
