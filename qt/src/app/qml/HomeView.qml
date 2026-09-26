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

    // --- the layout for the window's size (qt/docs/adaptive-layout.md, "The home screen and the tab overview") ---
    /// The window's layout (Main.qml's `win.adaptive`), if there is one
    readonly property var adaptive: typeof win !== "undefined" && win ? win.adaptive : null
    readonly property string layoutClass: adaptive ? adaptive.layoutClass : "desktopWide"
    /// A phone class: the "+" floats at the bottom, the actions on a selection are a bar at the bottom
    readonly property bool phoneLayout: ["phonePortrait", "phoneShort", "tiny"].indexOf(layoutClass) >= 0
    /// A phone held sideways (or another short window of a phone class): one header row with the breadcrumbs in it,
    /// shorter cards
    readonly property bool shortLayout: phoneLayout && (layoutClass === "phoneShort"
                                                        || (adaptive !== null && adaptive.orientation === "landscape"))
    /// A phone held upright: the switch Library / Recent / Bookmarks in a row of its own, across the width
    readonly property bool portraitPhone: phoneLayout && !shortLayout
    readonly property bool touch: adaptive !== null && adaptive.touchProfile
    /// What a finger needs (48 with the touch profile, else 40)
    readonly property int minTarget: adaptive ? adaptive.minTarget : 40
    readonly property real safeBottom: typeof win !== "undefined" && win && win.safeBottom ? win.safeBottom : 0
    /// Room below the last cards for the floating "+" of a phone
    readonly property real fabSpace: phoneLayout ? 72 : 0

    // The header's ladder: every action a button of its own where the row has room for all of them with the search
    // at its smallest ("expanded", a wide desktop window); else New and Import behind "+" and the ways to show the
    // cards behind "View" ("grouped"). Only one of the two is there at a time: no action in two places.
    /// The row's width (16 px margin at the left, 8 at the right)
    readonly property real headerRoom: width - 24
    readonly property real searchMinimum: 150 + 6 + 48
    /// The switch as the header shows it (the words of Bookmarks counted: the need does not change with the page)
    readonly property real switchNeed: libraryTab.implicitWidth + recentTab.implicitWidth + 52 + bookmarksWord.advanceWidth + 12
    TextMetrics { id: bookmarksWord; text: qsTr("Bookmarks") }
    /// What the expanded row needs: the switch, the libraries' ▾, the search at its smallest, New, Last page, Import,
    /// New folder, Flat, Favourites, Show, Sort, − and + with their separators, Settings, and the spacing between them
    readonly property real expandedNeed: switchNeed + 40 + searchMinimum + 48 + resume.implicitWidth + 5 * 48 + 40
                                         + 2 * 40 + 2 * 13 + 48 + 16 * 6
    readonly property bool expanded: !phoneLayout && headerRoom >= expandedNeed
    /// Room for the words beside the icons of the Bookmarks tab and the Favourites chip (else icons with tips), and a
    /// search at its full width
    readonly property bool roomy: expanded && headerRoom >= expandedNeed + 230 + 180
    /// The grouped row: the switch, ▾, the search at its smallest, "+", View and Settings
    readonly property real groupedNeed: switchNeed + 40 + searchMinimum + 3 * 48 + 8 * 6
    /// The search in a row of its own: on a phone upright, and in a window too narrow for it in the header
    readonly property bool searchOwnRow: portraitPhone || (!phoneLayout && !expanded && headerRoom < groupedNeed)
    /// The actions on a selection in a bar at the bottom (Open, Copy, Move, Trash, ⋮): on a phone, and where the
    /// selection bar's row does not fit
    readonly property bool selectionAtBottom: phoneLayout || selectionFull.implicitWidth + selectionLabel.implicitWidth + 96 > width
    /// 0: library, 1: recent documents, 2: the library's bookmarks (qt/docs/bookmarks.md)
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
        const grid = page === 0 ? libraryGrid : recentGrid
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
    function focusGrid() { (page === 0 ? libraryGrid : page === 2 ? bookmarksView : recentGrid).forceActiveFocus() }
    function focusSearch() {
        page = 0
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    /// Search the library for this text, right away (selected text: the look-up menu)
    function searchFor(text) {
        page = 0
        searchTyping.stop()
        searchField.text = text
        lib.searchQuery = text
        libraryGrid.forceActiveFocus()
    }
    onVisibleChanged: if (visible) focusGrid()
    onPageChanged: focusGrid()
    function openLibraryRow(index) {
        const item = libraryGrid.itemAtIndex(index)
        if (!item) return
        if (item.isFolder) {
            searchTyping.stop()
            searchField.text = ""  // a folder found by the search: show it
            lib.searchQuery = ""
            lib.folder = lib.relativeFolder(item.path)
        } else if (home.searching) {
            app.openSearchHit(item.path, lib.searchQuery)
        } else {
            app.openListed([item.path])  // (other files: with their app)
        }
    }
    function openRecentRow(index) {
        const item = recentGrid.itemAtIndex(index)
        if (item && item.isLibrary) openLibraryFolder(item.path)
        else if (item) app.openListed([item.path])
    }
    /// "Open a folder as library…": the folder picker. On Android a folder of the phone's storage can be a library
    /// only with "All files access": explained and asked for first (then the picker opens, see onPickLibraryFolder).
    /// With it, Android's picker is replaced by the app's own folder list (the picker refuses the Download folder).
    function pickLibraryFolder() {
        if (!app.storageAccess) storageAccessDialog.ask("")
        else if (app.inAppFolderChooser) folderChooser.openAt(app.storageRoot)
        else openLibraryDialog.open()
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
        if (searchField.text === "" || searchField.text.length >= 4) searchTyping.restart()
        else searchTyping.stop()
    }
    /// Imports into the Downloads folder (a short-lived place) are confirmed first.
    function confirmImport(targetIsTemporary, action) {
        if (!targetIsTemporary) {
            action()
            return
        }
        temporaryImportDialog.action = action
        temporaryImportDialog.open()
    }
    function countText(n) { return n === 1 ? qsTr("1 item") : qsTr("%1 items").arg(n) }
    function askTransfer(paths, copy) {
        transferDialog.paths = paths
        transferDialog.copy = copy
        transferDialog.open()
    }
    function askTrash(model, paths) {
        home.menuModel = model
        home.menuPaths = paths
        trashDialog.open()
    }
    // Menu, rename and trash work on a row of the library or the recent list.
    property var menuModel: null
    property int menuRow: -1
    property string menuName: ""
    property string menuPath: ""
    property bool menuFolder: false
    /// The kind of the row ("notes", "pdf", "md", "image", "text", "other"; a folder: "")
    property string menuKind: ""
    /// What the menu applies to: the row, or all selected items if the row is one of them.
    property var menuPaths: []
    readonly property bool menuMany: menuPaths.length > 1
    function showMenu(model, row, name, path, isFolder, item, x, y, kind) {
        menuModel = model; menuRow = row; menuName = name; menuPath = path; menuFolder = isFolder
        menuKind = kind || ""
        menuPaths = model.pathsFor(row)
        itemMenu.openMenu(Qt.point(x, y), item)
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
        function onStorageAccessNeeded(folder) { storageAccessDialog.ask(folder) }
        function onPickLibraryFolder() { home.pickLibraryFolder() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- while items are selected: what to do with them ---
        // (the whole row where it fits; else the count here and the actions in a bar at the bottom, as the file apps
        // of phones have them: selectionActions)
        Rectangle {
            id: selectionBar
            objectName: "homeSelectionBar"
            visible: home.selectionCount > 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: home.shortLayout ? 4 : 10
            Layout.preferredHeight: 48
            radius: 24
            color: "#e8eaf6"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 8
                spacing: 2
                IconButton {
                    objectName: "clearSelectionButton"
                    iconName: "xqt-close"
                    tip: qsTr("Clear the selection (Esc)")
                    onClicked: home.currentModel.clearSelection()
                }
                Label {
                    id: selectionLabel
                    objectName: "selectionLabel"
                    text: qsTr("%1 selected").arg(home.selectionCount)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#283593"
                    elide: Text.ElideRight
                    Layout.leftMargin: 4
                    Layout.fillWidth: home.selectionAtBottom
                }
                // (measured also while it is hidden: whether it fits decides where the actions are)
                RowLayout {
                    id: selectionFull
                    visible: !home.selectionAtBottom
                    Layout.fillWidth: true
                    spacing: 2
                    Button { objectName: "selectAllButton"; text: qsTr("Select all"); flat: true; onClicked: home.currentModel.selectAll() }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "openSelectedButton"
                        text: qsTr("Open")
                        flat: true
                        icon.source: app.iconUrl("xopp-document-open")
                        onClicked: home.openAll(home.currentModel.selectedPaths(), home.currentModel)
                    }
                    Button {
                        objectName: "copySelectedButton"
                        text: qsTr("Copy to…")
                        flat: true
                        enabled: app.library.available
                        icon.source: app.iconUrl("xopp-edit-copy")
                        onClicked: home.askTransfer(home.currentModel.selectedPaths(), true)
                    }
                    Button {
                        objectName: "moveSelectedButton"
                        text: qsTr("Move to…")
                        flat: true
                        enabled: app.library.available
                        icon.source: app.iconUrl("xqt-folder-input")
                        onClicked: home.askTransfer(home.currentModel.selectedPaths(), false)
                    }
                    Button {
                        visible: home.page === 1
                        text: qsTr("Remove from list")
                        flat: true
                        onClicked: app.recent.removePaths(app.recent.selectedPaths())
                    }
                    Button {
                        objectName: "trashSelectedButton"
                        text: qsTr("Trash…")
                        flat: true
                        icon.source: app.iconUrl("xqt-delete")
                        onClicked: home.askTrash(home.currentModel, home.currentModel.selectedPaths())
                    }
                }
            }
        }

        // --- header: library / recent / bookmarks, search, actions (qt/docs/adaptive-layout.md) ---
        // (the ladder: every action a button of its own where there is room (expanded), else behind "+" and View; on
        // a phone "+" floats at the bottom, the switch has a row of its own when upright, and the breadcrumbs come
        // into this row when held sideways. A window too narrow even for that, e.g. a tiny one, scrolls it sideways)
        Flickable {
            id: headerFlick
            objectName: "homeHeader"
            visible: home.selectionCount === 0
            Layout.fillWidth: true
            Layout.topMargin: home.shortLayout ? 4 : 10
            Layout.preferredHeight: headerRow.implicitHeight
            contentWidth: headerRow.width + 24
            contentHeight: headerRow.implicitHeight
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width + 1
            clip: true
        RowLayout {
            id: headerRow
            x: 16
            // (the search field and the breadcrumbs give way down to their minimum before the row scrolls)
            width: Math.max(headerFlick.width - 24, implicitWidth
                            - (searchSlot.visible ? searchSlot.Layout.preferredWidth - searchSlot.Layout.minimumWidth : 0)
                            - (headerCrumbSlot.visible ? headerCrumbSlot.Layout.preferredWidth - headerCrumbSlot.Layout.minimumWidth : 0))
            height: headerFlick.height
            spacing: 6

            // The switch Library / Recent / Bookmarks (switchBox): here, or in a row of its own on a phone upright
            Item {
                id: switchSlot
                visible: !home.portraitPhone
                Layout.preferredWidth: switchBox.implicitWidth
                Layout.preferredHeight: switchBox.implicitHeight
            }
            // The libraries: a ▾ beside the switch; on a phone upright the header's title, the library's name
            ToolButton {
                id: libraryMenuButton
                objectName: "libraryMenuButton"
                readonly property bool titled: home.portraitPhone
                readonly property string tip: qsTr("Libraries")
                Layout.fillWidth: titled
                implicitWidth: titled ? libraryTitleRow.implicitWidth + leftPadding + rightPadding : 40
                implicitHeight: 48
                leftPadding: titled ? 8 : 4
                rightPadding: titled ? 8 : 4
                Accessible.name: tip
                onClicked: Popups.openAt(libraryMenu)
                // (a finger held on it says what it is, as the other buttons: qt/docs/adaptive-layout.md)
                property bool heldTip: false
                onPressAndHold: heldTip = true
                onReleased: heldTip = false
                onCanceled: heldTip = false
                ToolTip.visible: heldTip || (hovered && !titled)
                ToolTip.text: tip
                ToolTip.delay: heldTip ? 0 : 600
                background: Rectangle {
                    radius: 10
                    color: libraryMenuButton.pressed ? "#e8e8e8" : "transparent"
                }
                contentItem: Item {
                    RowLayout {
                        id: libraryTitleRow
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: libraryMenuButton.titled ? undefined : parent.horizontalCenter
                        width: libraryMenuButton.titled ? Math.min(implicitWidth, parent.width) : implicitWidth
                        spacing: 8
                        Image {
                            visible: libraryMenuButton.titled
                            source: app.iconUrl("xqt-library")
                            sourceSize.width: 20
                            sourceSize.height: 20
                        }
                        Label {
                            objectName: "libraryTitle"
                            visible: libraryMenuButton.titled
                            Layout.fillWidth: true
                            text: app.library.available ? app.library.name : qsTr("Libraries")
                            elide: Text.ElideRight
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            color: "#202124"
                        }
                        Image {
                            source: app.iconUrl("xqt-chevron-down")
                            sourceSize.width: 22
                            sourceSize.height: 22
                        }
                    }
                }
                // The libraries: this window shows one (highlighted); another one opens in a new window.
                AdaptiveMenu {
                    id: libraryMenu
                    objectName: "libraryMenu"
                    minimumWidth: 300
                    property var libraries: []
                    onAboutToShow: libraries = app.libraries()
                    Label {
                        text: app.libraryWindows ? qsTr("Libraries (another one opens in a new window)")
                                                 : qsTr("Libraries (the window switches to another one)")
                        leftPadding: 16
                        rightPadding: 16
                        topPadding: 8
                        bottomPadding: 4
                        width: libraryMenu.width
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: "#6b6f75"
                    }
                    Instantiator {
                        id: libraryList
                        model: libraryMenu.libraries
                        delegate: AdaptiveMenuItem {
                            id: libraryItem
                            objectName: "libraryMenuEntry"
                            required property var modelData
                            readonly property bool current: modelData.current
                            readonly property string label: modelData.downloads ? qsTr("Downloads folder (quick library)") : modelData.name
                            text: current ? qsTr("%1 — this window").arg(label) : label
                            font.weight: current ? Font.DemiBold : Font.Normal
                            icon.source: app.iconUrl(modelData.downloads ? "xqt-download" : "xqt-library")
                            icon.color: current ? Material.accentColor : "#566d86"
                            background: Rectangle {
                                color: libraryItem.current ? "#e8eaf6" : (libraryItem.highlighted ? "#f1f3f4" : "transparent")
                            }
                            // This library: just the home screen; another: a new window (one library per window)
                            // (a path, not "file://" + path: on Windows that makes the drive letter a host)
                            onTriggered: current ? (app.homeVisible = true) : app.openLibraryAt(modelData.path)
                        }
                        // after the heading
                        onObjectAdded: function(index, object) { libraryMenu.insertItem(index + 1, object) }
                        onObjectRemoved: function(index, object) { libraryMenu.removeItem(object) }
                    }
                    MenuSeparator {}
                    AdaptiveMenuItem {
                        text: app.libraryWindows ? qsTr("New library… (new window)") : qsTr("New library…")
                        onTriggered: newLibraryDialog.open()
                    }
                    AdaptiveMenuItem {
                        objectName: "openFolderAsLibraryItem"
                        text: app.libraryWindows ? qsTr("Open a folder as library… (new window)") : qsTr("Open a folder as library…")
                        onTriggered: home.pickLibraryFolder()
                    }
                    // (not on Android: there is no file manager the app could show a folder in reliably)
                    AdaptiveMenuItem {
                        objectName: "libraryShowInFileManagerItem"
                        text: qsTr("Show in file manager")
                        enabled: app.library.available
                        offered: app.canShowInFileManager
                        onTriggered: app.showInFileManager(app.library.rootPath)
                    }
                    AdaptiveMenuItem {
                        objectName: "exportLibraryArchiveItem"
                        text: qsTr("Export library as archive…")
                        enabled: app.library.available && !app.libraryArchive.running
                        onTriggered: libraryArchiveDialog.open()
                    }
                }
            }
            // Where we are in the library (crumbBar): here on a phone held sideways, else in a row of its own below
            Item {
                id: headerCrumbSlot
                visible: home.shortLayout && home.page === 0 && app.library.available
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                Layout.preferredWidth: 320
                Layout.preferredHeight: 44
            }

            Item { Layout.fillWidth: true; visible: !headerCrumbSlot.visible }

            // Search in the whole library (searchGroup, below): here, or in a row of its own
            Item {
                id: searchSlot
                visible: !home.searchOwnRow && (home.page === 0 || home.page === 2) && app.library.available
                // As wide as there is room for, up to 380 and the button (the buttons of the row come first)
                Layout.fillWidth: true
                Layout.minimumWidth: home.searchMinimum
                Layout.maximumWidth: 380 + 6 + 48
                Layout.preferredWidth: home.shortLayout ? 240 + 6 + 48 : 380 + 6 + 48
                Layout.preferredHeight: 48
            }

            Item { Layout.fillWidth: true; visible: !headerCrumbSlot.visible }

            // --- expanded: every action a button of its own ---
            // The same as in the settings: open a document at the page where it was left (a small toggle)
            ToolButton {
                id: resume
                objectName: "resumeSwitch"
                visible: home.expanded
                text: qsTr("Last page")
                icon.source: app.iconUrl("xqt-history")
                icon.width: 18
                icon.height: 18
                checkable: true
                checked: (app.settings.revision, app.settings.get("resumeAtLastPage"))
                onToggled: app.settings.set("resumeAtLastPage", checked)
                implicitHeight: 40
                font.pixelSize: 13
                font.weight: checked ? Font.DemiBold : Font.Normal
                Material.foreground: checked ? Material.accentColor : "#5f6368"
                icon.color: checked ? Material.accentColor : "#5f6368"
                ToolTip.visible: hovered
                ToolTip.text: checked ? qsTr("Documents open where they were left off - tap: at their first page")
                                      : qsTr("Open documents where they were left off")
                ToolTip.delay: 600
                background: Rectangle {
                    radius: 10
                    color: resume.checked ? "#e0e3f5" : (resume.pressed ? "#e8e8e8" : "transparent")
                }
            }

            // New: a document of notes, a Markdown file or a text file (in the current folder). Grouped ("+"): also
            // Import, New folder and (Recent) Open a file. On a phone the floating "+" (newDocumentFab) opens it.
            IconButton {
                id: newButton
                objectName: "newDocumentButton"
                visible: !home.phoneLayout
                iconName: home.expanded ? "xqt-file-plus" : "xqt-plus"
                tip: home.expanded ? (app.newTextAsPdf ? qsTr("New document, text document or text file")
                                                       : qsTr("New document, Markdown file or text file"))
                                   : qsTr("New document, import, new folder")
                onClicked: Popups.openAt(newMenu)
                AdaptiveMenu {
                    id: newMenu
                    objectName: "newMenu"
                    title: qsTr("New")
                    /// Import, New folder and Open a file are here when they have no button of their own
                    readonly property bool grouped: !home.expanded
                    AdaptiveMenuItem {
                        objectName: "newDocumentItem"
                        text: qsTr("New document…")
                        icon.source: app.iconUrl("xqt-notebook-pen")
                        onTriggered: newDocumentDialog.open()
                    }
                    // A text document: a PDF text document or a Markdown file, as Settings → Documents says
                    // (qt/docs/md-pdf.md)
                    AdaptiveMenuItem {
                        objectName: "newMarkdownItem"
                        text: app.newTextAsPdf ? qsTr("New text document…") : qsTr("New Markdown file…")
                        icon.source: app.iconUrl("xqt-markdown")
                        enabled: app.library.available
                        onTriggered: { textFileDialog.extension = app.newTextAsPdf ? ".pdf" : ".md"; textFileDialog.open() }
                    }
                    AdaptiveMenuItem {
                        objectName: "newTextItem"
                        text: qsTr("New text file…")
                        icon.source: app.iconUrl("xqt-file-text")
                        enabled: app.library.available
                        onTriggered: { textFileDialog.extension = ".txt"; textFileDialog.open() }
                    }
                    MenuSeparator {
                        property bool offered: newMenu.grouped && (home.page === 0 && app.library.available || home.page === 1)
                        visible: offered
                        height: offered ? implicitHeight : 0
                    }
                    AdaptiveMenuItem {
                        objectName: "addImportFilesItem"
                        text: qsTr("Import files…")
                        icon.source: app.iconUrl("xqt-import")
                        offered: newMenu.grouped && home.page === 0 && app.library.available
                        onTriggered: importDialog.open()
                    }
                    AdaptiveMenuItem {
                        objectName: "addImportFolderItem"
                        text: qsTr("Import a folder with its subfolders…")
                        icon.source: app.iconUrl("xqt-folder-input")
                        offered: newMenu.grouped && home.page === 0 && app.library.available
                        onTriggered: importFolderDialog.open()
                    }
                    AdaptiveMenuItem {
                        objectName: "addNewFolderItem"
                        text: qsTr("New folder…")
                        icon.source: app.iconUrl("xqt-folder-plus")
                        offered: newMenu.grouped && home.page === 0 && app.library.available
                        enabled: !app.library.flat && !home.searching
                        onTriggered: { folderNameDialog.row = -1; folderNameDialog.open() }
                    }
                    AdaptiveMenuItem {
                        objectName: "addOpenFileItem"
                        text: qsTr("Open a file…")
                        icon.source: app.iconUrl("xopp-document-open")
                        offered: newMenu.grouped && home.page === 1
                        onTriggered: home.openFileRequested()
                    }
                }
            }
            IconButton {
                objectName: "importButton"
                visible: home.expanded && home.page === 0 && app.library.available
                iconName: "xqt-import"
                tip: qsTr("Import PDFs and Xournal files, or a whole folder (copies them into this folder)")
                onClicked: Popups.openAt(importMenu)
                AdaptiveMenu {
                    id: importMenu
                    objectName: "importMenu"
                    AdaptiveMenuItem { objectName: "importFilesItem"; text: qsTr("Import files…"); onTriggered: importDialog.open() }
                    AdaptiveMenuItem { objectName: "importFolderItem"; text: qsTr("Import a folder with its subfolders…"); onTriggered: importFolderDialog.open() }
                }
            }
            IconButton {
                objectName: "newFolderButton"
                visible: home.expanded && home.page === 0 && app.library.available
                enabled: !app.library.flat && !home.searching
                iconName: "xqt-folder-plus"
                tip: qsTr("New folder")
                onClicked: { folderNameDialog.row = -1; folderNameDialog.open() }
            }
            IconButton {
                objectName: "flatButton"
                visible: home.expanded && home.page === 0 && app.library.available
                iconName: app.library.flat ? "xqt-layout-grid" : "xqt-folder-tree"
                tip: app.library.flat ? qsTr("All documents (show folders)") : qsTr("Folders (show all documents at once)")
                checked: app.library.flat
                onClicked: app.library.flat = !app.library.flat
            }
            // Only the favourites (starred documents of the whole library): a chip of its own, combined with the kinds
            // shown and the search; the Bookmarks view follows it too
            ToolButton {
                id: favouritesChip
                objectName: "favouritesChip"
                visible: home.expanded && (home.page === 0 || home.page === 2) && app.library.available
                text: qsTr("Favourites")
                display: home.roomy ? AbstractButton.TextBesideIcon : AbstractButton.IconOnly
                implicitWidth: home.roomy ? implicitContentWidth + leftPadding + rightPadding : 40
                Accessible.name: text
                icon.source: app.iconUrl(checked ? "xqt-star-filled" : "xqt-star")
                icon.color: "transparent"
                icon.width: 18
                icon.height: 18
                checkable: true
                checked: app.library.favouritesOnly
                onToggled: app.library.favouritesOnly = checked
                implicitHeight: 40
                font.pixelSize: 13
                font.weight: checked ? Font.DemiBold : Font.Normal
                Material.foreground: checked ? "#8a5a00" : "#5f6368"
                ToolTip.visible: hovered
                ToolTip.text: checked ? qsTr("Only favourites are shown - tap: all documents")
                                      : qsTr("Show only favourites (starred documents)")
                ToolTip.delay: 600
                background: Rectangle {
                    radius: 10
                    color: favouritesChip.checked ? "#fdf1d0" : (favouritesChip.pressed ? "#e8e8e8" : "transparent")
                    border.width: favouritesChip.checked ? 1 : 0
                    border.color: "#f4b400"
                }
            }
            // Which kinds of files the library shows (a setting of the library), marked when not the default
            IconButton {
                id: showButton
                objectName: "showButton"
                visible: home.expanded && (home.page === 0 || home.page === 2) && app.library.available
                iconName: "xqt-filter"
                tip: app.library.showFiltered ? qsTr("Show: some kinds of files are hidden or added") : qsTr("Show: which kinds of files")
                checked: app.library.showFiltered
                onClicked: Popups.openAt(showPopup)
                ShowMenu { id: showPopup; objectName: "showPopup"; prefix: "show"; heading: true }
            }
            IconButton {
                objectName: "sortButton"
                visible: home.expanded && home.page === 0 && app.library.available
                iconName: "xqt-sort"
                tip: qsTr("Sort")
                onClicked: Popups.openAt(sortMenu)
                SortMenu { id: sortMenu; objectName: "sortMenu"; prefix: "sort" }
            }
            IconButton {
                objectName: "openFileButton"
                visible: home.expanded && home.page === 1
                iconName: "xopp-document-open"
                tip: qsTr("Open a file")
                onClicked: home.openFileRequested()
            }
            ToolSeparator { visible: home.expanded }
            ToolButton {
                objectName: "zoomOutButton"
                visible: home.expanded
                text: "−"
                font.pixelSize: 22
                implicitWidth: 40
                enabled: home.page !== 2 && (home.page === 0 ? libraryGrid : recentGrid).columns < 12
                onClicked: home.zoom(-1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Smaller cells (Ctrl+wheel, pinch)")
                ToolTip.delay: 600
            }
            ToolButton {
                objectName: "zoomInButton"
                visible: home.expanded
                text: "+"
                font.pixelSize: 22
                implicitWidth: 40
                enabled: home.page !== 2 && (home.page === 0 ? libraryGrid : recentGrid).columns > home.fewestColumns(home.extendedView && home.page === 0)
                onClicked: home.zoom(1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Bigger cells (Ctrl+wheel, pinch)")
                ToolTip.delay: 600
            }
            ToolSeparator { visible: home.expanded }

            // --- grouped: how the cards are shown, behind one button ---
            IconButton {
                id: viewButton
                objectName: "homeViewButton"
                visible: !home.expanded
                iconName: "xqt-sliders"
                tip: qsTr("View: favourites, which files, sorting, size of the cards")
                // (marked while it shows less than everything)
                checked: app.library.available && home.page !== 1
                         && (app.library.favouritesOnly || app.library.showFiltered || (home.page === 0 && app.library.flat))
                onClicked: Popups.openAt(viewMenu)
                AdaptiveMenu {
                    id: viewMenu
                    objectName: "homeViewMenu"
                    title: qsTr("View")
                    AdaptiveMenuItem {
                        objectName: "viewFavouritesItem"
                        text: qsTr("Only favourites")
                        offered: home.page !== 1 && app.library.available
                        checkable: true
                        checked: app.library.favouritesOnly
                        onTriggered: app.library.favouritesOnly = checked
                    }
                    AdaptiveMenuItem {
                        objectName: "viewFlatItem"
                        text: qsTr("All documents at once (no folders)")
                        offered: home.page === 0 && app.library.available
                        checkable: true
                        checked: app.library.flat
                        onTriggered: app.library.flat = checked
                    }
                    ShowMenu {
                        objectName: "viewShowMenu"
                        prefix: "viewShow"
                        offered: home.page !== 1 && app.library.available
                    }
                    SortMenu {
                        objectName: "viewSortMenu"
                        prefix: "viewSort"
                        title: qsTr("Sort")
                        offered: home.page === 0 && app.library.available
                    }
                    AdaptiveMenuItem {
                        objectName: "viewResumeItem"
                        text: qsTr("Open documents where they were left off")
                        checkable: true
                        checked: (app.settings.revision, app.settings.get("resumeAtLastPage"))
                        onTriggered: app.settings.set("resumeAtLastPage", checked)
                    }
                    MenuSeparator {
                        property bool offered: home.page !== 2
                        visible: offered
                        height: offered ? implicitHeight : 0
                    }
                    // The size of the cards: − and + (Ctrl+wheel and pinch too); the menu stays open
                    RowLayout {
                        objectName: "viewCardSizeRow"
                        property bool offered: home.page !== 2
                        visible: offered
                        height: offered ? implicitHeight : 0
                        width: parent ? parent.width : implicitWidth
                        readonly property var grid: home.page === 0 ? libraryGrid : recentGrid
                        Label { text: qsTr("Size of the cards"); Layout.leftMargin: 16; Layout.fillWidth: true }
                        ToolButton {
                            objectName: "viewZoomOutButton"
                            text: "−"
                            font.pixelSize: 20
                            implicitWidth: home.minTarget
                            implicitHeight: home.minTarget
                            enabled: parent.grid.columns < 12
                            onClicked: home.zoom(-1)
                            Accessible.name: qsTr("Smaller cards")
                        }
                        ToolButton {
                            objectName: "viewZoomInButton"
                            text: "+"
                            font.pixelSize: 20
                            implicitWidth: home.minTarget
                            implicitHeight: home.minTarget
                            Layout.rightMargin: 8
                            enabled: parent.grid.columns > home.fewestColumns(home.extendedView && home.page === 0)
                            onClicked: home.zoom(1)
                            Accessible.name: qsTr("Bigger cards")
                        }
                    }
                }
            }
            // (the tool bar with its menu is not there while the library is shown)
            IconButton {
                objectName: "homeSettingsButton"
                iconName: "xqt-settings"
                tip: qsTr("Settings (Ctrl+,)")
                onClicked: home.settingsRequested()
            }
        }
        }
        // The switch's own row on a phone held upright: across the width
        Item {
            id: switchRow
            visible: home.portraitPhone && home.selectionCount === 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            Layout.preferredHeight: switchBox.implicitHeight
        }
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
            visible: !home.shortLayout && home.page === 0 && app.library.available
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
            onClicked: librariesHomeDialog.open()
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
            Item {
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
                            renameDialog.open()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Delete && item) {
                            home.askTrash(app.library, [item.path])
                            event.accepted = true
                        } else if (event.text.length === 1 && event.text.charCodeAt(0) > 32 && event.text.charCodeAt(0) !== 127
                                   && !(event.modifiers & Qt.ControlModifier)) {
                            // Typing searches - visible characters only (Escape, Backspace, Delete have a text too)
                            searchField.forceActiveFocus()
                            searchField.text += event.text
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
                        preview: model.preview
                        hasPdf: model.hasPdf
                        lastRead: model.lastRead ? home.formatDate(model.lastRead) : ""
                        lastPage: model.lastPage
                        hasXopp: model.hasXopp
                        kind: model.kind
                        pdfKind: model.pdfKind
                        fileIcon: model.fileIcon
                        hits: model.hits
                        conflicts: model.conflicts ? model.conflicts.length : 0
                        onConflictsRequested: conflictDialog.show(model.path)
                        snippet: model.snippet
                        itemCount: model.itemCount
                        hitPages: home.extendedView ? model.hitPageList : []
                        hitPageBase: model.hitPageBase
                        hitPassages: home.extendedView ? model.hitPassageList : []
                        hitPassageBase: model.hitPassageBase
                        stripHeight: home.extendedView && !model.isFolder ? libraryGrid.stripHeight : 0
                        favourite: model.favourite
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
                              : app.library.folder !== "" ? qsTr("This folder is empty")
                              : qsTr("Your library is empty")
                    }
                    Label {
                        visible: !home.searching && !app.library.favouritesOnly
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: "#80868b"
                        text: qsTr("Drop PDFs and Xournal files here, import them, or create a new document.")
                    }
                    // (one below the other when they do not fit side by side)
                    GridLayout {
                        visible: !home.searching && !app.library.favouritesOnly
                        Layout.alignment: Qt.AlignHCenter
                        columns: emptyNew.implicitWidth + emptyImport.implicitWidth + emptyImportFolder.implicitWidth
                                 + 2 * columnSpacing <= parent.width ? 3 : 1
                        Button { id: emptyNew; objectName: "emptyNewDocument"; Layout.alignment: Qt.AlignHCenter; text: qsTr("New document"); highlighted: true; onClicked: newDocumentDialog.open() }
                        Button { id: emptyImport; objectName: "emptyImportFiles"; Layout.alignment: Qt.AlignHCenter; text: qsTr("Import files…"); flat: true; onClicked: importDialog.open() }
                        Button { id: emptyImportFolder; objectName: "emptyImportFolder"; Layout.alignment: Qt.AlignHCenter; text: qsTr("Import a folder…"); flat: true; onClicked: importFolderDialog.open() }
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
                        if (drop.hasUrls) {
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

            // --- recent documents ---
            Item {
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
                        preview: model.preview
                        isFolder: model.isLibrary
                        isLibrary: model.isLibrary
                        hasPdf: model.hasPdf
                        lastRead: model.isLibrary ? "" : home.formatDate(model.opened)
                        lastPage: model.lastPage
                        hasXopp: model.hasXopp
                        kind: model.kind
                        pdfKind: model.pdfKind
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

            // --- the library's bookmarks ---
            BookmarksView {
                id: bookmarksView
                shown: home.visible && home.page === 2
                bottomSpace: home.fabSpace
            }
        }

        // --- the actions on a selection at the bottom (a phone, or no room in the selection bar) ---
        // Open, Copy, Move and Trash, and ⋮ for the rest, as the file apps of phones have them
        Rectangle {
            id: selectionActions
            objectName: "selectionActionBar"
            visible: home.selectionCount > 0 && home.selectionAtBottom
            Layout.fillWidth: true
            Layout.preferredHeight: 60 + home.safeBottom
            color: "#ffffff"
            Rectangle { width: parent.width; height: 1; color: "#dadce0" }
            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                height: 60
                spacing: 0
                BarAction {
                    objectName: "selectionOpenAction"
                    iconName: "xopp-document-open"
                    text: qsTr("Open")
                    onClicked: home.openAll(home.currentModel.selectedPaths(), home.currentModel)
                }
                BarAction {
                    objectName: "selectionCopyAction"
                    iconName: "xopp-edit-copy"
                    text: qsTr("Copy to")
                    enabled: app.library.available
                    onClicked: home.askTransfer(home.currentModel.selectedPaths(), true)
                }
                BarAction {
                    objectName: "selectionMoveAction"
                    iconName: "xqt-folder-input"
                    text: qsTr("Move to")
                    enabled: app.library.available
                    onClicked: home.askTransfer(home.currentModel.selectedPaths(), false)
                }
                BarAction {
                    objectName: "selectionTrashAction"
                    iconName: "xqt-delete"
                    text: qsTr("Trash")
                    onClicked: home.askTrash(home.currentModel, home.currentModel.selectedPaths())
                }
                BarAction {
                    id: selectionMoreButton
                    objectName: "selectionMoreButton"
                    iconName: "xqt-more"
                    text: qsTr("More")
                    onClicked: Popups.openAt(selectionMenu)
                    AdaptiveMenu {
                        id: selectionMenu
                        objectName: "selectionMenu"
                        title: qsTr("%1 selected").arg(home.selectionCount)
                        AdaptiveMenuItem {
                            objectName: "selectAllItem"
                            text: qsTr("Select all")
                            onTriggered: home.currentModel.selectAll()
                        }
                        AdaptiveMenuItem {
                            objectName: "removeSelectedFromListItem"
                            text: qsTr("Remove from list")
                            offered: home.page === 1
                            onTriggered: app.recent.removePaths(app.recent.selectedPaths())
                        }
                    }
                }
            }
        }
    }

    // A button of the selection's bar at the bottom: its icon above its word
    component BarAction: AbstractButton {
        id: action
        property string iconName
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        implicitHeight: 56
        Accessible.name: text
        background: Rectangle {
            radius: 12
            color: action.pressed ? "#e8eaed" : "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 2
            opacity: action.enabled ? 1 : 0.4
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: app.iconUrl(action.iconName)
                sourceSize.width: 24
                sourceSize.height: 24
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: action.width - 4
                text: action.text
                elide: Text.ElideRight
                font.pixelSize: 12
                color: "#3c4043"
            }
        }
    }

    // A tab of the switch Library / Recent / Bookmarks: its icon and its word; only the icon (its word in a tip, and
    // while a finger is held on it) where room is short and it is not the one shown
    component PageTab: AbstractButton {
        id: tab
        property int pageIndex
        property string label
        property string iconName
        /// Only its icon while it is not shown and the header is short of room (the Bookmarks tab)
        property bool compact: false
        readonly property bool current: home.page === pageIndex
        readonly property bool iconOnly: !home.portraitPhone
                                         && (home.shortLayout ? !current || pageIndex === 0 : !current && compact && !home.roomy)
        Layout.fillWidth: home.portraitPhone
        Layout.preferredWidth: home.portraitPhone ? 100 : implicitWidth
        Layout.fillHeight: true
        implicitHeight: home.touch ? 44 : 40
        implicitWidth: iconOnly ? (home.touch ? 44 : 40) : tabRow.implicitWidth + 28
        onClicked: home.page = pageIndex
        property bool heldTip: false
        onPressAndHold: heldTip = true
        onReleased: heldTip = false
        onCanceled: heldTip = false
        ToolTip.visible: iconOnly && (hovered || heldTip)
        ToolTip.text: label
        ToolTip.delay: heldTip ? 0 : 600
        Accessible.name: label
        background: Rectangle {
            radius: height / 2
            color: tab.current ? "#ffffff" : "transparent"
        }
        contentItem: Item {
            RowLayout {
                id: tabRow
                anchors.centerIn: parent
                // (a narrow tab: its word elided)
                width: Math.min(implicitWidth, parent.width)
                spacing: 6
                Image { source: app.iconUrl(tab.iconName); sourceSize.width: 18; sourceSize.height: 18 }
                Label {
                    visible: !tab.iconOnly
                    text: tab.label
                    font.weight: tab.current ? Font.DemiBold : Font.Normal
                    color: tab.enabled ? "#202124" : "#9aa0a6"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.maximumWidth: 220
                }
            }
        }
    }

    // A kind of file shown or not (the library's "Show" setting)
    component ShowToggle: CheckDelegate {
        property string key
        Layout.fillWidth: true
        checked: app.library.show[key] === true
        onToggled: app.library.setShown(key, checked)
        font.pixelSize: 14
        topPadding: 6
        bottomPadding: 6
        implicitHeight: Math.max(home.minTarget, Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding)
    }
    // Which kinds of files the library shows (a setting of each library): switches that leave the menu open while they
    // are changed. The Show button's menu (expanded) and View → Show (grouped); on a phone a page of the sheet.
    component ShowMenu: AdaptiveMenu {
        id: showMenu
        /// The start of the objectNames of its switches ("show": showNotes, showPdfs, …, showDefaults)
        property string prefix: "show"
        /// Its title on top (the Show button's menu; as a submenu and in the sheet the title is the entry)
        property bool heading: false
        title: qsTr("Kinds of files shown")
        ColumnLayout {
            width: parent ? parent.width : implicitWidth
            spacing: 0
            Label {
                visible: showMenu.heading
                text: qsTr("Show in this library")
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: "#5f6368"
                Layout.leftMargin: 12
                Layout.topMargin: 4
                Layout.bottomMargin: 4
            }
            ShowToggle { objectName: showMenu.prefix + "Notes"; key: "notes"; text: qsTr("Notes (.xopp, .xoj)") }
            ShowToggle { objectName: showMenu.prefix + "Pdfs"; key: "pdfs"; text: qsTr("PDFs") }
            ShowToggle {
                objectName: showMenu.prefix + "OnlyPdfsWithNotes"
                key: "onlyPdfsWithNotes"
                text: qsTr("Only PDFs with notes")
                enabled: app.library.show.pdfs === true
                leftPadding: 40
                font.pixelSize: 13
            }
            ShowToggle {
                objectName: showMenu.prefix + "OnlyTextDocuments"
                key: "onlyTextDocuments"
                text: qsTr("Only PDF text documents")
                enabled: app.library.show.pdfs === true
                leftPadding: 40
                font.pixelSize: 13
            }
            ShowToggle { objectName: showMenu.prefix + "Markdown"; key: "markdown"; text: qsTr("Markdown (.md)") }
            ShowToggle { objectName: showMenu.prefix + "Images"; key: "images"; text: qsTr("Images") }
            ShowToggle { objectName: showMenu.prefix + "Text"; key: "text"; text: qsTr("Text and code (.txt, .tex, .py, …)") }
            ShowToggle { objectName: showMenu.prefix + "Other"; key: "other"; text: qsTr("All other files") }
            Button {
                objectName: showMenu.prefix + "Defaults"
                Layout.alignment: Qt.AlignRight
                Layout.rightMargin: 8
                flat: true
                text: qsTr("Defaults")
                enabled: app.library.showFiltered
                onClicked: app.library.resetShown()
            }
        }
    }
    // How the library is sorted (the Sort button's menu, and View → Sort)
    component SortMenu: AdaptiveMenu {
        id: sortChoices
        property string prefix: "sort"
        AdaptiveMenuItem {
            objectName: sortChoices.prefix + "ByName"
            text: qsTr("By name")
            checkable: true
            checked: app.library.sortBy === "name"
            onTriggered: app.library.sortBy = "name"
        }
        AdaptiveMenuItem {
            objectName: sortChoices.prefix + "ByModified"
            text: qsTr("Last modified first")
            checkable: true
            checked: app.library.sortBy === "modified"
            onTriggered: app.library.sortBy = "modified"
        }
        AdaptiveMenuItem {
            objectName: sortChoices.prefix + "ByRead"
            text: qsTr("Last read first")
            checkable: true
            checked: app.library.sortBy === "read"
            onTriggered: app.library.sortBy = "read"
        }
    }

    // The switch Library / Recent / Bookmarks: in the header, or across a row of its own on a phone held upright
    Rectangle {
        id: switchBox
        parent: home.portraitPhone ? switchRow : switchSlot
        anchors.fill: parent
        radius: height / 2
        color: "#e1e4e8"
        implicitWidth: switchTabs.implicitWidth + 8
        implicitHeight: switchTabs.implicitHeight + 6
        RowLayout {
            id: switchTabs
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            anchors.topMargin: 3
            anchors.bottomMargin: 3
            spacing: 2
            PageTab {
                id: libraryTab
                objectName: "libraryPageButton"
                pageIndex: 0
                // (on a phone upright the header's title has the library's name)
                label: app.library.available && !home.portraitPhone ? app.library.name : qsTr("Library")
                iconName: "xqt-library"
                enabled: app.library.available
            }
            PageTab {
                id: recentTab
                objectName: "recentPageButton"
                pageIndex: 1
                label: qsTr("Recent")
                iconName: "xqt-history"
            }
            PageTab {
                id: bookmarksTab
                objectName: "bookmarksPageButton"
                pageIndex: 2
                label: qsTr("Bookmarks")
                iconName: "xqt-bookmark"
                enabled: app.library.available
                compact: true
            }
        }
    }

    // --- where we are in the library: the breadcrumbs, or what the list shows (search, flat, favourites) ---
    // The breadcrumbs elide from the middle: the library, "…" (a menu of the folders left out) and as many of the last
    // folders as fit ("Library › … › Quantum mechanics › Exercise sheets"); they never widen the page.
    RowLayout {
        id: crumbBar
        parent: home.shortLayout ? headerCrumbSlot : crumbRowSlot
        anchors.fill: parent
        spacing: 2
        readonly property bool showCrumbs: !home.searching && !app.library.flat && !app.library.favouritesOnly

        IconButton {
            objectName: "folderUpButton"
            iconName: "xqt-arrow-up"
            tip: qsTr("Up (Backspace)")
            implicitWidth: home.touch ? 44 : 40
            implicitHeight: home.touch ? 44 : 40
            visible: crumbBar.showCrumbs
            enabled: app.library.folder !== ""
            onClicked: app.library.goUp()
        }
        Item {
            id: crumbArea
            objectName: "crumbArea"
            visible: crumbBar.showCrumbs
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            clip: true
            /// Which crumbs show: the library (unless even the last folder is short of room then), "…" for the
            /// folders left out, and the ones from `first` on; the widths of the library's and of the last one (elided
            /// when they do not fit)
            readonly property var plan: {
                const n = crumbRepeater.count, room = width
                const widths = []
                for (let i = 0; i < n; ++i) {
                    const c = crumbRepeater.itemAt(i)
                    widths.push(c ? c.naturalWidth : 0)
                }
                const total = widths.reduce(function(a, b) { return a + b }, 0)
                if (n <= 1 || total <= room)
                    return { first: 1, showRoot: true, rootWidth: n > 0 ? Math.min(widths[0], room) : 0,
                             lastWidth: n > 0 ? Math.min(widths[n - 1], room) : 0 }
                const ellipsis = crumbEllipsis.implicitWidth
                const last = widths[n - 1]
                // The last folder first (up to 60 % of the room, at least 120), then the library with what is left
                // (at most half the room), left out too when that is less than 56
                const lastWant = Math.min(last, Math.max(120, room * 0.6))
                const rootRoom = room - (n > 2 ? ellipsis : 0) - lastWant
                const showRoot = rootRoom >= 56
                const rootWidth = Math.min(widths[0], rootRoom, room * 0.5)
                let first = n - 1
                let used = (showRoot ? rootWidth : 0) + (n > 2 || !showRoot ? ellipsis : 0) + last
                while (first - 1 >= 1 && used + widths[first - 1] <= room) {
                    first--
                    used += widths[first]
                }
                const shownEllipsis = first > 1 || !showRoot
                let middle = 0
                for (let i = first; i < n - 1; ++i) middle += widths[i]
                const lastRoom = room - (showRoot ? rootWidth : 0) - (shownEllipsis ? ellipsis : 0) - middle
                return { first: first, showRoot: showRoot, rootWidth: rootWidth,
                         lastWidth: Math.max(40, Math.min(last, lastRoom)) }
            }
            /// The folders left out ("…"): [{name, folder}]
            readonly property var hidden: {
                const list = [], crumbs = app.library.breadcrumbs
                for (let i = plan.showRoot ? 1 : 0; i < plan.first && i < crumbs.length; ++i) list.push(crumbs[i])
                return list
            }
            Row {
                id: crumbRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                Repeater {
                    id: crumbRepeater
                    model: app.library.breadcrumbs
                    delegate: Row {
                        id: crumbItem
                        required property int index
                        required property var modelData
                        readonly property string folder: modelData.folder
                        readonly property bool last: index === crumbRepeater.count - 1
                        /// Its width with its whole name
                        readonly property real naturalWidth: crumbLabel.implicitWidth + (index > 0 ? 34 : 18)
                        /// Where the crumb itself starts (after "…")
                        readonly property real crumbX: crumb.x
                        visible: index === 0 ? crumbArea.plan.showRoot : index >= crumbArea.plan.first
                        // "…" before the first folder shown after the library: the ones left out
                        AbstractButton {
                            id: crumbEllipsisHere
                            objectName: "crumbEllipsis"
                            visible: crumbItem.index > 0 && crumbItem.index === crumbArea.plan.first
                                     && (crumbItem.index > 1 || !crumbArea.plan.showRoot)
                            implicitHeight: home.touch ? 44 : 40
                            implicitWidth: crumbEllipsis.implicitWidth
                            onClicked: Popups.openAt(crumbMenu)
                            Accessible.name: qsTr("More folders")
                            background: Rectangle {
                                radius: 8
                                color: crumbEllipsisHere.hovered || crumbEllipsisHere.pressed ? "#e1e4e8" : "transparent"
                            }
                            contentItem: Item {
                                Image {
                                    visible: crumbArea.plan.showRoot
                                    anchors.verticalCenter: parent.verticalCenter
                                    source: app.iconUrl("xqt-chevron-right")
                                    sourceSize.width: 16; sourceSize.height: 16
                                    opacity: 0.6
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: crumbArea.plan.showRoot ? 24 : 12
                                    text: "…"
                                    font.pixelSize: 15
                                    color: "#3c4043"
                                }
                            }
                        }
                        AbstractButton {
                            id: crumb
                            objectName: "crumb"
                            readonly property bool dropTarget: moveDrag.active && moveDrag.hasTarget && moveDrag.target === crumbItem.folder
                            implicitHeight: home.touch ? 44 : 40
                            implicitWidth: crumbItem.naturalWidth
                            width: crumbItem.index === 0 ? crumbArea.plan.rootWidth
                                   : crumbItem.last ? crumbArea.plan.lastWidth : implicitWidth
                            onClicked: app.library.folder = crumbItem.folder
                            Accessible.name: crumbItem.modelData.name
                            background: Rectangle {
                                radius: 8
                                color: crumb.dropTarget ? "#c5cae9" : (crumb.hovered ? "#e1e4e8" : "transparent")
                            }
                            contentItem: Item {
                                Image {
                                    visible: crumbItem.index > 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 0
                                    source: app.iconUrl("xqt-chevron-right")
                                    sourceSize.width: 16; sourceSize.height: 16
                                    opacity: 0.6
                                }
                                Label {
                                    id: crumbLabel
                                    objectName: "crumbLabel"
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: crumbItem.index > 0 ? 24 : 9
                                    width: crumb.width - x - (crumbItem.index > 0 ? 10 : 9)
                                    text: crumbItem.modelData.name
                                    elide: Text.ElideRight
                                    font.pixelSize: 15
                                    font.weight: crumbItem.last ? Font.DemiBold : Font.Normal
                                    color: "#3c4043"
                                }
                            }
                        }
                    }
                }
            }
            // (its size: the "…" of the crumbs is as wide)
            Item { id: crumbEllipsis; visible: false; implicitWidth: 48 }
            AdaptiveMenu {
                id: crumbMenu
                objectName: "crumbMenu"
                title: qsTr("Folders")
                Instantiator {
                    model: crumbArea.hidden
                    delegate: AdaptiveMenuItem {
                        required property var modelData
                        objectName: "crumbMenuEntry"
                        text: modelData.name
                        icon.source: app.iconUrl("xqt-folder")
                        onTriggered: app.library.folder = modelData.folder
                    }
                    onObjectAdded: function(index, object) { crumbMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { crumbMenu.removeItem(object) }
                }
            }
        }
        Label {
            objectName: "listCaption"
            visible: !crumbBar.showCrumbs
            Layout.leftMargin: 8
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: home.searching ? (libraryGrid.count === 1 ? qsTr("1 result") : qsTr("%1 results").arg(libraryGrid.count))
                  : app.library.favouritesOnly ? qsTr("Favourites in %1").arg(app.library.name)
                                 : qsTr("All documents in %1").arg(app.library.name)
            elide: Text.ElideRight
            font.pixelSize: 15
            color: "#3c4043"
        }
        BusyIndicator {
            visible: app.library.importing
            running: visible
            implicitWidth: 28; implicitHeight: 28
        }
        Label {
            objectName: "indexStatus"
            visible: app.library.indexing && app.library.indexTotal > 0
            text: home.shortLayout ? qsTr("Indexing %1/%2").arg(app.library.indexed).arg(app.library.indexTotal)
                                   : qsTr("Indexing for search %1/%2").arg(app.library.indexed).arg(app.library.indexTotal)
            font.pixelSize: 12
            color: "#6b6f75"
        }
    }

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
        readonly property string tip: newButton.tip
        Accessible.name: tip
        onClicked: Popups.openAt(newMenu)
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
    RowLayout {
        id: searchGroup
        parent: home.searchOwnRow ? narrowSearchSlot : searchSlot
        anchors.fill: parent
        spacing: 6
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.minimumWidth: 0
            radius: 22
            color: "#ffffff"
            border.width: searchField.activeFocus ? 2 : 1
            border.color: searchField.activeFocus ? Material.accentColor : "#c9ccd1"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 2
                spacing: 2
                ToolButton {
                    objectName: "librarySearchButton"
                    implicitWidth: 40; implicitHeight: 40
                    icon.source: app.iconUrl("xqt-search")
                    icon.color: "#3c4043"
                    display: AbstractButton.IconOnly
                    onClicked: { searchTyping.stop(); home.lib.searchQuery = searchField.text }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Search (Enter)")
                    ToolTip.delay: 600
                }
                TextField {
                    id: searchField
                    objectName: "librarySearchField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 40
                    background: null
                    selectByMouse: true
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        x: parent.leftPadding
                        width: parent.width - parent.leftPadding - parent.rightPadding  // (a narrow field: …)
                        elide: Text.ElideRight
                        visible: parent.text === "" && parent.preeditText === ""
                        text: parent.width < 170 ? qsTr("Search") : qsTr("Search documents and folders")
                        color: "#8a8d91"
                    }
                    onTextEdited: home.typed()
                    Keys.onReturnPressed: { searchTyping.stop(); home.lib.searchQuery = text; libraryGrid.forceActiveFocus() }
                    Keys.onEnterPressed: { searchTyping.stop(); home.lib.searchQuery = text; libraryGrid.forceActiveFocus() }
                    Keys.onDownPressed: libraryGrid.forceActiveFocus()
                    Keys.onEscapePressed: { text = ""; home.lib.searchQuery = ""; libraryGrid.forceActiveFocus() }
                }
                Label {
                    objectName: "librarySearchHint"
                    visible: searchField.text !== "" && searchField.text.length < 4 && searchField.text !== home.lib.searchQuery
                    text: qsTr("Enter ↵")
                    color: "#6b6f75"
                    font.pixelSize: 12
                }
                // Fuzzy search: an expression that is not valid is searched as plain text, and says why
                Label {
                    objectName: "librarySyntaxHint"
                    visible: home.lib.searchHint !== ""
                    Layout.maximumWidth: 150
                    text: home.lib.searchHint
                    elide: Text.ElideRight
                    color: "#b3261e"
                    font.pixelSize: 12
                    ToolTip.visible: hintHover.hovered
                    ToolTip.text: qsTr("%1 - searched as plain text").arg(home.lib.searchHint)
                    ToolTip.delay: 300
                    HoverHandler { id: hintHover }
                }
                FuzzyToggle { objectName: "librarySearchFuzzy"; implicitHeight: 40 }
                // The reduced search: names only (of documents, and of folders unless the list is flat)
                ToolButton {
                    id: namesOnly
                    objectName: "searchNamesOnly"
                    text: qsTr("Names")
                    checkable: true
                    checked: home.lib.namesOnly
                    onToggled: home.lib.namesOnly = checked
                    implicitHeight: 40
                    leftPadding: 6
                    rightPadding: 6
                    font.pixelSize: 13
                    font.weight: checked ? Font.DemiBold : Font.Normal
                    Material.foreground: checked ? Material.accentColor : "#5f6368"
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("Searching names only - tap to search the text of the documents too")
                                          : qsTr("Search names only: of documents, and of folders when they are shown")
                    ToolTip.delay: 600
                    background: Rectangle {
                        radius: 10
                        color: namesOnly.checked ? "#e0e3f5" : (namesOnly.pressed ? "#e8e8e8" : "transparent")
                    }
                }
                ToolButton {
                    objectName: "librarySearchClear"
                    visible: searchField.text !== ""
                    implicitWidth: 40; implicitHeight: 40
                    icon.source: app.iconUrl("xqt-close")
                    icon.color: "#3c4043"
                    display: AbstractButton.IconOnly
                    Accessible.name: qsTr("Clear the search")
                    onClicked: { searchField.text = ""; searchTyping.stop(); home.lib.searchQuery = "" }
                }
            }
            Timer {
                id: searchTyping
                interval: 300
                onTriggered: home.lib.searchQuery = searchField.text
            }
        }
        IconButton {
            objectName: "extendedSearchButton"
            iconName: "xqt-pages-grid"
            tip: qsTr("Extended search: show the pages with hits of every result")
            checked: home.extended
            onClicked: home.extended = !home.extended
        }
    }

    // --- moving documents and folders: drag onto a folder or a breadcrumb ---
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
            libraryGrid.dropIndex = -1
            // A folder tile under the pointer
            const g = mapToItem(libraryGrid, p.x, p.y)
            const idx = libraryGrid.indexAt(libraryGrid.contentX + g.x, libraryGrid.contentY + g.y)
            const item = libraryGrid.itemAtIndex(idx)
            if (item && item.isFolder && idx !== row && !item.selected) {
                libraryGrid.dropIndex = idx
                target = app.library.relativeFolder(item.path)
                hasTarget = true
                return
            }
            // A breadcrumb (a folder above; not its "…")
            const c = mapToItem(crumbRow, p.x, p.y)
            const crumb = crumbRow.childAt(c.x, c.y)
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
            libraryGrid.dropIndex = -1
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

    AdaptiveMenu {
        id: itemMenu
        objectName: "homeItemMenu"
        title: home.menuMany ? home.countText(home.menuPaths.length) : home.menuName  // (at the top of the sheet on phones)
        AdaptiveMenuItem {
            text: home.menuMany ? qsTr("Open %1").arg(home.countText(home.menuPaths.length))
                                : home.menuKind === "library" ? qsTr("Open library")
                                : home.menuFolder ? qsTr("Open folder") : qsTr("Open")
            onTriggered: home.openAll(home.menuPaths, home.menuModel)
        }
        AdaptiveMenuItem {
            objectName: "openAsReferenceItem"
            text: qsTr("Open as reference")
            // Beside the document open now (without one it is simply opened)
            offered: !home.menuMany && !home.menuFolder && app.tabs.count > 0
                     && ["notes", "pdf", "md", "image", "text"].indexOf(home.menuKind) >= 0
            onTriggered: app.openAsReference(home.menuPath)
        }
        AdaptiveMenuItem {
            id: favouriteItemRef
            objectName: "favouriteItem"
            // (asked when the menu opens: the star is kept beside the document)
            property bool starred: false
            text: starred ? qsTr("Remove from favourites") : qsTr("Add to favourites")
            icon.source: app.iconUrl(starred ? "xqt-star-filled" : "xqt-star")
            icon.color: "transparent"
            offered: !home.menuMany && !home.menuFolder && ["notes", "pdf", "md", "image", "text"].indexOf(home.menuKind) >= 0
            onTriggered: app.setFavouriteFile(home.menuPath, !starred)
            Connections {
                target: itemMenu
                function onAboutToShow() { favouriteItemRef.starred = app.isFavouriteFile(home.menuPath) }
            }
        }
        AdaptiveMenuItem {
            objectName: "openAsLibraryItem"
            text: app.libraryWindows ? qsTr("Open as library (new window)") : qsTr("Open as library")
            offered: !home.menuMany && home.menuFolder && home.menuModel === app.library
            onTriggered: app.openLibraryAt(home.menuPath)
        }
        AdaptiveMenuItem {
            objectName: "selectItem"
            text: qsTr("Select")
            offered: !home.menuMany && home.menuModel && home.menuModel.selectionCount === 0 && home.menuKind !== "library"
            onTriggered: home.menuModel.toggleSelected(home.menuRow)
        }
        AdaptiveMenuItem {
            objectName: "renameItem"
            text: qsTr("Rename…")
            offered: !home.menuMany && home.menuKind !== "library"
            onTriggered: renameDialog.open()
        }
        AdaptiveMenuItem {
            objectName: "copyToItem"
            text: qsTr("Copy to…")
            enabled: app.library.available
            offered: home.menuKind !== "library"
            onTriggered: home.askTransfer(home.menuPaths, true)
        }
        AdaptiveMenuItem {
            objectName: "moveToItem"
            text: qsTr("Move to…")
            enabled: app.library.available
            offered: home.menuKind !== "library"
            onTriggered: home.askTransfer(home.menuPaths, false)
        }
        AdaptiveMenuItem {
            text: qsTr("Show in its folder")
            offered: !home.menuMany && home.menuModel === app.library && (home.searching || app.library.flat) && !home.menuFolder
            onTriggered: {
                searchField.text = ""
                app.library.searchQuery = ""
                app.library.flat = false
                app.library.folder = app.library.relativeFolder(home.menuPath.substring(0, home.menuPath.lastIndexOf("/")))
            }
        }
        AdaptiveMenuItem {
            objectName: "openWithSystemAppItem"
            text: qsTr("Open externally")
            // Markdown, text and other files, images: in the app the system has for them (not notes and PDFs)
            readonly property string file: !home.menuMany && ["md", "image", "text", "other"].indexOf(home.menuKind) >= 0
                                           ? app.externalFileOf(home.menuPath) : ""
            offered: file !== ""
            onTriggered: app.openWithSystemApp(file)
        }
        AdaptiveMenuItem {
            objectName: "copyLinkItem"
            text: qsTr("Copy link")
            // A link to the document, to paste into notes (qt/docs/links.md)
            offered: !home.menuMany && !home.menuFolder && home.menuKind !== "library" && home.menuKind !== "other"
            onTriggered: app.copyDocumentLink(home.menuPath)
        }
        AdaptiveMenuItem {
            objectName: "shareCardItem"
            text: qsTr("Share…")
            offered: !home.menuMany && !home.menuFolder && ["pdf", "notes", "md", "text"].indexOf(home.menuKind) >= 0
            onTriggered: home.shareRequested(home.menuPath)
        }
        AdaptiveMenuItem {
            objectName: "showInFileManagerItem"
            text: qsTr("Show in file manager")
            offered: !home.menuMany && app.canShowInFileManager
            onTriggered: app.showInFileManager(home.menuPath)
        }
        MenuSeparator {}
        AdaptiveMenuItem {
            text: qsTr("Remove from this list")
            offered: home.menuModel === app.recent
            onTriggered: app.recent.removePaths(home.menuPaths)
        }
        AdaptiveMenuItem {
            objectName: "trashItem"
            text: qsTr("Move to trash…")
            offered: home.menuKind !== "library"  // (a library is never trashed from the Recent grid)
            onTriggered: home.askTrash(home.menuModel, home.menuPaths)
        }
    }

    AdaptiveDialog {
        id: renameDialog
        objectName: "renameDialog"
        title: home.menuFolder ? qsTr("Rename folder") : qsTr("Rename document")
        preferredWidth: 440
        onAboutToShow: { renameField.text = home.menuName; renameField.selectAll(); renameField.forceActiveFocus() }
        ColumnLayout {
            width: renameDialog.availableWidth
            TextField {
                id: renameField
                objectName: "renameField"
                Layout.fillWidth: true
                selectByMouse: true
                Keys.onReturnPressed: renameDialog.accept()
                Keys.onEnterPressed: renameDialog.accept()
            }
            Label {
                visible: !home.menuFolder
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6b6f75"
                text: home.menuKind === "other" || home.menuKind === "text" ? qsTr("The whole file name, with its extension.")
                                                                              : qsTr("The Xournal file and its PDF are renamed together.")
            }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (renameField.text.trim() !== "" && home.menuModel) home.menuModel.rename(home.menuRow, renameField.text)
    }

    AdaptiveDialog {
        id: folderNameDialog
        objectName: "folderNameDialog"
        property int row: -1
        title: qsTr("New folder")
        preferredWidth: 440
        onAboutToShow: { folderField.text = ""; folderField.forceActiveFocus() }
        TextField {
            id: folderField
            objectName: "folderNameField"
            width: folderNameDialog.availableWidth
            placeholderText: qsTr("Folder name")
            selectByMouse: true
            Keys.onReturnPressed: folderNameDialog.accept()
            Keys.onEnterPressed: folderNameDialog.accept()
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (folderField.text.trim() !== "") app.library.createFolder(folderField.text)
    }

    // "New text document" (a PDF text document, ".pdf"), "New Markdown file" / "New text file": its name (made in the
    // current folder and opened to write in)
    AdaptiveDialog {
        id: textFileDialog
        objectName: "textFileDialog"
        property string extension: ".md"
        title: extension === ".pdf" ? qsTr("New text document") : extension === ".md" ? qsTr("New Markdown file")
                                                                                     : qsTr("New text file")
        preferredWidth: 440
        onAboutToShow: { textFileField.text = ""; textFileField.forceActiveFocus() }
        RowLayout {
            width: textFileDialog.availableWidth
            TextField {
                id: textFileField
                objectName: "textFileName"
                Layout.fillWidth: true
                placeholderText: qsTr("Name (Untitled)")
                selectByMouse: true
                Keys.onReturnPressed: textFileDialog.accept()
                Keys.onEnterPressed: textFileDialog.accept()
            }
            Label { text: textFileDialog.extension; color: "#5f6368" }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: extension === ".pdf" ? app.createTextDocument(textFileField.text)
                                         : app.createTextFile(textFileField.text, extension)
    }

    // Where to copy / move documents and folders: a folder of this library or of another one.
    AdaptiveDialog {
        id: transferDialog
        objectName: "transferDialog"
        property var paths: []
        property bool copy: false
        property var libraries: []
        property string targetRoot: ""
        title: (copy ? qsTr("Copy %1 to") : qsTr("Move %1 to"))
                   .arg(paths.length === 1 ? "“" + paths[0].substring(paths[0].lastIndexOf("/") + 1) + "”" : home.countText(paths.length))
        preferredWidth: 480
        fillBody: true
        preferredHeight: 580
        standardButtons: Dialog.Cancel
        onAboutToShow: {
            libraries = app.libraries()
            targetRoot = app.library.rootPath
            libraryBox.currentIndex = Math.max(0, libraryBox.indexOfValue(targetRoot))
        }
        function choose(folderPath) {
            const paths = transferDialog.paths, copy = transferDialog.copy
            transferDialog.close()
            // Into the Downloads folder from elsewhere: short-lived, ask first
            home.confirmImport(app.library.isTemporaryFolder(folderPath) && !app.library.temporary, function() {
                app.library.transferTo(paths, folderPath, copy)
                app.recent.clearSelection()
            })
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            ComboBox {
                id: libraryBox
                objectName: "transferLibraryBox"
                Layout.fillWidth: true
                model: transferDialog.libraries
                valueRole: "path"
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view ? ListView.view.width : implicitWidth
                    text: modelData.downloads ? qsTr("Downloads folder") : modelData.name
                    font.weight: modelData.current ? Font.DemiBold : Font.Normal
                    icon.source: app.iconUrl(modelData.downloads ? "xqt-download" : "xqt-library")
                    icon.color: "#566d86"
                    highlighted: libraryBox.highlightedIndex === index
                }
                displayText: {
                    const lib = transferDialog.libraries[currentIndex]
                    if (!lib) return ""
                    const name = lib.downloads ? qsTr("Downloads folder") : lib.name
                    return lib.current ? qsTr("%1 (this library)").arg(name) : qsTr("Library: %1").arg(name)
                }
                onActivated: transferDialog.targetRoot = currentValue
            }
            ListView {
                objectName: "transferFolders"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: transferDialog.opened ? app.library.foldersOf(transferDialog.targetRoot) : []
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    leftPadding: 16 + modelData.depth * 20
                    text: modelData.name
                    icon.source: app.iconUrl(modelData.depth === 0 ? "xqt-library" : "xqt-folder")
                    icon.color: "#566d86"
                    onClicked: transferDialog.choose(modelData.path)
                }
            }
        }
    }

    AdaptiveDialog {
        id: trashDialog
        objectName: "trashDialog"
        kind: "question"
        title: qsTr("Move to trash?")
        preferredWidth: 460
        ColumnLayout {
            width: trashDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: home.menuPaths.length > 1
                      ? qsTr("%1 go to the trash (documents with their PDFs, folders with everything in them).").arg(home.countText(home.menuPaths.length))
                      : qsTr("“%1” goes to the trash (a document with its PDF, a folder with everything in it).")
                            .arg(home.menuPaths.length === 1 ? home.menuPaths[0].substring(home.menuPaths[0].lastIndexOf("/") + 1) : "")
            }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            app.library.trashPaths(home.menuPaths)
            if (home.menuModel === app.recent) app.recent.clearSelection()
        }
    }

    // Android: why "All files access" is asked for, before the system's page for it
    AdaptiveDialog {
        id: storageAccessDialog
        objectName: "storageAccessDialog"
        kind: "question"
        property string folder: ""  // then opened as library ("": the folder picker)
        function ask(path) { folder = path; open() }
        title: qsTr("Allow access to your files?")
        preferredWidth: 520
        Label {
            width: storageAccessDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("To use a folder of the phone's storage as a library (for example one that Syncthing, "
                       + "FolderSync or Autosync keeps in sync), Xournal Qt needs “All files access”. It then "
                       + "works with the folder as on a computer: its documents, previews and search, and changes "
                       + "other apps make are seen at once. It only reads and writes the folders you open as a "
                       + "library.")
                  + "\n\n" + qsTr("Android shows its settings page next: turn on the switch for Xournal Qt, then "
                                   + "come back.")
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Not now")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "storageAccessContinue"
                text: qsTr("Continue")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.requestStorageAccess(folder)
    }

    // Android: the libraries belong in the phone's Documents folder (they are in the app's own folder, which Android
    // deletes with the app): explained, then "All files access" and the move (AppController::moveLibrariesHome)
    AdaptiveDialog {
        id: librariesHomeDialog
        objectName: "librariesHomeDialog"
        kind: "question"
        property string toMove: ""
        onAboutToShow: toMove = app.librariesToMove()
        title: qsTr("Keep libraries on the phone?")
        preferredWidth: 520
        Label {
            width: librariesHomeDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Your libraries are kept in %1 on the phone, where other apps (file managers, Syncthing) see "
                       + "them and they survive uninstalling the app.").arg(app.sharedLibrariesName)
                  + (librariesHomeDialog.toMove !== ""
                     ? "\n\n" + qsTr("The libraries you have now (%1) are moved there. Each file is checked when it has "
                                        + "arrived; nothing is deleted before all of them are there.").arg(librariesHomeDialog.toMove)
                     : "")
                  + (app.storageAccess ? ""
                     : "\n\n" + qsTr("For this Xournal Qt needs \u201cAll files access\u201d. Android shows its settings "
                                        + "page next: turn on the switch for Xournal Qt, then come back."))
        }
        footer: DialogButtonBox {
            Button {
                objectName: "librariesHomeNotNow"
                text: qsTr("Not now")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "librariesHomeContinue"
                text: qsTr("Continue")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: app.moveLibrariesHome()
        onRejected: app.declineLibrariesHome()
    }
    /// At the start, after the other questions (Main.qml): the offer, once
    property bool librariesHomeOffered: false
    function offerLibrariesHomeAtStart() {
        if (!librariesHomeOffered && app.offerLibrariesHome) {
            librariesHomeOffered = true
            librariesHomeDialog.open()
        }
    }

    // The move in progress: copied, checked, then the old copies removed
    AdaptiveDialog {
        id: libraryMoveDialog
        objectName: "libraryMoveDialog"
        kind: "card"
        readonly property var move: app.libraryMove
        closePolicy: Popup.NoAutoClose
        title: qsTr("Moving the libraries")
        preferredWidth: 480
        Connections {
            target: app.libraryMove
            function onRunningChanged() {
                if (app.libraryMove.running) libraryMoveDialog.open()
                else libraryMoveDialog.close()
            }
        }
        ColumnLayout {
            width: libraryMoveDialog.availableWidth
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const m = libraryMoveDialog.move
                    if (m.step === "clean") return qsTr("Removing the old copies from the app\u2019s folder…")
                    const files = qsTr("%1 of %2 files").arg(m.files).arg(m.totalFiles)
                    return (m.step === "verify" ? qsTr("Checking the copies in %1…") : qsTr("Copying to %1…"))
                               .arg(app.sharedLibrariesName) + "\n" + files
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: libraryMoveDialog.move.fraction
                indeterminate: libraryMoveDialog.move.step === "clean"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6b6f75"
                text: qsTr("Until everything is copied and checked, the libraries stay where they are.")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                enabled: libraryMoveDialog.move.step !== "clean"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole  // (closed when the move stops)
                onClicked: app.cancelLibrariesMove()
            }
        }
    }

    AdaptiveDialog {
        id: newLibraryDialog
        title: qsTr("New library")
        preferredWidth: 460
        onAboutToShow: { libraryName.text = ""; libraryName.forceActiveFocus() }
        ColumnLayout {
            width: newLibraryDialog.availableWidth
            TextField {
                id: libraryName
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                selectByMouse: true
                Keys.onReturnPressed: newLibraryDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6b6f75"
                text: app.libraryWindows ? qsTr("A library is a folder in Documents/Xournal_Libraries. It opens in a new window.")
                                         : qsTr("A library is a folder in Documents/Xournal_Libraries.")
            }
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (!app.createLibrary(libraryName.text)) {
                errorDialog.text = qsTr("A library named “%1” cannot be created (the name is taken or not allowed).").arg(libraryName.text)
                errorDialog.open()
            }
        }
    }

    // Export library as archive: what it does, the whole library or this folder, then a folder outside the library
    AdaptiveDialog {
        id: libraryArchiveDialog
        objectName: "libraryArchiveDialog"
        preferredWidth: 540
        title: qsTr("Export library as archive")
        onAboutToShow: archiveWholeLibrary.checked = true
        ColumnLayout {
            width: libraryArchiveDialog.availableWidth
            spacing: 6
            Label {
                objectName: "libraryArchiveExplanation"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Every document becomes a PDF made for keeping (PDF/A-3): readable for decades in any PDF "
                           + "viewer, with your ink merged into the pages and the full Xournal data inside, so this "
                           + "app can still open it for editing. Images, Markdown, text and other files are copied "
                           + "as they are, and a README.txt explains the files.") + "\n\n"
                      + qsTr("It all goes into a new folder “%1 archive” inside a folder you choose, with the folders "
                             + "of the library. The library itself is not changed.").arg(app.library.name)
            }
            ButtonGroup { id: archiveScope }
            RadioButton {
                id: archiveWholeLibrary
                objectName: "archiveWholeLibrary"
                Layout.fillWidth: true
                ButtonGroup.group: archiveScope
                text: qsTr("The whole library")
            }
            RadioButton {
                id: archiveThisFolder
                objectName: "archiveThisFolder"
                Layout.fillWidth: true
                ButtonGroup.group: archiveScope
                enabled: app.library.folder !== ""
                text: enabled ? qsTr("Only this folder: %1").arg(app.library.folder) : qsTr("Only this folder")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "libraryArchiveChoose"
                text: qsTr("Choose a folder…")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            libraryArchiveFolderDialog.onlyFolder = archiveThisFolder.checked
            libraryArchiveFolderDialog.open()
        }
    }
    FolderDialog {
        id: libraryArchiveFolderDialog
        objectName: "libraryArchiveFolderDialog"
        property bool onlyFolder: false
        title: qsTr("Folder for the archive (outside the library)")
        onAccepted: app.exportLibraryArchive(selectedFolder, onlyFolder)
    }
    // While it runs: progress and Cancel
    AdaptiveDialog {
        id: libraryArchiveProgress
        objectName: "libraryArchiveProgress"
        kind: "card"
        modal: false
        closePolicy: Popup.NoAutoClose
        preferredWidth: 460
        title: qsTr("Writing the archive…")
        visible: app.libraryArchive.running
        ColumnLayout {
            width: libraryArchiveProgress.availableWidth
            spacing: 6
            ProgressBar {
                objectName: "libraryArchiveBar"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, app.libraryArchive.total)
                value: app.libraryArchive.done
            }
            Label {
                objectName: "libraryArchiveStatus"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: qsTr("%1 of %2").arg(app.libraryArchive.done).arg(app.libraryArchive.total)
                      + (app.libraryArchive.current !== "" ? " · " + app.libraryArchive.current : "")
                font.pixelSize: 13
                color: "#6b6f75"
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "libraryArchiveCancel"
                text: qsTr("Cancel")
                onClicked: app.cancelLibraryArchive()
            }
        }
    }
    // At the end: what was done
    AdaptiveDialog {
        id: libraryArchiveSummary
        objectName: "libraryArchiveSummary"
        kind: "card"
        property var summary: ({})
        preferredWidth: 560
        title: summary.cancelled ? qsTr("Archive cancelled") : qsTr("Archive written")
        ColumnLayout {
            width: libraryArchiveSummary.availableWidth
            spacing: 6
            Label {
                objectName: "libraryArchiveSummaryText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    const s = libraryArchiveSummary.summary
                    if (s.archived === undefined)
                        return ""
                    let t = qsTr("%1 archived (%2 of them PDF/A-3b), %3 copied, %4 not PDF/A, %5 failed.")
                                .arg(s.archived).arg(s.pdfa).arg(s.copied).arg(s.notPdfA.length).arg(s.failed.length)
                    if (s.cancelled)
                        t += "\n" + qsTr("Cancelled: the archive is incomplete.")
                    if (s.notPdfA.length > 0)
                        t += "\n\n" + qsTr("Not PDF/A (still readable everywhere):") + "\n• " + s.notPdfA.join("\n• ")
                    if (s.failed.length > 0)
                        t += "\n\n" + qsTr("Failed:") + "\n• " + s.failed.join("\n• ")
                    return t
                }
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "libraryArchiveShow"
                text: qsTr("Show in file manager")
                visible: app.canShowInFileManager
                flat: true
                onClicked: { app.showInFileManager(libraryArchiveSummary.summary.target); libraryArchiveSummary.close() }
            }
            Button {
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
    Connections {
        target: app.libraryArchive
        function onFinished(summary) {
            libraryArchiveSummary.summary = summary
            libraryArchiveSummary.open()
        }
    }

    FolderChooser {
        id: folderChooser
        onChosen: function(path) { app.openLibraryAt(path) }
    }
    FolderDialog {
        id: openLibraryDialog
        title: qsTr("Open a folder as library")
        onAccepted: app.openLibrary(selectedFolder)
    }

    FileDialog {
        id: importDialog
        objectName: "importDialog"
        title: qsTr("Import into the library")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf *.md *.png *.jpg *.jpeg *.webp *.heic *.heif)"),
                      qsTr("All files (*)")]
        onAccepted: {
            const files = selectedFiles, folder = app.library.flat || home.searching ? "" : app.library.folder
            home.confirmImport(app.library.temporary, function() { app.library.importUrls(files, folder) })
        }
    }
    FolderDialog {
        id: importFolderDialog
        objectName: "importFolderDialog"
        title: qsTr("Import a folder (with its subfolders) into the library")
        onAccepted: {
            const dir = selectedFolder, folder = app.library.flat || home.searching ? "" : app.library.folder
            home.confirmImport(app.library.temporary, function() { app.library.importUrls([dir], folder) })
        }
    }

    AdaptiveDialog {
        id: temporaryImportDialog
        objectName: "temporaryImportDialog"
        kind: "question"
        property var action: null
        title: qsTr("Import into Downloads?")
        preferredWidth: 520
        ColumnLayout {
            width: temporaryImportDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This goes into your Downloads folder. Files there are often cleaned up or deleted. "
                           + "Documents you want to keep are better in one of your libraries.")
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Import anyway"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            const action = temporaryImportDialog.action
            temporaryImportDialog.action = null
            if (action) action()
        }
        onRejected: action = null
    }

    NewDocumentDialog { id: newDocumentDialog }

    // Sync conflicts of a document (the badge on its card): compare side by side, or keep one (the other goes to the
    // trash; where there is none, deleted after asking)
    AdaptiveDialog {
        id: conflictDialog
        objectName: "conflictDialog"
        property string documentPath: ""
        property var items: []  // app.library.conflictsOf: the document first, then its conflict copies
        function show(path) {
            documentPath = path
            items = app.library.conflictsOf(path)
            if (items.length > 1) open()
        }
        title: qsTr("Sync conflict")
        preferredWidth: 560
        standardButtons: Dialog.Close
        ColumnLayout {
            id: conflictColumn
            width: conflictDialog.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("A sync app found this document changed in two places and kept both versions. Compare "
                           + "them side by side, then keep one; the other goes to the trash.")
                      + (app.library.canTrash ? "" : " " + qsTr("(There is no trash here: it is deleted.)"))
            }
            Repeater {
                model: conflictDialog.items
                delegate: Frame {
                    id: conflictRow
                    required property var modelData
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            font.weight: Font.DemiBold
                            text: conflictRow.modelData.original ? qsTr("This document: %1").arg(conflictRow.modelData.name)
                                                                 : conflictRow.modelData.name
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: "#5f6368"
                            text: {
                                const d = conflictRow.modelData
                                let parts = []
                                if (!d.original && d.app) parts.push(qsTr("Conflict copy (%1)").arg(d.app))
                                else if (!d.original) parts.push(qsTr("Conflict copy"))
                                parts.push(qsTr("changed %1").arg(d.modified.toLocaleString(Qt.locale(), Locale.ShortFormat)))
                                parts.push(home.sizeText(d.size))
                                return parts.join(" · ")
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            visible: !conflictRow.modelData.original
                            spacing: 8
                            Button {
                                objectName: "conflictCompareButton"
                                text: qsTr("Compare")
                                onClicked: {
                                    conflictDialog.close()
                                    app.compareConflict(conflictDialog.documentPath, conflictRow.modelData.path)
                                }
                            }
                            Button {
                                objectName: "conflictKeepDocumentButton"
                                text: qsTr("Keep the document")
                                onClicked: conflictConfirm.ask(conflictRow.modelData.path, false,
                                                               conflictRow.modelData.name)
                            }
                            Button {
                                objectName: "conflictKeepCopyButton"
                                text: qsTr("Keep this copy")
                                onClicked: conflictConfirm.ask(conflictRow.modelData.path, true,
                                                               conflictDialog.items[0].name)
                            }
                        }
                    }
                }
            }
        }
    }
    // Keep one: without a trash (Android), what goes is deleted - asked first
    AdaptiveDialog {
        id: conflictConfirm
        objectName: "conflictConfirm"
        kind: "question"
        property string copyPath: ""
        property bool keepCopy: false
        property string goes: ""
        function ask(path, keep, goesName) {
            copyPath = path
            keepCopy = keep
            goes = goesName
            if (app.library.canTrash) resolve()
            else open()
        }
        function resolve() {
            if (app.library.resolveConflict(copyPath, keepCopy)) {
                conflictDialog.show(conflictDialog.documentPath)  // (more copies: still listed)
                if (conflictDialog.items.length < 2) conflictDialog.close()
            }
        }
        title: qsTr("Delete the other version?")
        preferredWidth: 480
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: conflictConfirm.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("There is no trash on this device: %1 is deleted and cannot be brought back.").arg(conflictConfirm.goes)
        }
        onAccepted: resolve()
    }

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
        anchors.bottomMargin: addFab.visible ? 88 + home.safeBottom : 32
    }
}
