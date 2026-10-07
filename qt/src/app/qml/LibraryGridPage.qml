// xournal-qt: the library's page of the home screen: the grid of its documents and folders (DocumentGrid), the empty
// library, files dropped on it.
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
    readonly property alias libraryGrid: libraryGrid
    DocumentGrid {
        id: libraryGrid
        objectName: "libraryGrid"
    }

    // Empty library / folder / search
    ColumnLayout {
        anchors.centerIn: parent
        visible: libraryGrid.count === 0 && app.library.available
        spacing: 10
        width: Math.min(parent.width - 40, 460)
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: app.iconUrl(home.searching ? "xqt-search" : "xqt-library")
            sourceSize.width: 56; sourceSize.height: 56
            opacity: 0.5
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: 16
            color: "#5f6368"
            text: home.searching ? (app.library.indexing ? qsTr("Nothing found yet (still indexing)") : qsTr("Nothing found"))
                  : app.library.favouritesOnly ? qsTr("No favourites yet: tap the star on a document")
                  : app.library.tagFilter !== "" ? qsTr("No documents tagged #%1 here").arg(app.library.tagFilter)
                  : app.library.folder !== "" ? qsTr("This folder is empty")
                  : qsTr("Your library is empty")
        }
        Label {
            visible: !home.searching && !app.library.favouritesOnly && app.library.tagFilter === ""
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#80868b"
            text: qsTr("Drop PDFs and Xournal files here, import them, or create a new document.")
        }
        // (one below the other when they do not fit side by side)
        GridLayout {
            visible: !home.searching && !app.library.favouritesOnly && app.library.tagFilter === ""
            Layout.alignment: Qt.AlignHCenter
            columns: emptyNew.implicitWidth + emptyImport.implicitWidth + emptyImportFolder.implicitWidth
                     + 2 * columnSpacing <= parent.width ? 3 : 1
            Button { id: emptyNew; objectName: "emptyNewDocument"; Layout.alignment: Qt.AlignHCenter; text: qsTr("New document"); highlighted: true; onClicked: newDocumentDialog.open() }
            Button { id: emptyImport; objectName: "emptyImportFiles"; Layout.alignment: Qt.AlignHCenter; text: qsTr("Import files…"); flat: true; onClicked: importDialogs.importDialog.open() }
            Button { id: emptyImportFolder; objectName: "emptyImportFolder"; Layout.alignment: Qt.AlignHCenter; text: qsTr("Import a folder…"); flat: true; onClicked: importDialogs.importFolderDialog.open() }
        }
    }

    // Files dropped from the file manager are copied into the folder (or onto a folder tile).
    DropArea {
        id: fileDrop
        objectName: "libraryDropArea"
        anchors.fill: parent
        keys: ["text/uri-list"]
        enabled: app.library.available
        property string targetFolder: app.library.folder
        function update(x, y) {
            const p = mapToItem(libraryGrid, x, y)
            const idx = libraryGrid.indexAt(libraryGrid.contentX + p.x, libraryGrid.contentY + p.y)
            const item = libraryGrid.itemAtIndex(idx)
            if (item && item.isFolder && !home.searching) {
                libraryGrid.dropIndex = idx
                targetFolder = app.library.relativeFolder(item.path)
            } else {
                libraryGrid.dropIndex = -1
                targetFolder = home.searching || app.library.flat ? "" : app.library.folder
            }
        }
        onEntered: function(drag) {
            drag.accepted = drag.hasUrls
            update(drag.x, drag.y)
        }
        onPositionChanged: function(drag) { update(drag.x, drag.y) }
        onExited: libraryGrid.dropIndex = -1
        onDropped: function(drop) {
            libraryGrid.dropIndex = -1
            if (drop.hasUrls && drop.urls.length === 1 && String(drop.urls[0]).toLowerCase().endsWith(".zip")
                    && !app.library.contains(drop.urls[0])) {
                app.openUrls(drop.urls)  // (a zip: "Open in library…")
                drop.accept(Qt.CopyAction)
            } else if (drop.hasUrls) {
                const urls = drop.urls, folder = targetFolder
                home.confirmImport(app.library.temporary, function() { app.library.importUrls(urls, folder) })
                drop.accept(Qt.CopyAction)
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 6
            visible: fileDrop.containsDrag && libraryGrid.dropIndex < 0
            color: "#10283593"
            radius: 14
            border.width: 2
            border.color: Material.accentColor
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
                text: qsTr("Copy into %1").arg(fileDrop.targetFolder === "" ? app.library.name : fileDrop.targetFolder)
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: Material.accentColor
            }
        }
    }
}
