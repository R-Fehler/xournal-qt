// xournal-qt: moving documents and folders of the library by dragging them onto a folder or a breadcrumb.
// Part of HomeView.qml (the home screen, qt/docs/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    id: moveDrag
    anchors.fill: parent
    z: 20
    property bool active: false
    property int row: -1
    property string label
    property bool isFolder: false
    property int count: 1  // the dragged row and the other selected items
    property point pos
    property bool hasTarget: false
    property string target: ""

    function start(row, name, folder, p) {
        moveDrag.row = row
        label = name
        isFolder = folder
        count = app.library.pathsFor(row).length
        active = true
        moveTo(p)
    }
    function moveTo(p) {
        pos = p
        hasTarget = false
        libraryPage.libraryGrid.dropIndex = -1
        // A folder tile under the pointer
        const g = mapToItem(libraryPage.libraryGrid, p.x, p.y)
        const idx = libraryPage.libraryGrid.indexAt(libraryPage.libraryGrid.contentX + g.x, libraryPage.libraryGrid.contentY + g.y)
        const item = libraryPage.libraryGrid.itemAtIndex(idx)
        if (item && item.isFolder && idx !== row && !item.selected) {
            libraryPage.libraryGrid.dropIndex = idx
            target = app.library.relativeFolder(item.path)
            hasTarget = true
            return
        }
        // A breadcrumb (a folder above; not its "…")
        const c = mapToItem(crumbBar.crumbRow, p.x, p.y)
        const crumb = crumbBar.crumbRow.childAt(c.x, c.y)
        if (crumb && crumb.visible && crumb.folder !== undefined && c.x - crumb.x >= crumb.crumbX
                && crumb.folder !== app.library.folder) {
            target = crumb.folder
            hasTarget = true
        }
    }
    function finish() {
        if (active && hasTarget) app.library.moveTo(row, target)
        stop()
    }
    function stop() {
        active = false
        hasTarget = false
        libraryPage.libraryGrid.dropIndex = -1
    }

    Rectangle {
        visible: moveDrag.active
        x: moveDrag.pos.x - width / 2
        y: moveDrag.pos.y - height / 2
        width: Math.min(220, dragLabel.implicitWidth + 56)
        height: 44
        radius: 22
        color: "#ffffff"
        border.width: 2
        border.color: Material.accentColor
        opacity: 0.95
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Image { source: app.iconUrl(moveDrag.isFolder ? "xqt-folder" : "xqt-file-text"); sourceSize.width: 20; sourceSize.height: 20 }
            Label {
                id: dragLabel
                text: moveDrag.count > 1 ? qsTr("%1 items").arg(moveDrag.count) : moveDrag.label
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
        }
    }
}
