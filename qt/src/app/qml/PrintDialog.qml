// Printing: what goes on the paper (what was written, or only the PDF that is annotated) and which pages. The
// printer itself, copies and duplex come from the system's print dialog afterwards.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "printDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Print")
    width: Math.min(parent ? parent.width * 0.9 : 460, 460)
    standardButtons: Dialog.Cancel | Dialog.Ok

    property bool withAnnotations: true
    property string pages: "all"   ///< "all", "current" or "range"
    readonly property string range: pages === "current" ? String(app.pageNumber)
                                    : pages === "range" ? rangeField.text.trim() : ""

    /// Opens it for these pages (0-based, e.g. what is selected in the page overview).
    function openFor(list) {
        open()
        if (list && list.length > 0) {
            rangeField.text = rangeOf(list)
            pages = "range"
        }
    }
    /// [0,1,2,4] -> "1-3,5"
    function rangeOf(list) {
        const sorted = list.map(p => p + 1).sort((a, b) => a - b)
        const parts = []
        let from = sorted[0], previous = sorted[0]
        for (let i = 1; i <= sorted.length; ++i) {
            const page = sorted[i]
            if (page !== previous + 1) {
                parts.push(from === previous ? String(from) : from + "-" + previous)
                from = page
            }
            previous = page
        }
        return parts.join(",")
    }

    onAboutToShow: {
        withAnnotations = true
        pages = "all"
        rangeField.text = "1-" + app.pageCount
    }
    onAccepted: app.printDocument(withAnnotations, range)

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 4

        Label { text: qsTr("What"); font.weight: Font.DemiBold; visible: app.hasPdfBackground() }
        RadioButton {
            objectName: "printWithAnnotations"
            visible: app.hasPdfBackground()
            text: qsTr("The document with everything on it")
            checked: dlg.withAnnotations
            onClicked: dlg.withAnnotations = true
        }
        RadioButton {
            objectName: "printPdfOnly"
            visible: app.hasPdfBackground()
            text: qsTr("Only the PDF, without what was written")
            checked: !dlg.withAnnotations
            onClicked: dlg.withAnnotations = false
        }

        Label { text: qsTr("Pages"); font.weight: Font.DemiBold; topPadding: 8 }
        RadioButton {
            objectName: "printAllPages"
            text: qsTr("All %1 pages").arg(app.pageCount)
            checked: dlg.pages === "all"
            onClicked: dlg.pages = "all"
        }
        RadioButton {
            objectName: "printCurrentPage"
            text: qsTr("This page (%1)").arg(app.pageNumber)
            checked: dlg.pages === "current"
            onClicked: dlg.pages = "current"
        }
        RowLayout {
            RadioButton {
                objectName: "printRange"
                text: qsTr("Pages")
                checked: dlg.pages === "range"
                onClicked: dlg.pages = "range"
            }
            TextField {
                id: rangeField
                objectName: "printRangeField"
                Layout.preferredWidth: 140
                placeholderText: qsTr("e.g. 2-5")
                onActiveFocusChanged: if (activeFocus) dlg.pages = "range"
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.Wrap
            color: "#5f6368"
            text: qsTr("The printer, the number of copies and printing on both sides come next, in the dialog of the system.")
        }
    }
}
