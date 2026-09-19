// The three preset colors for PDF text highlights (the current one is ringed).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Row {
    id: row
    /// A color was chosen (it is the highlight color now).
    signal picked()
    spacing: 2
    Repeater {
        model: app.pdfHighlightColors
        delegate: AbstractButton {
            id: button
            required property color modelData
            objectName: "highlightColor"
            implicitWidth: 34
            implicitHeight: 40
            onClicked: {
                app.pdfHighlightColor = modelData
                row.picked()
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Highlight color")
            ToolTip.delay: 600
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 24; height: 24; radius: 5
                    color: button.modelData
                    border.width: Qt.colorEqual(app.pdfHighlightColor, button.modelData) ? 3 : 1
                    border.color: Qt.colorEqual(app.pdfHighlightColor, button.modelData) ? Material.accentColor : "#9e9e9e"
                }
            }
        }
    }
}
