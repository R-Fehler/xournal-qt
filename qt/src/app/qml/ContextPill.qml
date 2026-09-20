// What can be done here: appears where a finger was held down or the right mouse button was pressed, and stays
// inside the window. Paste puts the clipboard at that very place; with something selected it also copies, cuts or
// deletes it. On selected PDF text only copying and marking make sense, so the rest is not offered.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: pill
    objectName: "contextPill"
    parent: Overlay.overlay
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// Where it was asked for, in canvas coordinates
    property point at: Qt.point(0, 0)
    property bool onPdfText: false
    /// What the clipboard holds cannot be watched, so it is looked at when the pill opens
    property bool pasteAvailable: false

    function openAt(viewPos, pdfText) {
        at = viewPos
        onPdfText = pdfText === true
        pasteAvailable = app.canPaste()
        const x0 = canvas.x + viewPos.x - implicitWidth / 2
        const y0 = canvas.y + viewPos.y + 18
        x = Math.max(8, Math.min(x0, win.width - implicitWidth - 8))
        y = y0 + implicitHeight + 8 > win.height ? Math.max(8, canvas.y + viewPos.y - implicitHeight - 18) : y0
        open()
    }

    background: Rectangle {
        radius: 22
        color: "#ffffff"
        border.width: 1
        border.color: "#d5d8dc"
    }

    RowLayout {
        spacing: 0
        ToolButton {
            objectName: "contextPaste"
            text: qsTr("Paste")
            visible: !pill.onPdfText && pill.pasteAvailable
            onClicked: { app.pasteAt(pill.at.x, pill.at.y); pill.close() }
        }
        ToolButton {
            objectName: "contextCopy"
            text: qsTr("Copy")
            visible: app.hasSelection || pill.onPdfText
            onClicked: { pill.onPdfText ? app.copyPdfText() : app.copySelection(); pill.close() }
        }
        ToolButton {
            objectName: "contextCut"
            text: qsTr("Cut")
            visible: app.hasSelection && !pill.onPdfText
            onClicked: { app.cutSelection(); pill.close() }
        }
        ToolButton {
            objectName: "contextDelete"
            text: qsTr("Delete")
            visible: app.hasSelection && !pill.onPdfText
            onClicked: { app.deleteSelection(); pill.close() }
        }
        ToolButton {
            objectName: "contextSelectAll"
            text: qsTr("Select all")
            visible: !pill.onPdfText && !app.hasSelection
            onClicked: { app.selectAllOnPage(); pill.close() }
        }
        ToolSeparator { visible: !pill.onPdfText }
        ToolButton {
            objectName: "contextImage"
            text: qsTr("Image…")
            visible: !pill.onPdfText
            onClicked: { imageDialog.open(); pill.close() }
        }
    }
}
