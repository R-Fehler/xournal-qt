// xournal-qt: a grid of document cards on the home screen: the library's documents and folders (app.library), or the
// documents opened lately (`recent`: app.recent). Both have the same columns, zoom, keys, selection and card menu; the
// library's grid also has its search (the pages with hits, the extended view), folders, drops and typing to search.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), in LibraryGridPage and RecentGridPage: it reads
// the home screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls

GridView {
    id: grid
    /// The documents opened lately (app.recent), else the library's (app.library)
    property bool recent: false
    /// The extended search: each result shows its pages with hits (taller cells; the library's grid only)
    readonly property bool extendedCells: !recent && home.extendedView
    /// The card a file is dragged over (a folder of the library; -1: none)
    property int dropIndex: -1
    anchors.fill: parent
    anchors.margins: 8
    clip: true
    model: recent ? app.recent : app.library
    keyNavigationEnabled: true
    boundsBehavior: Flickable.StopAtBounds
    readonly property int columns: home.columnsFor(width, extendedCells)
    bottomMargin: home.fabSpace  // (the last cards scroll out from under the floating "+")
    cellWidth: Math.floor(width / columns)
    // Extended search: a smaller first page, the row of pages with hits below the title.
    readonly property int stripHeight: extendedCells ? Math.round(Math.max(120, cellWidth * 0.55)) : 0
    cellHeight: extendedCells ? Math.round(cellWidth * 0.5 + 44 + stripHeight + 24) : home.cardHeight(cellWidth)
    ScrollBar.vertical: ScrollBar {}
    TouchpadMomentum { flickable: grid }
    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        onWheel: function(event) { home.zoom(event.angleDelta.y > 0 ? 1 : -1) }
    }
    PinchHandler {
        enabled: !grid.recent
        target: null
        property int startColumns: 2
        onActiveChanged: if (active) startColumns = grid.columns
        onActiveScaleChanged: {
            const n = Math.max(1, Math.min(12, Math.round(startColumns / activeScale)))
            if (n !== grid.columns) home.zoom(grid.columns - n)
        }
    }
    currentIndex: -1

    Keys.onPressed: function(event) {
        const item = grid.itemAtIndex(grid.currentIndex)
        const list = grid.model
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            list.selectAll()
        } else if (event.key === Qt.Key_Escape && list.selectionCount > 0) {
            list.clearSelection()
        } else if (!grid.recent && event.key === Qt.Key_Delete && list.selectionCount > 0) {
            home.askTrash(list, list.selectedPaths())
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && list.selectionCount > 0) {
            home.openAll(list.selectedPaths(), list)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (grid.recent) home.openRecentRow(grid.currentIndex)
            else home.openLibraryRow(grid.currentIndex)
        } else if (event.key === Qt.Key_Space && item) {
            list.toggleSelected(item.index)
        } else if (!grid.recent && event.key === Qt.Key_Backspace && !home.searching) {
            list.goUp()
        } else if (!grid.recent && event.key === Qt.Key_F2 && item) {
            home.menuTarget.model = list; home.menuTarget.row = item.index; home.menuTarget.name = item.name
            libraryDialogs.renameDialog.open()
        } else if (event.key === Qt.Key_Delete && item) {
            // (Recent: the card, or the selection it is part of)
            home.askTrash(list, grid.recent ? list.pathsFor(item.index) : [item.path])
        } else if (!grid.recent && event.text.length === 1 && event.text.charCodeAt(0) > 32
                   && event.text.charCodeAt(0) !== 127 && !(event.modifiers & Qt.ControlModifier)) {
            // Typing searches - visible characters only (Escape, Backspace, Delete have a text too)
            searchGroup.type(event.text)
        } else {
            return
        }
        event.accepted = true
    }

    // (roles through `model`: required properties would shadow the card's own. Recent's rows have no folders, hits,
    // tags or conflicts: a library's folder there is a library)
    delegate: DocumentCard {
        id: card
        required property int index
        required property var model
        readonly property bool fromLibrary: !grid.recent
        name: model.name
        nameMarks: fromLibrary && home.searching && home.lib.fuzzySearch ? model.nameMarks : []
        path: model.path
        cover: model.cover
        isFolder: fromLibrary ? model.isFolder : model.isLibrary
        isLibrary: fromLibrary ? false : model.isLibrary
        isStickers: fromLibrary && model.isFolder && model.path === app.stickers.libraryFolder
        isTemplates: fromLibrary && model.isFolder && model.path === app.templates.libraryFolder
        hasPdf: model.hasPdf
        lastRead: fromLibrary ? (model.lastRead ? home.formatDate(model.lastRead) : "")
                              : (model.isLibrary ? "" : home.formatDate(model.opened))
        lastPage: model.lastPage
        hasXopp: model.hasXopp
        kind: model.kind
        pdfKind: model.pdfKind
        versions: model.versions || 0
        locked: model.locked || false
        fileIcon: fromLibrary ? model.fileIcon : ""
        hits: fromLibrary ? model.hits : 0
        conflicts: fromLibrary && model.conflicts ? model.conflicts.length : 0
        onConflictsRequested: importDialogs.conflictDialog.show(model.path)
        snippet: fromLibrary ? model.snippet : ""
        itemCount: fromLibrary ? model.itemCount : 0
        hitPages: grid.extendedCells ? model.hitPageList : []
        hitPageBase: fromLibrary ? model.hitPageBase : ""
        hitPassages: grid.extendedCells ? model.hitPassageList : []
        hitPassageBase: fromLibrary ? model.hitPassageBase : ""
        stripHeight: grid.extendedCells && !model.isFolder ? grid.stripHeight : 0
        favourite: fromLibrary ? model.favourite
                               : (home.favouriteRevision, !model.isLibrary && app.isFavouriteFile(model.path))
        tags: fromLibrary && model.tags ? model.tags : []
        onFavouriteToggled: {
            if (fromLibrary) app.library.setFavourite(model.path, !model.favourite)
            else app.setFavouriteFile(model.path, !favourite)
        }
        onPageActivated: function(pageNo) { app.openSearchHitAt(model.path, home.lib.searchQuery, pageNo) }
        onPassageActivated: function(passage) {
            app.openSearchHitInPassage(model.path, home.lib.searchQuery, passage)
        }
        width: grid.cellWidth
        height: grid.cellHeight
        twoLineName: home.twoLineNames(grid.cellWidth)
        active: home.visible
        row: index
        dragOverlay: fromLibrary && !home.searching ? moveDrag : null
        selected: model.selected
        selectionMode: grid.model.selectionCount > 0
        highlighted: GridView.isCurrentItem && grid.activeFocus
        dropTarget: grid.dropIndex === index
        subtitle: {
            if (!fromLibrary) return home.formatDate(model.opened) + " · " + model.location
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
            grid.currentIndex = index
            grid.forceActiveFocus()
            home.cardActivated(grid.model, index, modifiers)
        }
        onToggleRequested: grid.model.toggleSelected(index)
        onMenuRequested: function(item, x, y) {
            grid.currentIndex = index
            home.showMenu(grid.model, index, model.name, model.path, fromLibrary && model.isFolder, item, x, y, model.kind)
        }
        onRenameAccepted: function(newName) { grid.model.rename(index, newName) }
    }
}
