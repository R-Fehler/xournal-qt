// Home screen (shown when no document is open, or with the home tab): the library of this window and the recently
// opened documents, as grids of first-page previews.
//  - Library: folders (tap to enter, breadcrumbs to go back) or all documents at once; search in folder names and
//    the text and names of all documents; new document, import (files or whole folder trees, also by dropping them),
//    new folder; rename, move (drag onto a folder or a breadcrumb, or "Move to"), move to trash. A .xopp and its PDF
//    are one document.
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
    /// A narrow window (a phone): the library search gets a row of its own
    readonly property bool narrow: width < 760
    color: "#eef0f3"
    /// 0: library, 1: recent documents
    property int page: app.library.available ? 0 : 1
    readonly property var lib: app.library
    readonly property bool searching: lib.searchQuery !== ""
    /// Extended search: each result shows its pages with hits (taller cells).
    property bool extended: false
    readonly property bool extendedView: extended && searching && page === 0
    // Grid zoom: columns chosen with − / + (Ctrl+wheel, pinch); 0: from the width. Separate for the extended view.
    property int columnsNormal: 0
    property int columnsExtended: 0
    function autoColumns(width, extendedCells) { return Math.max(extendedCells ? 1 : 2, Math.floor(width / (extendedCells ? 380 : 210))) }
    function columnsFor(width, extendedCells) {
        const chosen = extendedCells ? columnsExtended : columnsNormal
        return chosen > 0 ? chosen : autoColumns(width, extendedCells)
    }
    /// −1: smaller cells (more columns), +1: bigger cells
    function zoom(step) {
        const grid = page === 0 ? libraryGrid : recentGrid
        const n = Math.max(1, Math.min(12, grid.columns - step))
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
    function focusGrid() { (page === 0 ? libraryGrid : recentGrid).forceActiveFocus() }
    function focusSearch() {
        page = 0
        searchField.forceActiveFocus()
        searchField.selectAll()
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
    function pickLibraryFolder() {
        if (app.storageAccess) openLibraryDialog.open()
        else storageAccessDialog.ask("")
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
    readonly property var currentModel: page === 0 ? app.library : app.recent
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
        itemMenu.popup(item, x, y)
    }

    Connections {
        target: app.library
        function onError(text) { errorDialog.text = text; errorDialog.open() }
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
        function onStorageAccessNeeded(folder) { storageAccessDialog.ask(folder) }
        function onPickLibraryFolder() { openLibraryDialog.open() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- while items are selected: what to do with them ---
        Rectangle {
            objectName: "homeSelectionBar"
            visible: home.selectionCount > 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.preferredHeight: 48
            radius: 24
            color: "#e8eaf6"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 8
                spacing: 2
                IconButton { iconName: "xqt-close"; tip: qsTr("Clear the selection (Esc)"); onClicked: home.currentModel.clearSelection() }
                Label {
                    objectName: "selectionLabel"
                    text: qsTr("%1 selected").arg(home.selectionCount)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#283593"
                    Layout.leftMargin: 4
                }
                Button { text: qsTr("Select all"); flat: true; onClicked: home.currentModel.selectAll() }
                Item { Layout.fillWidth: true }
                Button {
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
                    text: qsTr("Trash…")
                    flat: true
                    icon.source: app.iconUrl("xqt-delete")
                    onClicked: home.askTrash(home.currentModel, home.currentModel.selectedPaths())
                }
            }
        }

        // --- header: library / recent, search, actions ---
        // (a window too narrow for all of it scrolls it sideways, like the tool bar; a narrow one, e.g. a phone, has
        // the search in a row of its own below)
        Flickable {
            id: headerFlick
            objectName: "homeHeader"
            visible: home.selectionCount === 0
            Layout.fillWidth: true
            Layout.topMargin: 10
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
            // (the search field gives way down to its minimum before the row scrolls, as it did without scrolling)
            width: Math.max(headerFlick.width - 24, implicitWidth - (searchSlot.visible
                            ? searchSlot.Layout.preferredWidth - searchSlot.Layout.minimumWidth : 0))
            height: headerFlick.height
            spacing: 6

            Rectangle {
                radius: 22
                color: "#e1e4e8"
                implicitWidth: pageSwitch.implicitWidth + 8
                implicitHeight: 44
                Row {
                    id: pageSwitch
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [
                            { text: app.library.available ? app.library.name : qsTr("Library"), icon: "xqt-library", enabled: app.library.available },
                            { text: qsTr("Recent"), icon: "xqt-history", enabled: true }
                        ]
                        delegate: AbstractButton {
                            id: switchButton
                            required property int index
                            required property var modelData
                            objectName: index === 0 ? "libraryPageButton" : "recentPageButton"
                            enabled: modelData.enabled
                            implicitHeight: 38
                            implicitWidth: switchRow.implicitWidth + 28
                            onClicked: home.page = index
                            background: Rectangle {
                                radius: 19
                                color: home.page === switchButton.index ? "#ffffff" : "transparent"
                            }
                            contentItem: Item {
                                RowLayout {
                                    id: switchRow
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Image { source: app.iconUrl(switchButton.modelData.icon); sourceSize.width: 18; sourceSize.height: 18 }
                                    Label {
                                        text: switchButton.modelData.text
                                        font.weight: home.page === switchButton.index ? Font.DemiBold : Font.Normal
                                        color: switchButton.enabled ? "#202124" : "#9aa0a6"
                                        elide: Text.ElideRight
                                        Layout.maximumWidth: 220
                                    }
                                }
                            }
                        }
                    }
                }
            }
            IconButton {
                objectName: "libraryMenuButton"
                iconName: "xqt-chevron-down"
                tip: qsTr("Libraries")
                implicitWidth: 36
                onClicked: Popups.openAt(libraryMenu)
                // The libraries: this window shows one (highlighted); another one opens in a new window.
                Menu {
                    id: libraryMenu
                    objectName: "libraryMenu"
                    width: 320
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
                        delegate: MenuItem {
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
                    MenuItem {
                        text: app.libraryWindows ? qsTr("New library… (new window)") : qsTr("New library…")
                        onTriggered: newLibraryDialog.open()
                    }
                    MenuItem {
                        objectName: "openFolderAsLibraryItem"
                        text: app.libraryWindows ? qsTr("Open a folder as library… (new window)") : qsTr("Open a folder as library…")
                        onTriggered: home.pickLibraryFolder()
                    }
                    MenuItem {
                        text: qsTr("Show in file manager")
                        enabled: app.library.available
                        onTriggered: app.showInFileManager(app.library.rootPath)
                    }
                    MenuItem {
                        objectName: "exportLibraryArchiveItem"
                        text: qsTr("Export library as archive…")
                        enabled: app.library.available && !app.libraryArchive.running
                        onTriggered: libraryArchiveDialog.open()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Search in the whole library (searchGroup, below): here, or in a row of its own when the window is narrow
            Item {
                id: searchSlot
                visible: !home.narrow && home.page === 0 && app.library.available
                // As wide as there is room for, up to 380 and the button (the buttons of the row come first)
                Layout.fillWidth: true
                Layout.minimumWidth: 180 + 6 + 48
                Layout.maximumWidth: 380 + 6 + 48
                Layout.preferredWidth: 380 + 6 + 48
                Layout.preferredHeight: 48
            }

            Item { Layout.fillWidth: true }

            // The same as in the settings: open a document at the page where it was left (a small toggle)
            ToolButton {
                id: resume
                objectName: "resumeSwitch"
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

            // New: a document of notes, a Markdown file or a text file (in the current folder)
            IconButton {
                objectName: "newDocumentButton"
                iconName: "xqt-file-plus"
                tip: qsTr("New document, Markdown file or text file")
                onClicked: Popups.openAt(newMenu)
                Menu {
                    id: newMenu
                    objectName: "newMenu"
                    MenuItem { objectName: "newDocumentItem"; text: qsTr("New document…"); onTriggered: newDocumentDialog.open() }
                    MenuItem {
                        objectName: "newMarkdownItem"
                        text: qsTr("New Markdown file…")
                        enabled: app.library.available
                        onTriggered: { textFileDialog.extension = ".md"; textFileDialog.open() }
                    }
                    MenuItem {
                        objectName: "newTextItem"
                        text: qsTr("New text file…")
                        enabled: app.library.available
                        onTriggered: { textFileDialog.extension = ".txt"; textFileDialog.open() }
                    }
                }
            }
            IconButton {
                objectName: "importButton"
                visible: home.page === 0 && app.library.available
                iconName: "xqt-import"
                tip: qsTr("Import PDFs and Xournal files, or a whole folder (copies them into this folder)")
                onClicked: Popups.openAt(importMenu)
                Menu {
                    id: importMenu
                    objectName: "importMenu"
                    MenuItem { text: qsTr("Import files…"); onTriggered: importDialog.open() }
                    MenuItem { text: qsTr("Import a folder with its subfolders…"); onTriggered: importFolderDialog.open() }
                }
            }
            IconButton {
                objectName: "newFolderButton"
                visible: home.page === 0 && app.library.available
                enabled: !app.library.flat && !home.searching
                iconName: "xqt-folder-plus"
                tip: qsTr("New folder")
                onClicked: { folderNameDialog.row = -1; folderNameDialog.open() }
            }
            IconButton {
                objectName: "flatButton"
                visible: home.page === 0 && app.library.available
                iconName: app.library.flat ? "xqt-layout-grid" : "xqt-folder-tree"
                tip: app.library.flat ? qsTr("All documents (show folders)") : qsTr("Folders (show all documents at once)")
                checked: app.library.flat
                onClicked: app.library.flat = !app.library.flat
            }
            // Which kinds of files the library shows (a setting of the library), marked when not the default
            IconButton {
                id: showButton
                objectName: "showButton"
                visible: home.page === 0 && app.library.available
                iconName: "xqt-filter"
                tip: app.library.showFiltered ? qsTr("Show: some kinds of files are hidden or added") : qsTr("Show: which kinds of files")
                checked: app.library.showFiltered
                onClicked: showPopup.opened ? showPopup.close() : showPopup.open()
                Popup {
                    id: showPopup
                    objectName: "showPopup"
                    y: showButton.height
                    x: Math.min(0, showButton.width - width)
                    padding: 8
                    readonly property var show: app.library.show
                    component ShowToggle: CheckDelegate {
                        property string key
                        Layout.fillWidth: true
                        checked: showPopup.show[key] === true
                        onToggled: app.library.setShown(key, checked)
                        font.pixelSize: 14
                        topPadding: 6
                        bottomPadding: 6
                    }
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            text: qsTr("Show in this library")
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: "#5f6368"
                            Layout.leftMargin: 12
                            Layout.bottomMargin: 4
                        }
                        ShowToggle { objectName: "showNotes"; key: "notes"; text: qsTr("Notes (.xopp, .xoj)") }
                        ShowToggle { objectName: "showPdfs"; key: "pdfs"; text: qsTr("PDFs") }
                        ShowToggle {
                            objectName: "showOnlyPdfsWithNotes"
                            key: "onlyPdfsWithNotes"
                            text: qsTr("Only PDFs with notes")
                            enabled: showPopup.show.pdfs === true
                            leftPadding: 40
                            font.pixelSize: 13
                        }
                        ShowToggle { objectName: "showMarkdown"; key: "markdown"; text: qsTr("Markdown (.md)") }
                        ShowToggle { objectName: "showImages"; key: "images"; text: qsTr("Images") }
                        ShowToggle { objectName: "showText"; key: "text"; text: qsTr("Text and code (.txt, .tex, .py, …)") }
                        ShowToggle { objectName: "showOther"; key: "other"; text: qsTr("All other files") }
                        Button {
                            objectName: "showDefaults"
                            Layout.alignment: Qt.AlignRight
                            flat: true
                            text: qsTr("Defaults")
                            enabled: app.library.showFiltered
                            onClicked: app.library.resetShown()
                        }
                    }
                }
            }
            IconButton {
                visible: home.page === 0 && app.library.available
                iconName: "xqt-sort"
                tip: qsTr("Sort")
                onClicked: Popups.openAt(sortMenu)
                Menu {
                    id: sortMenu
                    MenuItem { text: qsTr("By name"); checkable: true; checked: app.library.sortBy === "name"; onTriggered: app.library.sortBy = "name" }
                    MenuItem { text: qsTr("Last modified first"); checkable: true; checked: app.library.sortBy === "modified"; onTriggered: app.library.sortBy = "modified" }
                    MenuItem { objectName: "sortByRead"; text: qsTr("Last read first"); checkable: true; checked: app.library.sortBy === "read"; onTriggered: app.library.sortBy = "read" }
                }
            }
            IconButton {
                visible: home.page === 1
                iconName: "xopp-document-open"
                tip: qsTr("Open a file")
                onClicked: home.openFileRequested()
            }
            ToolSeparator {}
            ToolButton {
                objectName: "zoomOutButton"
                text: "−"
                font.pixelSize: 22
                implicitWidth: 40
                enabled: (home.page === 0 ? libraryGrid : recentGrid).columns < 12
                onClicked: home.zoom(-1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Smaller cells (Ctrl+wheel, pinch)")
                ToolTip.delay: 600
            }
            ToolButton {
                objectName: "zoomInButton"
                text: "+"
                font.pixelSize: 22
                implicitWidth: 40
                enabled: (home.page === 0 ? libraryGrid : recentGrid).columns > 1
                onClicked: home.zoom(1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Bigger cells (Ctrl+wheel, pinch)")
                ToolTip.delay: 600
            }
            ToolSeparator {}
            // (the tool bar with its menu is not there while the library is shown)
            IconButton {
                objectName: "homeSettingsButton"
                iconName: "xqt-settings"
                tip: qsTr("Settings (Ctrl+,)")
                onClicked: home.settingsRequested()
            }
        }
        }
        // The search's own row in a narrow window
        Item {
            id: narrowSearchSlot
            visible: home.narrow && home.selectionCount === 0 && home.page === 0 && app.library.available
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            Layout.preferredHeight: 48
        }

        // --- where we are in the library ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: 44
            visible: home.page === 0 && app.library.available
            spacing: 2

            IconButton {
                objectName: "folderUpButton"
                iconName: "xqt-arrow-up"
                tip: qsTr("Up (Backspace)")
                implicitWidth: 40; implicitHeight: 40
                visible: !home.searching && !app.library.flat
                enabled: app.library.folder !== ""
                onClicked: app.library.goUp()
            }
            Row {
                id: crumbRow
                visible: !home.searching && !app.library.flat
                spacing: 0
                Repeater {
                    model: app.library.breadcrumbs
                    delegate: AbstractButton {
                        id: crumb
                        required property int index
                        required property var modelData
                        readonly property string folder: modelData.folder
                        readonly property bool dropTarget: moveDrag.active && moveDrag.hasTarget && moveDrag.target === folder
                        implicitHeight: 36
                        implicitWidth: crumbLabel.implicitWidth + (index > 0 ? 34 : 18)
                        onClicked: app.library.folder = folder
                        background: Rectangle {
                            radius: 8
                            color: crumb.dropTarget ? "#c5cae9" : (crumb.hovered ? "#e1e4e8" : "transparent")
                        }
                        contentItem: Item {
                            Image {
                                visible: crumb.index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                x: 0
                                source: app.iconUrl("xqt-chevron-right")
                                sourceSize.width: 16; sourceSize.height: 16
                                opacity: 0.6
                            }
                            Label {
                                id: crumbLabel
                                anchors.verticalCenter: parent.verticalCenter
                                x: crumb.index > 0 ? 24 : 9
                                text: crumb.modelData.name
                                font.pixelSize: 15
                                font.weight: crumb.index === app.library.breadcrumbs.length - 1 ? Font.DemiBold : Font.Normal
                                color: "#3c4043"
                            }
                        }
                    }
                }
            }
            Label {
                visible: home.searching || app.library.flat
                Layout.leftMargin: 8
                text: home.searching ? (libraryGrid.count === 1 ? qsTr("1 result") : qsTr("%1 results").arg(libraryGrid.count))
                                     : qsTr("All documents in %1").arg(app.library.name)
                font.pixelSize: 15
                color: "#3c4043"
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                visible: app.library.importing
                running: visible
                implicitWidth: 28; implicitHeight: 28
            }
            Label {
                objectName: "indexStatus"
                visible: app.library.indexing && app.library.indexTotal > 0
                text: qsTr("Indexing for search %1/%2").arg(app.library.indexed).arg(app.library.indexTotal)
                font.pixelSize: 12
                color: "#6b6f75"
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
                    cellWidth: Math.floor(width / columns)
                    // Extended search: a smaller first page, the row of pages with hits below the title.
                    readonly property int stripHeight: home.extendedView ? Math.round(Math.max(120, cellWidth * 0.55)) : 0
                    cellHeight: home.extendedView ? Math.round(cellWidth * 0.5 + 44 + stripHeight + 24)
                                                  : Math.round(cellWidth * 1.2 + 44)
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
                        onPageActivated: function(pageNo) { app.openSearchHitAt(model.path, home.lib.searchQuery, pageNo) }
                        onPassageActivated: function(passage) {
                            app.openSearchHitInPassage(model.path, home.lib.searchQuery, passage)
                        }
                        width: libraryGrid.cellWidth
                        height: libraryGrid.cellHeight
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
                              : app.library.folder !== "" ? qsTr("This folder is empty")
                              : qsTr("Your library is empty")
                    }
                    Label {
                        visible: !home.searching
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: "#80868b"
                        text: qsTr("Drop PDFs and Xournal files here, import them, or create a new document.")
                    }
                    // (one below the other when they do not fit side by side)
                    GridLayout {
                        visible: !home.searching
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
                    cellWidth: Math.floor(width / columns)
                    cellHeight: Math.round(cellWidth * 1.2 + 44)
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
                        width: recentGrid.cellWidth
                        height: recentGrid.cellHeight
                        active: home.visible
                        row: index
                        selected: model.selected
                        selectionMode: app.recent.selectionCount > 0
                        highlighted: GridView.isCurrentItem && recentGrid.activeFocus
                        subtitle: home.formatDate(model.opened) + " · " + model.location
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
        }
    }

    // Search in the whole library, and the extended search: in the header, or in a row of its own when narrow
    RowLayout {
        id: searchGroup
        parent: home.narrow ? narrowSearchSlot : searchSlot
        anchors.fill: parent
        spacing: 6
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            radius: 22
            color: "#ffffff"
            border.width: searchField.activeFocus ? 2 : 1
            border.color: searchField.activeFocus ? Material.accentColor : "#c9ccd1"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 4
                ToolButton {
                    objectName: "librarySearchButton"
                    implicitWidth: 36; implicitHeight: 36
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
                    background: null
                    selectByMouse: true
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        x: parent.leftPadding
                        width: parent.width - parent.leftPadding - parent.rightPadding  // (a narrow field: …)
                        elide: Text.ElideRight
                        visible: parent.text === "" && parent.preeditText === ""
                        text: qsTr("Search documents and folders")
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
                FuzzyToggle { objectName: "librarySearchFuzzy" }
                // The reduced search: names only (of documents, and of folders unless the list is flat)
                ToolButton {
                    id: namesOnly
                    objectName: "searchNamesOnly"
                    text: qsTr("Names")
                    checkable: true
                    checked: home.lib.namesOnly
                    onToggled: home.lib.namesOnly = checked
                    implicitHeight: 36
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
                    visible: searchField.text !== ""
                    implicitWidth: 36; implicitHeight: 36
                    icon.source: app.iconUrl("xqt-close")
                    icon.color: "#3c4043"
                    display: AbstractButton.IconOnly
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
            // A breadcrumb (a folder above)
            const c = mapToItem(crumbRow, p.x, p.y)
            const crumb = crumbRow.childAt(c.x, c.y)
            if (crumb && crumb.folder !== undefined && crumb.folder !== app.library.folder) {
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

    Menu {
        id: itemMenu
        objectName: "homeItemMenu"
        MenuItem {
            text: home.menuMany ? qsTr("Open %1").arg(home.countText(home.menuPaths.length))
                                : home.menuKind === "library" ? qsTr("Open library")
                                : home.menuFolder ? qsTr("Open folder") : qsTr("Open")
            onTriggered: home.openAll(home.menuPaths, home.menuModel)
        }
        MenuItem {
            objectName: "openAsReferenceItem"
            text: qsTr("Open as reference")
            // Beside the document open now (without one it is simply opened)
            visible: !home.menuMany && !home.menuFolder && app.tabs.count > 0
                     && ["notes", "pdf", "md", "image", "text"].indexOf(home.menuKind) >= 0
            height: visible ? implicitHeight : 0
            onTriggered: app.openAsReference(home.menuPath)
        }
        MenuItem {
            objectName: "openAsLibraryItem"
            text: app.libraryWindows ? qsTr("Open as library (new window)") : qsTr("Open as library")
            visible: !home.menuMany && home.menuFolder && home.menuModel === app.library
            height: visible ? implicitHeight : 0
            onTriggered: app.openLibraryAt(home.menuPath)
        }
        MenuItem {
            objectName: "selectItem"
            text: qsTr("Select")
            visible: !home.menuMany && home.menuModel && home.menuModel.selectionCount === 0 && home.menuKind !== "library"
            height: visible ? implicitHeight : 0
            onTriggered: home.menuModel.toggleSelected(home.menuRow)
        }
        MenuItem {
            objectName: "renameItem"
            text: qsTr("Rename…")
            visible: !home.menuMany && home.menuKind !== "library"
            height: visible ? implicitHeight : 0
            onTriggered: renameDialog.open()
        }
        MenuItem {
            objectName: "copyToItem"
            text: qsTr("Copy to…")
            enabled: app.library.available
            visible: home.menuKind !== "library"
            height: visible ? implicitHeight : 0
            onTriggered: home.askTransfer(home.menuPaths, true)
        }
        MenuItem {
            objectName: "moveToItem"
            text: qsTr("Move to…")
            enabled: app.library.available
            visible: home.menuKind !== "library"
            height: visible ? implicitHeight : 0
            onTriggered: home.askTransfer(home.menuPaths, false)
        }
        MenuItem {
            text: qsTr("Show in its folder")
            visible: !home.menuMany && home.menuModel === app.library && (home.searching || app.library.flat) && !home.menuFolder
            height: visible ? implicitHeight : 0
            onTriggered: {
                searchField.text = ""
                app.library.searchQuery = ""
                app.library.flat = false
                app.library.folder = app.library.relativeFolder(home.menuPath.substring(0, home.menuPath.lastIndexOf("/")))
            }
        }
        MenuItem {
            objectName: "openWithSystemAppItem"
            text: qsTr("Open externally")
            // Markdown, text and other files, images: in the app the system has for them (not notes and PDFs)
            readonly property string file: !home.menuMany && ["md", "image", "text", "other"].indexOf(home.menuKind) >= 0
                                           ? app.externalFileOf(home.menuPath) : ""
            visible: file !== ""
            height: visible ? implicitHeight : 0
            onTriggered: app.openWithSystemApp(file)
        }
        MenuItem {
            objectName: "copyLinkItem"
            text: qsTr("Copy link")
            // A link to the document, to paste into notes (qt/docs/links.md)
            visible: !home.menuMany && !home.menuFolder && home.menuKind !== "library" && home.menuKind !== "other"
            height: visible ? implicitHeight : 0
            onTriggered: app.copyDocumentLink(home.menuPath)
        }
        MenuItem {
            objectName: "shareCardItem"
            text: qsTr("Share…")
            visible: !home.menuMany && !home.menuFolder && ["pdf", "notes", "md", "text"].indexOf(home.menuKind) >= 0
            height: visible ? implicitHeight : 0
            onTriggered: home.shareRequested(home.menuPath)
        }
        MenuItem {
            objectName: "showInFileManagerItem"
            text: qsTr("Show in file manager")
            visible: !home.menuMany && app.canShowInFileManager
            height: visible ? implicitHeight : 0
            onTriggered: app.showInFileManager(home.menuPath)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Remove from this list")
            visible: home.menuModel === app.recent
            height: visible ? implicitHeight : 0
            onTriggered: app.recent.removePaths(home.menuPaths)
        }
        MenuItem {
            objectName: "trashItem"
            text: qsTr("Move to trash…")
            visible: home.menuKind !== "library"  // (a library is never trashed from the Recent grid)
            height: visible ? implicitHeight : 0
            onTriggered: home.askTrash(home.menuModel, home.menuPaths)
        }
    }

    Dialog {
        id: renameDialog
        objectName: "renameDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: home.menuFolder ? qsTr("Rename folder") : qsTr("Rename document")
        width: Math.min(parent ? parent.width * 0.9 : 440, 440)
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

    Dialog {
        id: folderNameDialog
        objectName: "folderNameDialog"
        property int row: -1
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("New folder")
        width: Math.min(parent ? parent.width * 0.9 : 440, 440)
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

    // "New Markdown file" / "New text file": its name (made in the current folder and opened to write in)
    Dialog {
        id: textFileDialog
        objectName: "textFileDialog"
        property string extension: ".md"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: extension === ".md" ? qsTr("New Markdown file") : qsTr("New text file")
        width: Math.min(parent ? parent.width * 0.9 : 440, 440)
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
        onAccepted: app.createTextFile(textFileField.text, extension)
    }

    // Where to copy / move documents and folders: a folder of this library or of another one.
    Dialog {
        id: transferDialog
        objectName: "transferDialog"
        property var paths: []
        property bool copy: false
        property var libraries: []
        property string targetRoot: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: (copy ? qsTr("Copy %1 to") : qsTr("Move %1 to"))
                   .arg(paths.length === 1 ? "“" + paths[0].substring(paths[0].lastIndexOf("/") + 1) + "”" : home.countText(paths.length))
        width: Math.min(parent ? parent.width * 0.9 : 480, 480)
        height: Math.min(parent ? parent.height * 0.85 : 560, 580)
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

    Dialog {
        id: trashDialog
        objectName: "trashDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Move to trash?")
        width: Math.min(parent ? parent.width * 0.9 : 460, 460)
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
    Dialog {
        id: storageAccessDialog
        objectName: "storageAccessDialog"
        property string folder: ""  // then opened as library ("": the folder picker)
        function ask(path) { folder = path; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Allow access to your files?")
        width: Math.min(parent ? parent.width - 32 : 520, 520)
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

    Dialog {
        id: newLibraryDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("New library")
        width: Math.min(parent ? parent.width * 0.9 : 460, 460)
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
    Dialog {
        id: libraryArchiveDialog
        objectName: "libraryArchiveDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(540, parent ? parent.width - 32 : 540)
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
    Dialog {
        id: libraryArchiveProgress
        objectName: "libraryArchiveProgress"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: false
        closePolicy: Popup.NoAutoClose
        width: Math.min(460, parent ? parent.width - 32 : 460)
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
    Dialog {
        id: libraryArchiveSummary
        objectName: "libraryArchiveSummary"
        property var summary: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, parent ? parent.width - 32 : 560)
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

    FolderDialog {
        id: openLibraryDialog
        title: qsTr("Open a folder as library")
        onAccepted: app.openLibrary(selectedFolder)
    }

    FileDialog {
        id: importDialog
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
        title: qsTr("Import a folder (with its subfolders) into the library")
        onAccepted: {
            const dir = selectedFolder, folder = app.library.flat || home.searching ? "" : app.library.folder
            home.confirmImport(app.library.temporary, function() { app.library.importUrls([dir], folder) })
        }
    }

    Dialog {
        id: temporaryImportDialog
        objectName: "temporaryImportDialog"
        property var action: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Import into Downloads?")
        width: Math.min(parent ? parent.width * 0.9 : 520, 520)
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
    Dialog {
        id: conflictDialog
        objectName: "conflictDialog"
        property string documentPath: ""
        property var items: []  // app.library.conflictsOf: the document first, then its conflict copies
        function show(path) {
            documentPath = path
            items = app.library.conflictsOf(path)
            if (items.length > 1) open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Sync conflict")
        width: Math.min(parent ? parent.width - 32 : 560, 560)
        height: Math.min(implicitHeight, parent ? parent.height - 64 : 800)
        standardButtons: Dialog.Close
        contentItem: Flickable {
            implicitHeight: conflictColumn.implicitHeight
            contentHeight: conflictColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: conflictColumn
                width: parent.width
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
                                    if (!d.original && d.app) parts.push(qsTr("Conflict copy of %1").arg(d.app))
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
    }
    // Keep one: without a trash (Android), what goes is deleted - asked first
    Dialog {
        id: conflictConfirm
        objectName: "conflictConfirm"
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
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete %1?").arg(goes)
        width: Math.min(parent ? parent.width - 32 : 480, 480)
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: conflictConfirm.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("There is no trash on this device: %1 is deleted and cannot be brought back.").arg(conflictConfirm.goes)
        }
        onAccepted: resolve()
    }

    Dialog {
        id: errorDialog
        property alias text: errorLabel.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("That did not work")
        width: Math.min(parent ? parent.width * 0.9 : 520, 520)
        standardButtons: Dialog.Ok
        Label { id: errorLabel; width: errorDialog.availableWidth; wrapMode: Text.Wrap }
    }

    Snackbar {
        id: importedNote
        z: 30
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
    }
}
