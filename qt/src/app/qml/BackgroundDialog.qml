// Gives pages another background (plain, ruled, graph, ...). Pages that show a page of the PDF lose it, so those
// are warned about first. One undo step.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "backgroundDialog"
    title: pages.length === 1 ? qsTr("Background of page %1").arg(pages[0] + 1)
                              : qsTr("Background of %1 pages").arg(pages.length)
    preferredWidth: 660
    standardButtons: Dialog.Cancel | Dialog.Ok

    /// 0-based page numbers
    property var pages: []
    property int bgIndex: 0
    /// The paper (qt/docs/features/dark-pages.md): its color and texture
    property color paper: "#ffffff"
    property bool textured: false
    readonly property bool overPdf: app.pagesHavePdfBackground(pages)

    function openFor(list) {
        pages = list && list.length > 0 ? list : [app.pageNumber - 1]
        const current = app.currentPageFormat()
        bgIndex = current && current.background >= 0 ? current.background
                                                     : Math.max(0, app.settings.get("pageBackground"))
        // A page of the PDF has no paper of its own: the paper of new pages
        paper = current && current.pdf === false ? current.paper : app.settings.get("pageColor")
        textured = current && current.pdf === false ? current.textured === true : app.settings.get("pageTexture") === true
        open()
    }
    onAccepted: app.changePageBackground(pages, bgIndex, paper, textured ? 1 : 0)

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
            paper: dlg.paper
            onChosen: function(index) { dlg.bgIndex = index }
        }
        Label { text: qsTr("Paper"); font.weight: Font.DemiBold }
        PaperSwatches {
            Layout.fillWidth: true
            paper: dlg.paper
            textured: dlg.textured
            onChosen: function(c) { dlg.paper = c }
            onTexturedToggled: function(on) { dlg.textured = on }
        }
    }
}
