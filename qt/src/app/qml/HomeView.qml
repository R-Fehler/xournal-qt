// Home screen (shown when no document is open, or with the home tab): the library of this window and the recently
// opened documents, as grids of first-page previews.
//  - Library: folders (tap to enter, breadcrumbs to go back) or all documents at once; search in folder names and
//    the text and names of all documents; new document, import (files or whole folder trees, also by dropping them),
//    new folder; rename (the menu, F2, or in place: press and hold or double click on a card's title), move (drag
//    onto a folder or a breadcrumb, or "Move to"), move to trash. A .xopp and its PDF are one document.
//  - Recent: documents opened lately that still exist; rename, remove from the list, copy / move into the library.
//  - Several documents and folders can be selected (Ctrl / Shift + click, the circle on a card, or "Select" in the
//    menu; then taps select more) and opened, copied, moved or trashed together.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: home
    objectName: "homeView"
    color: "#eef0f3"

    // --- the layout for the window's size
    // (qt/docs/features/adaptive-layout.md, "The home screen and the tab overview")
    /// The window's layout (Main.qml's `win.adaptive`), if there is one
    readonly property var adaptive: win.adaptive
    readonly property string layoutClass: adaptive.layoutClass
    /// A phone class: the "+" floats at the bottom, the actions on a selection are a bar at the bottom
    readonly property bool phoneLayout: adaptive.phoneLayout
    /// A phone held sideways (or another short window of a phone class): one header row with the breadcrumbs in it,
    /// shorter cards
    readonly property bool shortLayout: phoneLayout && (layoutClass === "phoneShort"
                                                        || (adaptive.orientation === "landscape"))
    /// A phone held upright: the switch Library / Recent / Bookmarks in a row of its own, across the width
    readonly property bool portraitPhone: phoneLayout && !shortLayout
    readonly property bool touch: adaptive.touchProfile
    /// What a finger needs (48 with the touch profile, else 40)
    readonly property int minTarget: adaptive.minTarget
    /// The part of its bottom under the navigation bar (not where the soft keyboard's room is below it)
    readonly property real safeBottom: win.insets.contentBottomInset
    /// Room below the last cards for the floating "+" of a phone
    readonly property real fabSpace: phoneLayout ? 72 : 0

    // The header's ladder: every action a button of its own where the row has room for all of them with the search
    // at its smallest ("expanded", a wide desktop window); else New and Import behind "+" and the ways to show the
    // cards behind "View" ("grouped"). Only one of the two is there at a time: no action in two places.
    /// The row's width (16 px margin at the left, 8 at the right)
    readonly property real headerRoom: width - 24
    readonly property real searchMinimum: 150 + 6 + 48
    /// The switch as icons: the library's name, its ▾, Recent, Favourites, Tags (not on a phone: in the ▾ menu there),
    /// Bookmarks, To-dos
    readonly property real switchNeed: switchBox.libraryTab.implicitWidth + switchBox.libraryMenuButton.implicitWidth
                                       + (phoneLayout ? 4 : 5) * 40 + (phoneLayout ? 5 : 6) * 2 + 8
    /// What the words beside the icons of Recent, Favourites and Bookmarks add
    readonly property real switchWords: recentWord.advanceWidth + favouritesWord.advanceWidth + bookmarksWord.advanceWidth
                                        + todosWord.advanceWidth + (phoneLayout ? 0 : tagsWord.advanceWidth + 12) + 4 * 12
    TextMetrics { id: recentWord; text: qsTr("Recent") }
    TextMetrics { id: favouritesWord; text: qsTr("Favourites") }
    TextMetrics { id: bookmarksWord; text: qsTr("Bookmarks") }
    TextMetrics { id: todosWord; text: qsTr("To-dos") }
    TextMetrics { id: tagsWord; text: qsTr("Tags") }
    /// What the expanded row needs: the switch, a search wide enough for its placeholder (300; below that the grouped
    /// row with a wider search is better), New, Quick note, Last page, Import, New folder, Flat, Show, Sort, − and +
    /// with their separators, Settings, and the spacing between them
    readonly property real expandedNeed: switchNeed + 300 + 6 + 48 + 48 + 48 + homeHeader.resume.implicitWidth + 5 * 48
                                         + 2 * 40 + 2 * 13 + 48 + 16 * 6
    readonly property bool expanded: !phoneLayout && headerRoom >= expandedNeed
    /// The keys of Quick note, for its tip
    readonly property string quickNoteKeys: (app.shortcuts.revision, app.shortcuts.keys("quickNote").join(", "))
    /// Room for the words beside the icons of the switch (Recent, Favourites, Bookmarks), and a search at its full
    /// width. One rule: words only where the header has room for every button and them; everywhere else icons (a tip
    /// on hover, and while a finger is held on them)
    readonly property bool roomy: expanded && headerRoom >= expandedNeed + 230 + switchWords
    /// The grouped row: the switch, the search at its smallest, "+", View and Settings
    readonly property real groupedNeed: switchNeed + searchMinimum + 3 * 48 + 6 * 6
    /// The search in a row of its own: on a phone upright, and in a window too narrow for it in the header
    readonly property bool searchOwnRow: portraitPhone || (!phoneLayout && !expanded && headerRoom < groupedNeed)
    /// The actions on a selection in a bar at the bottom (Open, Copy, Move, Trash, ⋮): on a phone, and where the
    /// selection bar's row does not fit
    readonly property bool selectionAtBottom: phoneLayout || selectionBar.selectionFull.implicitWidth + selectionBar.selectionLabel.implicitWidth + 96 > width
    /// 0: library, 1: recent documents, 2: the library's bookmarks (qt/docs/features/bookmarks.md), 3: its to-dos
    /// (qt/docs/features/todos.md), 4: its tags (qt/docs/features/tags.md)
    property int page: app.library.available ? 0 : 1
    /// Changes when a star is set or taken away (the Recent cards ask for theirs)
    property int favouriteRevision: 0
    readonly property var lib: app.library
    readonly property bool searching: lib.searchQuery !== ""
    /// Extended search: each result shows its pages with hits (taller cells).
    property bool extended: false
    readonly property bool extendedView: extended && searching && page === 0
    // Grid zoom: columns chosen with − / + (Ctrl+wheel, pinch); 0: from the width. Separate for the extended view.
    property int columnsNormal: 0
    property int columnsExtended: 0
    function autoColumns(width, extendedCells) { return Math.max(extendedCells ? 1 : 2, Math.floor(width / (extendedCells ? 380 : 210))) }
    /// The fewest columns: two cards side by side on a phone (the extended search: one)
    function fewestColumns(extendedCells) { return phoneLayout && !extendedCells ? 2 : 1 }
    function columnsFor(width, extendedCells) {
        const chosen = extendedCells ? columnsExtended : columnsNormal
        return chosen > 0 ? Math.max(fewestColumns(extendedCells), chosen) : autoColumns(width, extendedCells)
    }
    /// Narrow cards (two columns on a phone) show a name on two lines rather than cut short
    function twoLineNames(cellWidth) { return phoneLayout && cellWidth < 240 }
    /// A card: its first page (a sheet of paper, a little taller than wide) and the name below; on a phone held
    /// sideways about as high as wide, so that a whole row of them fits under the header
    function cardHeight(cellWidth) {
        return Math.round(cellWidth * (shortLayout ? 0.8 : 1.2) + 44 + (twoLineNames(cellWidth) ? 18 : 0))
    }
    /// −1: smaller cells (more columns), +1: bigger cells
    function zoom(step) {
        const grid = page === 0 ? libraryPage.libraryGrid : recentPage.recentGrid
        const n = Math.max(fewestColumns(extendedView && page === 0), Math.min(12, grid.columns - step))
        if (extendedView) columnsExtended = n
        else columnsNormal = n
    }
    /// "Open a file" (the window's file dialog)
    signal openFileRequested()
    signal settingsRequested()
    /// Share… of a PDF or notes card (a PDF: the file itself, or a copy for Xournal++; notes: opened first)
    signal shareRequested(string path)

    function formatDate(d) {
        if (!d || isNaN(d.getTime())) return ""
        const now = new Date()
        if (d.toDateString() === now.toDateString()) return qsTr("Today %1").arg(d.toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
        return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    function sizeText(bytes) {
        if (bytes < 0) return ""
        if (bytes < 1024) return qsTr("%1 B").arg(bytes)
        if (bytes < 1024 * 1024) return qsTr("%1 KB").arg(Math.round(bytes / 1024))
        if (bytes < 1024 * 1024 * 1024) return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
        return qsTr("%1 GB").arg((bytes / (1024 * 1024 * 1024)).toFixed(1))
    }
    function pagesText(n) { return n < 0 ? "" : (n === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(n)) }
    function focusGrid() {
        (page === 0 ? libraryPage.libraryGrid : page === 2 ? bookmarksView : page === 3 ? todosView : page === 4 ? tagsView
                                                                                               : recentPage.recentGrid).forceActiveFocus()
    }
    function focusSearch() {
        page = 0
        searchGroup.searchField.forceActiveFocus()
        searchGroup.searchField.selectAll()
    }
    /// Search the library for this text, right away (selected text: the look-up menu)
    function searchFor(text) {
        page = 0
        searchGroup.searchTyping.stop()
        searchGroup.searchField.text = text
        lib.searchQuery = text
        libraryPage.libraryGrid.forceActiveFocus()
    }
    onVisibleChanged: if (visible) focusGrid()
    onPageChanged: focusGrid()
    function openLibraryRow(index) {
        const item = libraryPage.libraryGrid.itemAtIndex(index)
        if (!item) return
        if (item.isFolder) {
            searchGroup.searchTyping.stop()
            searchGroup.searchField.text = ""  // a folder found by the search: show it
            lib.searchQuery = ""
            lib.folder = lib.relativeFolder(item.path)
        } else if (home.searching) {
            app.openSearchHit(item.path, lib.searchQuery)
        } else {
            app.openListed([item.path])  // (other files: with their app)
        }
    }
    function openRecentRow(index) {
        const item = recentPage.recentGrid.itemAtIndex(index)
        if (item && item.isLibrary) openLibraryFolder(item.path)
        else if (item) app.openListed([item.path])
    }
    /// "Open a folder as library…": the folder picker. On Android a folder of the phone's storage can be a library
    /// only with "All files access": explained and asked for first (then the picker opens, see onPickLibraryFolder).
    /// With it, Android's picker is replaced by the app's own folder list (the picker refuses the Download folder).
    function pickLibraryFolder() {
        if (!app.storageAccess) libraryDialogs.storageAccessDialog.ask("")
        else if (app.inAppFolderChooser) importDialogs.folderChooser.openAt(app.storageRoot)
        else importDialogs.openLibraryDialog.open()
    }
    /// A folder as library: this one's home screen, else a window of its own (raised if it is open already)
    function openLibraryFolder(path) {
        if (app.library.available && path === app.library.rootPath) {
            app.homeVisible = true
            page = 0
        } else {
            app.openLibraryAt(path)
        }
    }
    readonly property var currentModel: page === 1 ? app.recent : app.library
    readonly property int selectionCount: currentModel.selectionCount
    /// A tap or click on a card: open it, or (Ctrl / Shift, or while selecting) select it.
    function cardActivated(model, index, modifiers) {
        if (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
            model.select(index, modifiers)
        } else if (model.selectionCount > 0) {
            model.toggleSelected(index)
        } else if (model === app.library) {
            openLibraryRow(index)
        } else {
            openRecentRow(index)
        }
    }
    /// Open documents (folders among them are left out; a single folder is entered).
    function openAll(paths, model) {
        const docs = app.library.documentsIn(paths)
        if (docs.length === 0 && paths.length === 1 && model === app.recent) {
            openLibraryFolder(paths[0])  // (a library of the Recent grid)
        } else if (docs.length === 0 && paths.length === 1 && model === app.library) {
            app.library.folder = app.library.relativeFolder(paths[0])
        } else if (docs.length > 0) {
            if (model === app.library && home.searching && docs.length === 1) app.openSearchHit(docs[0], lib.searchQuery)
            else app.openListed(docs)
        }
        model.clearSelection()
    }
    /// Short texts (fewer than 4 characters: hits everywhere) are searched on Enter or the search icon only.
    function typed() {
        if (searchGroup.searchField.text === "" || searchGroup.searchField.text.length >= 4) searchGroup.searchTyping.restart()
        else searchGroup.searchTyping.stop()
    }
    /// Imports into the Downloads folder (a short-lived place) are confirmed first.
    function confirmImport(targetIsTemporary, action) {
        if (!targetIsTemporary) {
            action()
            return
        }
        importDialogs.temporaryImportDialog.action = action
        importDialogs.temporaryImportDialog.open()
    }
    function countText(n) { return n === 1 ? qsTr("1 item") : qsTr("%1 items").arg(n) }
    function askTransfer(paths, copy) {
        libraryDialogs.transferDialog.paths = paths
        libraryDialogs.transferDialog.copy = copy
        libraryDialogs.transferDialog.open()
    }
    function askTrash(model, paths) {
        home.menuTarget.model = model
        home.menuTarget.paths = paths
        libraryDialogs.trashDialog.open()
    }
    /// What the item menu, rename and trash work on: a row of the library or the recent list
    readonly property QtObject menuTarget: QtObject {
        property var model: null
        property int row: -1
        property string name: ""
        property string path: ""
        property bool folder: false
        /// The kind of the row ("notes", "pdf", "md", "image", "text", "other"; a folder: "")
        property string kind: ""
        /// What the menu applies to: the row, or all selected items if the row is one of them.
        property var paths: []
        readonly property bool many: paths.length > 1
    }
    function showMenu(model, row, name, path, isFolder, item, x, y, kind) {
        menuTarget.model = model; menuTarget.row = row; menuTarget.name = name; menuTarget.path = path
        menuTarget.folder = isFolder
        menuTarget.kind = kind || ""
        menuTarget.paths = model.pathsFor(row)
        itemMenu.openMenu(Qt.point(x, y), item)
    }
    /// At the start, after the other questions (Main.qml): the offer, once
    property bool librariesHomeOffered: false
    function offerLibrariesHomeAtStart() {
        if (!librariesHomeOffered && app.offerLibrariesHome) {
            librariesHomeOffered = true
            libraryDialogs.librariesHomeDialog.open()
        }
    }

    Connections {
        target: app.library
        function onError(text) { errorDialog.text = text; errorDialog.open() }
        // (Android: the window switched to another library, e.g. from the Recent grid: show it)
        function onLibraryChanged() { home.page = 0 }
        function onImported(count) {
            if (count > 0) importedNote.show(count === 1 ? qsTr("1 document imported") : qsTr("%1 documents imported").arg(count), false)
        }
    }
    Connections {
        target: app.recent
        function onError(text) { errorDialog.text = text; errorDialog.open() }
    }
    Connections {
        target: app
        function onFavouriteChanged() { home.favouriteRevision++ }
    }
    Connections {
        target: app
        function onStorageAccessNeeded(folder) { libraryDialogs.storageAccessDialog.ask(folder) }
        function onPickLibraryFolder() { home.pickLibraryFolder() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- while items are selected: what to do with them ---
        // (the whole row where it fits; else the count here and the actions in a bar at the bottom, as the file apps
        // of phones have them: selectionActions)
        HomeSelectionBar { id: selectionBar }

        // --- header: library / recent / bookmarks, search, actions (qt/docs/features/adaptive-layout.md) ---
        // (the ladder: every action a button of its own where there is room (expanded), else behind "+" and View; on
        // a phone "+" floats at the bottom, the switch has a row of its own when upright, and the breadcrumbs come
        // into this row when held sideways. A window too narrow even for that, e.g. a tiny one, scrolls it sideways)
        HomeHeader { id: homeHeader }
        // The search's own row
        Item {
            id: narrowSearchSlot
            visible: home.searchOwnRow && home.selectionCount === 0 && (home.page === 0 || home.page === 2) && app.library.available
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            Layout.preferredHeight: 48
        }

        // --- where we are in the library (crumbBar; on a phone held sideways in the header instead) ---
        Item {
            id: crumbRowSlot
            objectName: "crumbRow"
            visible: !home.shortLayout && home.page === 0 && app.library.available && crumbBar.needed
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: 44
        }

        // Android: the libraries are still in the app's own folder (Android deletes it with the app); tap: move them
        AbstractButton {
            id: librariesInAppNote
            objectName: "librariesInAppNote"
            visible: app.librariesInApp && !app.libraryMove.running
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 4
            implicitHeight: inAppLabel.implicitHeight + 12
            onClicked: libraryDialogs.librariesHomeDialog.open()
            background: Rectangle {
                radius: 8
                color: librariesInAppNote.pressed ? "#e8eaed" : "#f1f3f4"
            }
            contentItem: RowLayout {
                spacing: 8
                Image {
                    Layout.leftMargin: 12
                    source: app.iconUrl("xqt-library"); sourceSize.width: 16; sourceSize.height: 16
                    opacity: 0.7
                }
                Label {
                    id: inAppLabel
                    Layout.fillWidth: true
                    Layout.rightMargin: 12
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: "#5f6368"
                    text: qsTr("Libraries are inside the app and are deleted when it is uninstalled") + "  ·  "
                          + "<font color=\"" + Material.accentColor + "\">" + qsTr("Keep them on the phone…") + "</font>"
                    textFormat: Text.StyledText
                }
            }
        }

        // The Downloads folder as library: a hint that its files are short-lived
        Rectangle {
            objectName: "temporaryBanner"
            visible: home.page === 0 && app.library.temporary
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: bannerLabel.implicitHeight + 16
            radius: 8
            color: "#fff4e5"
            border.width: 1
            border.color: "#ffcc80"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Image { source: app.iconUrl("xqt-download"); sourceSize.width: 18; sourceSize.height: 18 }
                Label {
                    id: bannerLabel
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#6d4c00"
                    text: qsTr("This is your Downloads folder: a quick look at downloaded papers. Its files are often "
                               + "cleaned up; copy documents you want to keep into a library (menu of a card → Copy to…).")
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: home.page

            // --- library ---
            LibraryGridPage { id: libraryPage }

            // --- recent documents ---
            RecentGridPage { id: recentPage }

            // --- the library's bookmarks ---
            BookmarksView {
                id: bookmarksView
                shown: home.visible && home.page === 2
                bottomSpace: home.fabSpace
            }

            // --- the library's to-dos ---
            TodosView {
                id: todosView
                shown: home.visible && home.page === 3
                bottomSpace: home.fabSpace
            }

            // --- the library's tags: a tap shows the documents with the tag ---
            TagsView {
                id: tagsView
                shown: home.visible && home.page === 4
                bottomSpace: home.fabSpace
                onTagChosen: home.page = 0
            }
        }

        // --- the actions on a selection at the bottom (a phone, or no room in the selection bar) ---
        // Open, Copy, Move and Trash, and ⋮ for the rest, as the file apps of phones have them
        HomeSelectionActions { id: selectionActions }
    }

    // The switch in the header: the library's name (its page) with ▾ (the libraries), Recent, Favourites (★),
    // Bookmarks
    LibrarySwitch { id: switchBox }

    // --- where we are in the library: the breadcrumbs, or what the list shows (search, flat, favourites) ---
    // The breadcrumbs elide from the middle: the library, "…" (a menu of the folders left out) and as many of the last
    // folders as fit ("Library › … › Quantum mechanics › Exercise sheets"); they never widen the page.
    LibraryCrumbs { id: crumbBar }

    // A phone: "+" (new document, import, new folder) floats at the bottom right, in reach of the thumb
    RoundButton {
        id: addFab
        objectName: "newDocumentFab"
        visible: home.phoneLayout && home.selectionCount === 0 && !moveDrag.active
        z: 25
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 16
        anchors.bottomMargin: 16 + home.safeBottom
        width: 56
        height: 56
        highlighted: true
        Material.elevation: 6
        icon.source: app.iconUrl("xqt-plus")
        icon.color: "#ffffff"
        icon.width: 26
        icon.height: 26
        display: AbstractButton.IconOnly
        readonly property string tip: homeHeader.newButton.tip
        Accessible.name: tip
        onClicked: Popups.openAt(homeHeader.newMenu)
        // (a finger held on it says what it is)
        property bool heldTip: false
        onPressAndHold: heldTip = true
        onReleased: heldTip = false
        onCanceled: heldTip = false
        ToolTip.visible: heldTip || (hovered && !home.touch)
        ToolTip.text: tip
        ToolTip.delay: heldTip ? 0 : 600
    }

    // Search in the whole library, and the extended search: in the header, or in a row of its own
    LibrarySearchField { id: searchGroup }

    // --- moving documents and folders: drag onto a folder or a breadcrumb ---
    MoveDragOverlay { id: moveDrag }

    NewDocumentDialog { id: newDocumentDialog }

    TagsDialog { id: homeTagsDialog }

    LibraryItemMenu { id: itemMenu; target: home.menuTarget }

    LibraryDialogs { id: libraryDialogs }

    // Export library as archive: what it does, the whole library or this folder, then a folder outside the library
    LibraryArchive { id: archiveDialogs }

    LibraryImport { id: importDialogs }

    AdaptiveDialog {
        id: errorDialog
        kind: "card"
        property alias text: errorLabel.text
        title: qsTr("That did not work")
        preferredWidth: 520
        standardButtons: Dialog.Ok
        Label { id: errorLabel; width: errorDialog.availableWidth; wrapMode: Text.Wrap }
    }

    Snackbar {
        id: importedNote
        z: 30
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: (addFab.visible ? 88 : 32) + home.safeBottom
    }
}
