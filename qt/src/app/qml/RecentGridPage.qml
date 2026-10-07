// xournal-qt: the home screen's page of the documents opened lately.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    // (what the other parts use)
    readonly property alias recentGrid: recentGrid
    GridView {
        id: recentGrid
        objectName: "recentGrid"
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        model: app.recent
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property int columns: home.columnsFor(width, false)
        bottomMargin: home.fabSpace
        cellWidth: Math.floor(width / columns)
        cellHeight: home.cardHeight(cellWidth)
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: recentGrid }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) { home.zoom(event.angleDelta.y > 0 ? 1 : -1) }
        }
        currentIndex: -1
        Keys.onPressed: function(event) {
            const item = recentGrid.itemAtIndex(recentGrid.currentIndex)
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                app.recent.selectAll()
            } else if (event.key === Qt.Key_Escape && app.recent.selectionCount > 0) {
                app.recent.clearSelection()
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (app.recent.selectionCount > 0) home.openAll(app.recent.selectedPaths(), app.recent)
                else home.openRecentRow(recentGrid.currentIndex)
            } else if (event.key === Qt.Key_Space && item) {
                app.recent.toggleSelected(item.index)
            } else if (event.key === Qt.Key_Delete && item) {
                home.askTrash(app.recent, app.recent.pathsFor(item.index))
            } else {
                return
            }
            event.accepted = true
        }

        delegate: DocumentCard {
            required property int index
            required property var model
            name: model.name
            path: model.path
            cover: model.cover
            isFolder: model.isLibrary
            isLibrary: model.isLibrary
            hasPdf: model.hasPdf
            lastRead: model.isLibrary ? "" : home.formatDate(model.opened)
            lastPage: model.lastPage
            hasXopp: model.hasXopp
            kind: model.kind
            pdfKind: model.pdfKind
            versions: model.versions || 0
            locked: model.locked || false
            width: recentGrid.cellWidth
            height: recentGrid.cellHeight
            twoLineName: home.twoLineNames(recentGrid.cellWidth)
            active: home.visible
            row: index
            selected: model.selected
            selectionMode: app.recent.selectionCount > 0
            highlighted: GridView.isCurrentItem && recentGrid.activeFocus
            subtitle: home.formatDate(model.opened) + " · " + model.location
            favourite: (home.favouriteRevision, !model.isLibrary && app.isFavouriteFile(model.path))
            onFavouriteToggled: app.setFavouriteFile(model.path, !favourite)
            onActivated: function(modifiers) {
                recentGrid.currentIndex = index
                recentGrid.forceActiveFocus()
                home.cardActivated(app.recent, index, modifiers)
            }
            onToggleRequested: app.recent.toggleSelected(index)
            onMenuRequested: function(item, x, y) {
                recentGrid.currentIndex = index
                home.showMenu(app.recent, index, model.name, model.path, false, item, x, y, model.kind)
            }
            onRenameAccepted: function(newName) { app.recent.rename(index, newName) }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        visible: recentGrid.count === 0
        spacing: 10
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: app.iconUrl("xqt-history")
            sourceSize.width: 56; sourceSize.height: 56
            opacity: 0.5
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 16
            color: "#5f6368"
            text: qsTr("Documents you open appear here")
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button { text: qsTr("New document"); highlighted: true; onClicked: newDocumentDialog.open() }
            Button { text: qsTr("Open…"); flat: true; onClicked: home.openFileRequested() }
        }
    }
}
