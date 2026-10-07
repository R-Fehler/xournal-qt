// xournal-qt: the home screen's header: the switch's, the breadcrumbs' and the search's slots, New, Quick note,
// Import, New folder, Flat, Show, Sort, the size of the cards (expanded), or "+" and View (grouped), Settings.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Flickable {
    id: headerFlick
    // (what the other parts use)
    readonly property alias switchSlot: switchSlot
    readonly property alias headerCrumbSlot: headerCrumbSlot
    readonly property alias searchSlot: searchSlot
    readonly property alias resume: resume
    readonly property alias newButton: newButton
    readonly property alias newMenu: newMenu
    objectName: "homeHeader"
    visible: home.selectionCount === 0
    Layout.fillWidth: true
    Layout.topMargin: home.shortLayout ? 4 : 10
    Layout.preferredHeight: headerRow.implicitHeight
    contentWidth: headerRow.width + headerRow.x + 8
    contentHeight: headerRow.implicitHeight
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentWidth > width + 1
    clip: true
    RowLayout {
        id: headerRow
        x: home.phoneLayout ? 12 : 16
        // (the search field and the breadcrumbs give way down to their minimum before the row scrolls)
        width: Math.max(headerFlick.width - x - 8, implicitWidth
                        - (searchSlot.visible ? searchSlot.Layout.preferredWidth - searchSlot.Layout.minimumWidth : 0)
                        - (headerCrumbSlot.visible ? headerCrumbSlot.Layout.preferredWidth - headerCrumbSlot.Layout.minimumWidth : 0))
        height: headerFlick.height
        spacing: home.phoneLayout ? 4 : 6

        // The switch: the library's name with its ▾, Recent, Favourites, Bookmarks (switchBox); on a phone upright
        // it takes what the row leaves (the name elided)
        Item {
            id: switchSlot
            readonly property real smallest: switchBox.implicitWidth - switchBox.libraryTab.implicitWidth + 64
            Layout.fillWidth: home.portraitPhone
            Layout.minimumWidth: home.portraitPhone ? smallest : switchBox.implicitWidth
            Layout.preferredWidth: home.portraitPhone ? smallest : switchBox.implicitWidth
            Layout.preferredHeight: switchBox.implicitHeight
        }
        // Where we are in the library (crumbBar): here on a phone held sideways, else in a row of its own below
        Item {
            id: headerCrumbSlot
            visible: home.shortLayout && home.page === 0 && app.library.available && crumbBar.needed
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.preferredWidth: 320
            Layout.preferredHeight: 44
        }

        Item { Layout.fillWidth: true; visible: !headerCrumbSlot.visible && !home.portraitPhone }

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

        Item { Layout.fillWidth: true; visible: !headerCrumbSlot.visible && !home.portraitPhone }

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
            label: qsTr("New")
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
                    objectName: "quickNoteItem"
                    text: qsTr("Quick note")
                    icon.source: app.iconUrl("xqt-zap")
                    offered: newMenu.grouped
                    onTriggered: app.quickNote()
                }
                AdaptiveMenuItem {
                    objectName: "newDocumentItem"
                    text: qsTr("New document…")
                    icon.source: app.iconUrl("xqt-notebook-pen")
                    onTriggered: newDocumentDialog.open()
                }
                // A text document: a PDF text document or a Markdown file, as Settings → Documents says
                // (qt/docs/features/md-pdf.md)
                AdaptiveMenuItem {
                    objectName: "newMarkdownItem"
                    text: app.newTextAsPdf ? qsTr("New text document…") : qsTr("New Markdown file…")
                    icon.source: app.iconUrl("xqt-markdown")
                    enabled: app.library.available
                    onTriggered: { libraryDialogs.textFileDialog.extension = app.newTextAsPdf ? ".pdf" : ".md"; libraryDialogs.textFileDialog.open() }
                }
                AdaptiveMenuItem {
                    objectName: "newTextItem"
                    text: qsTr("New text file…")
                    icon.source: app.iconUrl("xqt-file-text")
                    enabled: app.library.available
                    onTriggered: { libraryDialogs.textFileDialog.extension = ".txt"; libraryDialogs.textFileDialog.open() }
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
                    onTriggered: importDialogs.importDialog.open()
                }
                AdaptiveMenuItem {
                    objectName: "addImportFolderItem"
                    text: qsTr("Import a folder with its subfolders…")
                    icon.source: app.iconUrl("xqt-folder-input")
                    offered: newMenu.grouped && home.page === 0 && app.library.available
                    onTriggered: importDialogs.importFolderDialog.open()
                }
                AdaptiveMenuItem {
                    objectName: "addNewFolderItem"
                    text: qsTr("New folder…")
                    icon.source: app.iconUrl("xqt-folder-plus")
                    offered: newMenu.grouped && home.page === 0 && app.library.available
                    enabled: !app.library.flat && !home.searching
                    onTriggered: { libraryDialogs.folderNameDialog.row = -1; libraryDialogs.folderNameDialog.open() }
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
        // Quick note (qt/docs/features/quick-note.md): a new note in the library's Inbox, named by the date and time
        // (or a line in today's Markdown note there); a button of its own when expanded, else first in "+"
        IconButton {
            objectName: "quickNoteButton"
            label: qsTr("Quick note")
            visible: home.expanded
            iconName: "xqt-zap"
            tip: qsTr("Quick note in the library's Inbox (%1)").arg(home.quickNoteKeys)
            onClicked: app.quickNote()
        }
        IconButton {
            objectName: "importButton"
            label: qsTr("Import")
            visible: home.expanded && home.page === 0 && app.library.available
            iconName: "xqt-import"
            tip: qsTr("Import PDFs and Xournal files, or a whole folder (copies them into this folder)")
            onClicked: Popups.openAt(importMenu)
            AdaptiveMenu {
                id: importMenu
                objectName: "importMenu"
                AdaptiveMenuItem { objectName: "importFilesItem"; text: qsTr("Import files…"); onTriggered: importDialogs.importDialog.open() }
                AdaptiveMenuItem { objectName: "importFolderItem"; text: qsTr("Import a folder with its subfolders…"); onTriggered: importDialogs.importFolderDialog.open() }
            }
        }
        IconButton {
            objectName: "newFolderButton"
            label: qsTr("New folder")
            visible: home.expanded && home.page === 0 && app.library.available
            enabled: !app.library.flat && !home.searching
            iconName: "xqt-folder-plus"
            tip: qsTr("New folder")
            onClicked: { libraryDialogs.folderNameDialog.row = -1; libraryDialogs.folderNameDialog.open() }
        }
        IconButton {
            objectName: "flatButton"
            label: app.library.flat ? qsTr("Show folders") : qsTr("All documents at once")
            visible: home.expanded && home.page === 0 && app.library.available
            iconName: app.library.flat ? "xqt-layout-grid" : "xqt-folder-tree"
            tip: app.library.flat ? qsTr("All documents (show folders)") : qsTr("Folders (show all documents at once)")
            checked: app.library.flat
            onClicked: app.library.flat = !app.library.flat
        }
        // Which kinds of files the library shows (a setting of the library), marked when not the default
        IconButton {
            id: showButton
            objectName: "showButton"
            label: qsTr("Show")
            visible: home.expanded && (home.page === 0 || home.page >= 2) && app.library.available
            iconName: "xqt-filter"
            tip: app.library.showFiltered ? qsTr("Show: some kinds of files are hidden or added") : qsTr("Show: which kinds of files")
            checked: app.library.showFiltered
            onClicked: Popups.openAt(showPopup)
            LibraryShowMenu { id: showPopup; objectName: "showPopup"; prefix: "show"; heading: true }
        }
        IconButton {
            objectName: "sortButton"
            label: qsTr("Sort")
            visible: home.expanded && home.page === 0 && app.library.available
            iconName: "xqt-sort"
            tip: qsTr("Sort")
            onClicked: Popups.openAt(sortMenu)
            LibrarySortMenu { id: sortMenu; objectName: "sortMenu"; prefix: "sort" }
        }
        IconButton {
            objectName: "openFileButton"
            label: qsTr("Open a file")
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
            enabled: home.page < 2 && (home.page === 0 ? libraryPage.libraryGrid : recentPage.recentGrid).columns < 12
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
            enabled: home.page < 2 && (home.page === 0 ? libraryPage.libraryGrid : recentPage.recentGrid).columns > home.fewestColumns(home.extendedView && home.page === 0)
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
            label: qsTr("View")
            visible: !home.expanded
            implicitWidth: home.phoneLayout ? 44 : 48
            iconName: "xqt-sliders"
            tip: qsTr("View: which files, sorting, size of the cards")
            // (marked while it shows less than everything)
            checked: app.library.available && home.page !== 1
                     && (app.library.showFiltered || (home.page === 0 && app.library.flat))
            onClicked: Popups.openAt(viewMenu)
            AdaptiveMenu {
                id: viewMenu
                objectName: "homeViewMenu"
                title: qsTr("View")
                AdaptiveMenuItem {
                    objectName: "viewFlatItem"
                    text: qsTr("All documents at once (no folders)")
                    offered: home.page === 0 && app.library.available
                    checkable: true
                    checked: app.library.flat
                    onTriggered: app.library.flat = checked
                }
                LibraryShowMenu {
                    objectName: "viewShowMenu"
                    prefix: "viewShow"
                    offered: home.page !== 1 && app.library.available
                }
                LibrarySortMenu {
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
                    property bool offered: home.page < 2
                    visible: offered
                    height: offered ? implicitHeight : 0
                }
                // The size of the cards: − and + (Ctrl+wheel and pinch too); the menu stays open
                RowLayout {
                    objectName: "viewCardSizeRow"
                    property bool offered: home.page < 2
                    visible: offered
                    height: offered ? implicitHeight : 0
                    width: parent ? parent.width : implicitWidth
                    readonly property var grid: home.page === 0 ? libraryPage.libraryGrid : recentPage.recentGrid
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
            label: qsTr("Settings")
            implicitWidth: home.phoneLayout ? 44 : 48
            iconName: "xqt-settings"
            tip: qsTr("Settings") + win.keyNote("settings")
            onClicked: home.settingsRequested()
        }
    }
}
