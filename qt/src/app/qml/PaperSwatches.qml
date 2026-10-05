// The paper of pages (qt/docs/dark-pages.md, "Page colors"): the curated colors (white, illustration paper, kraft, a
// soft green and blue, grey, dark grey, black) as round swatches, and textured paper. Used where pages get their
// background: the background dialog, a new document, inserting pages, Settings → new pages.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: swatches
    /// The paper's color (a color none of the swatches has, set in Xournal++, is shown as one more swatch)
    property color paper: "#ffffff"
    property bool textured: false
    signal chosen(color paper)
    signal texturedToggled(bool on)
    spacing: 2

    readonly property var list: app.paperSwatches()
    readonly property bool other: !list.some(function(s) { return Qt.colorEqual(s.color, swatches.paper) })

    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: swatches.list.concat(swatches.other ? [{ id: "other", name: qsTr("This page's color"), color: swatches.paper }] : [])
            delegate: AbstractButton {
                id: swatch
                required property var modelData
                objectName: "paper_" + modelData.id
                implicitWidth: 40
                implicitHeight: 40
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: modelData.name
                ToolTip.delay: 400
                Accessible.name: modelData.name
                readonly property bool current: Qt.colorEqual(swatches.paper, modelData.color)
                onClicked: swatches.chosen(modelData.color)
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 30; height: 30; radius: 15
                        color: swatch.modelData.color
                        border.width: swatch.current ? 3 : 1
                        border.color: swatch.current ? Material.accentColor : "#9e9e9e"
                        // (the grain, hinted: a few specks)
                        Repeater {
                            model: swatches.textured ? 5 : 0
                            delegate: Rectangle {
                                required property int index
                                width: 2; height: 2; radius: 1
                                x: 8 + (index * 7) % 15
                                y: 9 + (index * 11) % 13
                                color: Qt.colorEqual(swatch.modelData.color, "#ffffff") || swatch.modelData.dark === false
                                       ? "#40000000" : "#40ffffff"
                            }
                        }
                    }
                }
            }
        }
    }
    Switch {
        objectName: "paperTexture"
        text: qsTr("Textured paper")
        checked: swatches.textured
        onToggled: swatches.texturedToggled(checked)
    }
}
