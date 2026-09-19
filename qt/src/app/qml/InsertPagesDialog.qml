// Insert new pages with a chosen background (blank, ruled, graph, ...), paper size and orientation, before or after
// a page. By default like the current page.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "insertPagesDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Insert pages")
    width: Math.min(parent ? parent.width * 0.94 : 640, 660)
    /// The page the new ones go next to (0-based), and where
    property int page: 0
    property bool after: true
    property int bgIndex: 0
    property int paper: -1  // -1: like the page
    property bool landscape: false

    function openAt(position) {
        // position: before this page index (page count: at the end)
        after = position > 0
        page = after ? position - 1 : 0
        open()
    }
    onAboutToShow: {
        const current = app.currentPageFormat()
        bgIndex = current.background >= 0 ? current.background : Math.max(0, app.settings.get("pageBackground"))
        landscape = current.landscape === true
        paper = -1
        countBox.value = 1
    }
    function insert() {
        app.insertPages(after ? page + 1 : page, bgIndex, paper, landscape, countBox.value)
        dlg.close()
    }

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 12
        Label { text: qsTr("Background"); font.weight: Font.DemiBold }
        BackgroundChooser {
            Layout.fillWidth: true
            selected: dlg.bgIndex
            landscape: dlg.landscape
            onChosen: function(index) { dlg.bgIndex = index }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label { text: qsTr("Paper") }
            ComboBox {
                objectName: "insertPaperBox"
                Layout.preferredWidth: 190
                model: [qsTr("Like this page")].concat(app.settings.paperFormats)
                currentIndex: dlg.paper + 1
                onActivated: dlg.paper = currentIndex - 1
            }
            Item { Layout.fillWidth: true }
            ButtonGroup { id: orientation }
            Button { text: qsTr("Portrait"); checkable: true; checked: !dlg.landscape; flat: true; ButtonGroup.group: orientation; onClicked: dlg.landscape = false }
            Button { text: qsTr("Landscape"); checkable: true; checked: dlg.landscape; flat: true; ButtonGroup.group: orientation; onClicked: dlg.landscape = true }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            SpinBox {
                id: countBox
                objectName: "insertCount"
                from: 1; to: 100
                editable: true
            }
            Label { text: countBox.value === 1 ? qsTr("page") : qsTr("pages") }
            ComboBox {
                objectName: "insertWhere"
                Layout.preferredWidth: 200
                model: [qsTr("before page %1").arg(dlg.page + 1), qsTr("after page %1").arg(dlg.page + 1)]
                currentIndex: dlg.after ? 1 : 0
                onActivated: dlg.after = currentIndex === 1
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Insert"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        onAccepted: dlg.insert()
        onRejected: dlg.close()
    }
}
