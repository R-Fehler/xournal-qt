// xournal-qt: the library's page of the home screen: the grid of its documents and folders, the empty library,
// files dropped on it.
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
    GridView {
        id: libraryGrid
        objectName: "libraryGrid"
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        model: app.library
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property int columns: home.columnsFor(width, home.extendedView)
        property int dropIndex: -1
        bottomMargin: home.fabSpace  // (the last cards scroll out from under the floating "+")
        cellWidth: Math.floor(width / columns)
        // Extended search: a smaller first page, the row of pages with hits below the title.
        readonly property int stripHeight: home.extendedView ? Math.round(Math.max(120, cellWidth * 0.55)) : 0
        cellHeight: home.extendedView ? Math.round(cellWidth * 0.5 + 44 + stripHeight + 24)
                                      : home.cardHeight(cellWidth)
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: libraryGrid }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) { home.zoom(event.angleDelta.y > 0 ? 1 : -1) }
        }
        PinchHandler {
            target: null
            property int startColumns: 2
            onActiveChanged: if (active) startColumns = libraryGrid.columns
            onActiveScaleChanged: {
                const n = Math.max(1, Math.min(12, Math.round(startColumns / activeScale)))
                if (n !== libraryGrid.columns) home.zoom(libraryGrid.columns - n)
            }
        }
        currentIndex: -1

        Keys.onPressed: function(event) {
            const item = libraryGrid.itemAtIndex(libraryGrid.currentIndex)
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                app.library.selectAll()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && app.library.selectionCount > 0) {
                app.library.clearSelection()
                event.accepted = true
            } else if (event.key === Qt.Key_Delete && app.library.selectionCount > 0) {
                home.askTrash(app.library, app.library.selectedPaths())
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && app.library.selectionCount > 0) {
                home.openAll(app.library.selectedPaths(), app.library)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                home.openLibraryRow(libraryGrid.currentIndex)
                event.accepted = true
            } else if (event.key === Qt.Key_Space && item) {
                app.library.toggleSelected(item.index)
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace && !home.searching) {
                app.library.goUp()
                event.accepted = true
            } else if (event.key === Qt.Key_F2 && item) {
                home.menuModel = app.library; home.menuRow = item.index; home.menuName = item.name
                libraryDialogs.renameDialog.open()
                event.accepted = true
            } else if (event.key === Qt.Key_Delete && item) {
                home.askTrash(app.library, [item.path])
                event.accepted = true
            } else if (event.text.length === 1 && event.text.charCodeAt(0) > 32 && event.text.charCodeAt(0) !== 127
                       && !(event.modifiers & Qt.ControlModifier)) {
                // Typing searches - visible characters only (Escape, Backspace, Delete have a text too)
                searchGroup.searchField.forceActiveFocus()
                searchGroup.searchField.text += event.text
                home.typed()
                event.accepted = true
            }
        }

        // (roles through `model`: required properties would shadow the card's own)
        delegate: DocumentCard {
            id: libCard
            required property int index
            required property var model
            name: model.name
            nameMarks: home.searching && home.lib.fuzzySearch ? model.nameMarks : []
            path: model.path
            isFolder: model.isFolder
            isStickers: model.isFolder && model.path === app.stickers.libraryFolder
            isTemplates: model.isFolder && model.path === app.templates.libraryFolder
            cover: model.cover
            hasPdf: model.hasPdf
            lastRead: model.lastRead ? home.formatDate(model.lastRead) : ""
            lastPage: model.lastPage
            hasXopp: model.hasXopp
            kind: model.kind
            pdfKind: model.pdfKind
            versions: model.versions || 0
            locked: model.locked || false
            fileIcon: model.fileIcon
            hits: model.hits
            conflicts: model.conflicts ? model.conflicts.length : 0
            onConflictsRequested: importDialogs.conflictDialog.show(model.path)
            snippet: model.snippet
            itemCount: model.itemCount
            hitPages: home.extendedView ? model.hitPageList : []
            hitPageBase: model.hitPageBase
            hitPassages: home.extendedView ? model.hitPassageList : []
            hitPassageBase: model.hitPassageBase
            stripHeight: home.extendedView && !model.isFolder ? libraryGrid.stripHeight : 0
            favourite: model.favourite
            tags: model.tags ? model.tags : []
            onFavouriteToggled: app.library.setFavourite(model.path, !model.favourite)
            onPageActivated: function(pageNo) { app.openSearchHitAt(model.path, home.lib.searchQuery, pageNo) }
            onPassageActivated: function(passage) {
                app.openSearchHitInPassage(model.path, home.lib.searchQuery, passage)
            }
            width: libraryGrid.cellWidth
            height: libraryGrid.cellHeight
            twoLineName: home.twoLineNames(libraryGrid.cellWidth)
            active: home.visible
            row: index
            dragOverlay: home.searching ? null : moveDrag
            selected: model.selected
            selectionMode: app.library.selectionCount > 0
            highlighted: GridView.isCurrentItem && libraryGrid.activeFocus
            dropTarget: libraryGrid.dropIndex === index
            subtitle: {
                if (model.isFolder) {
                    const items = model.itemCount === 1 ? qsTr("1 item") : qsTr("%1 items").arg(model.itemCount)
                    return home.searching && model.location !== "" ? model.location + " · " + items : items
                }
                const parts = []
                if ((home.searching || app.library.flat) && model.location !== "") parts.push(model.location)
                if (model.kind === "other" || model.kind === "text") parts.push(home.sizeText(model.size))
                if (model.pageCount >= 0) parts.push(home.pagesText(model.pageCount))
                parts.push(home.formatDate(model.modified))
                return parts.join(" · ")
            }
            onActivated: function(modifiers) {
                libraryGrid.currentIndex = index
                libraryGrid.forceActiveFocus()
                home.cardActivated(app.library, index, modifiers)
            }
            onToggleRequested: app.library.toggleSelected(index)
            onMenuRequested: function(item, x, y) {
                libraryGrid.currentIndex = index
                home.showMenu(app.library, index, model.name, model.path, model.isFolder, item, x, y, model.kind)
            }
            onRenameAccepted: function(newName) { app.library.rename(index, newName) }
        }
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
