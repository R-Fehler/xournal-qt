// The size of existing pages (qt/src/canvas/PageResize.h): a paper format (or the page's own size, "Other"),
// portrait or landscape, for this page, the selected pages or all pages. What is on the pages stays where it is (from
// the top left); the page's Markdown text flows anew. Pages with a PDF background keep the PDF's size. One undo step.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "pageSizeDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Page size")
    width: Math.min(parent ? parent.width * 0.94 : 520, 520)

    /// The page it was opened for (0-based), and the selected pages (more than one, else none)
    property int page: 0
    property var selection: []
    /// 0: this page, 1: the selected pages, 2: all pages
    property int scope: 0
    /// Index in the paper formats; -1: the page's own size ("Other")
    property int paper: 4
    property bool landscape: false
    /// The page's own size when it is none of the formats (portrait, points; 0: it is one of them)
    property real otherWidth: 0
    property real otherHeight: 0
    property string otherText: ""

    readonly property var scopes: selection.length > 1 ? [0, 1, 2] : [0, 2]
    readonly property var targets: !visible ? [] : scope === 2 ? app.allPages() : scope === 1 ? selection : [page]
    /// The size chosen (points)
    readonly property size portrait: paper >= 0 ? app.settings.paperFormatSize(paper) : Qt.size(otherWidth, otherHeight)
    readonly property real chosenWidth: landscape ? portrait.height : portrait.width
    readonly property real chosenHeight: landscape ? portrait.width : portrait.height
    readonly property var info: visible ? app.pageSizePreview(targets, chosenWidth, chosenHeight) : ({})

    function openFor(list) {
        const current = app.pageNumber - 1
        if (list && list.length > 1) {
            selection = list
            page = list.indexOf(current) >= 0 ? current : list[0]
            scope = 1
        } else {
            page = list && list.length === 1 ? list[0] : current
            const selected = app.pages.selectedPages()
            selection = selected.length > 1 ? selected : []
            scope = 0
        }
        const s = app.pageSizeOf(page)
        landscape = s.landscape === true
        paper = s.paper !== undefined ? s.paper : 4
        otherWidth = paper < 0 ? Math.min(s.width, s.height) : 0
        otherHeight = paper < 0 ? Math.max(s.width, s.height) : 0
        otherText = paper < 0 ? s.text : ""
        paperBox.currentIndex = paper >= 0 ? paper : paperBox.count - 1  // (a choice made before took the binding)
        open()
    }
    function apply() {
        app.applyPageSize(targets, chosenWidth, chosenHeight)
        dlg.close()
    }

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label { text: qsTr("Paper") }
            ComboBox {
                id: paperBox
                objectName: "pageSizePaper"
                Layout.preferredWidth: 230
                model: dlg.otherText !== "" ? app.settings.paperFormats.concat([qsTr("Other: %1").arg(dlg.otherText)])
                                            : app.settings.paperFormats
                currentIndex: dlg.paper >= 0 ? dlg.paper : count - 1
                onActivated: {
                    const other = dlg.otherText !== "" && currentIndex === count - 1
                    dlg.paper = other ? -1 : currentIndex
                    if (!other && app.settings.paperIsWide(dlg.paper)) dlg.landscape = true  // (a slide is landscape)
                }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            spacing: 8
            ButtonGroup { id: orientation }
            Button { objectName: "pageSizePortrait"; text: qsTr("Portrait"); checkable: true; checked: !dlg.landscape; flat: true; ButtonGroup.group: orientation; onClicked: dlg.landscape = false }
            Button { objectName: "pageSizeLandscape"; text: qsTr("Landscape"); checkable: true; checked: dlg.landscape; flat: true; ButtonGroup.group: orientation; onClicked: dlg.landscape = true }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Apply to") }
            ComboBox {
                objectName: "pageSizeScope"
                Layout.fillWidth: true
                model: dlg.scopes.map(function(s) {
                    return s === 0 ? qsTr("This page (%1)").arg(dlg.page + 1)
                         : s === 1 ? qsTr("The %1 selected pages").arg(dlg.selection.length)
                                   : qsTr("All pages")
                })
                currentIndex: Math.max(0, dlg.scopes.indexOf(dlg.scope))
                onActivated: dlg.scope = dlg.scopes[currentIndex]
            }
        }
        Label {
            objectName: "pageSizeCount"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#555a60"
            visible: text !== ""
            text: (dlg.info.pages || 0) === 0 ? ((dlg.info.pdfPages || 0) > 0 ? "" : qsTr("Nothing to change: the pages have this size already."))
                : dlg.info.pages === 1 ? qsTr("1 page gets this size. What is on it stays where it is.")
                : qsTr("%1 pages get this size. What is on them stays where it is.").arg(dlg.info.pages)
        }
        Label {
            objectName: "pageSizePdfNote"
            Layout.fillWidth: true
            visible: (dlg.info.pdfPages || 0) > 0
            wrapMode: Text.WordWrap
            color: "#555a60"
            text: qsTr("PDF pages keep the PDF's size; use Space for notes to enlarge them.")
        }
        Label {
            objectName: "pageSizeOutside"
            Layout.fillWidth: true
            visible: (dlg.info.outside || 0) > 0
            wrapMode: Text.WordWrap
            color: "#a14a00"
            text: dlg.info.outside === 1
                  ? qsTr("1 element would be outside the smaller page. It is kept there, beyond the edge.")
                  : qsTr("%1 elements would be outside the smaller pages. They are kept there, beyond the edge.").arg(dlg.info.outside)
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button {
            objectName: "pageSizeApply"
            text: qsTr("Apply")
            highlighted: true
            enabled: (dlg.info.pages || 0) > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        onAccepted: dlg.apply()
        onRejected: dlg.close()
    }
}
