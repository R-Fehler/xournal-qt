// Gives pages another background (plain, ruled, graph, ...). Pages that show a page of the PDF lose it, so those
// are warned about first. One undo step.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "backgroundDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: pages.length === 1 ? qsTr("Background of page %1").arg(pages[0] + 1)
                              : qsTr("Background of %1 pages").arg(pages.length)
    width: Math.min(parent ? parent.width * 0.94 : 640, 660)
    standardButtons: Dialog.Cancel | Dialog.Ok

    /// 0-based page numbers
    property var pages: []
    property int bgIndex: 0
    readonly property bool overPdf: app.pagesHavePdfBackground(pages)

    function openFor(list) {
        pages = list && list.length > 0 ? list : [app.pageNumber - 1]
        const current = app.currentPageFormat()
        bgIndex = current && current.background >= 0 ? current.background
                                                     : Math.max(0, app.settings.get("pageBackground"))
        open()
    }
    onAccepted: app.changePageBackground(pages, bgIndex)

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 10
        Rectangle {
            objectName: "pdfWarning"
            visible: dlg.overPdf
            Layout.fillWidth: true
            color: "#fff4e5"
            border.width: 1
            border.color: "#f0b86e"
            radius: 8
            implicitHeight: warning.implicitHeight + 16
            Label {
                id: warning
                anchors.fill: parent
                anchors.margins: 8
                wrapMode: Text.Wrap
                text: dlg.pages.length === 1
                      ? qsTr("This page shows a page of the PDF. The new background replaces it — what was drawn stays.")
                      : qsTr("Some of these pages show pages of the PDF. The new background replaces them — what was drawn stays.")
            }
        }
        BackgroundChooser {
            id: chooser
            Layout.fillWidth: true
            selected: dlg.bgIndex
            onChosen: function(index) { dlg.bgIndex = index }
        }
    }
}
