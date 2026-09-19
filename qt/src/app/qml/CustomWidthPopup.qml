// The adjustable fifth width of the pen, highlighter or eraser: a slider (0.1 to 20 mm, finer at the thin end),
// − / + and a sample stroke in the current color. Opens beside its button.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: popup
    objectName: "customWidthPopup"
    /// Where the tool bar is: "top", "left", "right"
    property string side: "top"
    x: side === "right" ? -width - 4 : side === "left" ? parent.width + 4 : 0
    y: side === "top" ? parent.height + 4 : 0
    margins: 8  // inside the window
    padding: 14
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    readonly property real ptPerMm: 72 / 25.4
    readonly property real minMm: 0.1
    readonly property real maxMm: 20
    readonly property real mm: app.customWidth / ptPerMm
    readonly property string mmText: mm < 1 ? mm.toFixed(2) : mm.toFixed(1)
    // Logarithmic: fine steps for thin lines
    function toSlider(v) { return Math.log(v / minMm) / Math.log(maxMm / minMm) }
    function fromSlider(v) { return minMm * Math.pow(maxMm / minMm, v) }
    function setMm(v) { app.customWidth = Math.max(minMm, Math.min(maxMm, v)) * ptPerMm }

    ColumnLayout {
        spacing: 6
        Label {
            text: (app.tool === "highlighter" ? qsTr("Highlighter width")
                   : app.tool === "eraser" ? qsTr("Eraser width") : qsTr("Pen width")) + ": " + popup.mmText + " mm"
            font.bold: true
        }
        RowLayout {
            spacing: 0
            ToolButton {
                objectName: "customWidthLess"
                text: "−"
                font.pixelSize: 20
                autoRepeat: true
                onClicked: popup.setMm(popup.mm / 1.1)
            }
            Slider {
                objectName: "customWidthSlider"
                Layout.preferredWidth: 220
                from: 0; to: 1
                value: popup.toSlider(Math.max(popup.minMm, popup.mm))
                onMoved: popup.setMm(popup.fromSlider(value))
            }
            ToolButton {
                objectName: "customWidthMore"
                text: "+"
                font.pixelSize: 20
                autoRepeat: true
                onClicked: popup.setMm(popup.mm * 1.1)
            }
        }
        // Sample at the actual size (100 % zoom)
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(80, Math.max(12, sample.height + 8))
            Rectangle {
                id: sample
                anchors.centerIn: parent
                width: parent.width - 20
                height: Math.min(76, Math.max(1, popup.mm * Screen.pixelDensity))
                radius: height / 2
                color: app.tool === "eraser" ? "#bdbdbd" : app.color
                opacity: app.tool === "highlighter" ? 0.5 : 1
            }
        }
    }
}
