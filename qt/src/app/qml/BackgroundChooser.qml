// Choice of a page background (upstream's page types that are patterns: plain, ruled, graph, dotted, ...), as small
// page drawings.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Flow {
    id: chooser
    /// Index in the settings' pageBackgrounds
    property int selected: 0
    property bool landscape: false
    signal chosen(int index)
    spacing: 8
    readonly property var s: app.settings
    Repeater {
        model: chooser.s.pageBackgrounds
        delegate: AbstractButton {
            id: bgButton
            required property int index
            required property string modelData
            readonly property string format: chooser.s.pageBackgroundFormats[index]
            objectName: "background_" + format
            // Only real patterns (not "copy the current page", PDF or image backgrounds).
            visible: ["plain", "ruled", "lined", "staves", "graph", "dotted", "isodotted", "isograph"].indexOf(format) >= 0
            width: 100
            height: 132
            onClicked: chooser.chosen(index)
            contentItem: ColumnLayout {
                spacing: 4
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: chooser.landscape ? 72 : 52
                    Layout.preferredHeight: chooser.landscape ? 52 : 72
                    color: "#ffffff"
                    border.width: chooser.selected === bgButton.index ? 3 : 1
                    border.color: chooser.selected === bgButton.index ? Material.accentColor : "#c9ccd1"
                    radius: 3
                    BackgroundPreview {
                        anchors.fill: parent
                        anchors.margins: parent.border.width
                        format: bgButton.format
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: bgButton.modelData
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: chooser.selected === bgButton.index ? Material.accentColor : "#3c4043"
                }
            }
            background: Rectangle {
                radius: 8
                color: bgButton.hovered ? "#f1f3f4" : "transparent"
            }
        }
    }
}
