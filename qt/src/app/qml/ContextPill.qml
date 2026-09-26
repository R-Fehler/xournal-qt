// What can be done here: appears where a finger was held down or the right mouse button was pressed, and stays
// inside the window. Paste puts the clipboard at that very place; with something selected it also copies, cuts or
// deletes it. On selected PDF text only copying and marking make sense, so the rest is not offered. One per canvas:
// the notes (target app), the reference beside them (target app.reference); on a canvas for reading only: copy,
// select all, go to a page, fit width - nothing that changes it. Selected text (PDF text or the text being written)
// can be looked up (qt/docs/citations.md).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

Popup {
    id: pill
    objectName: named("contextPill")
    /// The DocumentCanvas it is asked for on, and what acts on it
    property Item canvasItem: canvas
    property var target: app
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    /// "Image…": the window asks for the file (for the notes: its image dialog)
    signal imageRequested()
    /// "Go to page…" (a canvas for reading only)
    signal goToPageRequested()
    parent: Overlay.overlay
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// Where it was asked for, in canvas coordinates
    property point at: Qt.point(0, 0)
    property bool onPdfText: false
    /// Selected text (PDF text, or of the text being written) to look up (qt/docs/citations.md); read when it opens
    property string lookUpText: ""
    /// What the clipboard holds cannot be watched, so it is looked at when the pill opens
    property bool pasteAvailable: false
    readonly property bool reading: canvasItem.readingOnly
    /// Something to copy, cut or delete: selected elements, or a selected sticky note (the notes' canvas)
    readonly property bool selected: target.hasSelection || (target === app && app.noteSelected)

    function openAt(viewPos, pdfText) {
        at = viewPos
        onPdfText = pdfText === true
        pasteAvailable = target.canPaste()
        lookUpText = target.selectedText()
        const origin = canvasItem.mapToItem(win.contentItem, 0, 0)  // (the notes: their x, y in the window)
        const x0 = origin.x + viewPos.x - implicitWidth / 2
        const y0 = origin.y + viewPos.y + 18
        x = Math.max(8, Math.min(x0, win.width - implicitWidth - 8))
        y = y0 + implicitHeight + 8 > win.height ? Math.max(8, origin.y + viewPos.y - implicitHeight - 18) : y0
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
            objectName: pill.named("contextPaste")
            text: qsTr("Paste")
            visible: !pill.onPdfText && pill.pasteAvailable && !pill.reading
            onClicked: { pill.target.pasteAt(pill.at.x, pill.at.y); pill.close() }
        }
        ToolButton {
            objectName: pill.named("contextCopy")
            text: qsTr("Copy")
            visible: pill.selected || pill.onPdfText
            onClicked: { pill.onPdfText ? pill.target.copyPdfText() : pill.target.copySelection(); pill.close() }
        }
        ToolButton {
            objectName: pill.named("contextCut")
            text: qsTr("Cut")
            visible: pill.selected && !pill.onPdfText && !pill.reading
            onClicked: { pill.target.cutSelection(); pill.close() }
        }
        ToolButton {
            objectName: pill.named("contextDelete")
            text: qsTr("Delete")
            visible: pill.selected && !pill.onPdfText && !pill.reading
            onClicked: { pill.target.deleteSelection(); pill.close() }
        }
        ToolButton {
            id: lookUpButton
            objectName: pill.named("contextLookUp")
            text: qsTr("Look up…")
            visible: pill.lookUpText !== ""
            onClicked: {
                lookUpMenu.text = pill.lookUpText
                Popups.openAt(lookUpMenu)
            }
            LookUpMenu {
                id: lookUpMenu
                namePrefix: pill.namePrefix === "" ? "context" : pill.namePrefix + "Context"
                onClosed: pill.close()
            }
        }
        ToolButton {
            objectName: pill.named("contextSelectAll")
            text: qsTr("Select all")
            visible: !pill.onPdfText && !pill.target.hasSelection
            onClicked: { pill.target.selectAllOnPage(); pill.close() }
        }
        ToolSeparator { visible: !pill.onPdfText }
        ToolButton {
            objectName: pill.named("contextImage")
            text: qsTr("Image…")
            visible: !pill.onPdfText && !pill.reading
            onClicked: { pill.imageRequested(); pill.close() }
        }
        // For reading only: finding the way in it
        ToolButton {
            objectName: pill.named("contextGoToPage")
            text: qsTr("Go to page…")
            visible: !pill.onPdfText && pill.reading
            onClicked: { pill.close(); pill.goToPageRequested() }
        }
        ToolButton {
            objectName: pill.named("contextFitWidth")
            text: qsTr("Fit width")
            visible: !pill.onPdfText && pill.reading
            onClicked: { pill.target.fitWidth(); pill.close() }
        }
    }
}
