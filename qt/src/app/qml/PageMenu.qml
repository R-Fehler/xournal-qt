// What can be done with a page, from the sidebar or the page grid: it acts on the selection when the page is
// selected, else on the page itself. The everyday actions are a small grid of icons, every one with its tool tip,
// so the whole thing stays short; only what an icon cannot tell is written out. Inserting somewhere else, or
// several pages at once, is in the "Insert pages…" dialog (it asks where, how many, background and size). Extracting,
// splitting, pictures of the pages and pages from a file are in PageFiles.qml (the window's).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: menu
    objectName: "pageMenu"
    parent: Overlay.overlay
    padding: 6
    focus: true  // (Esc, Android's back: closes it)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // In the phone classes a bottom sheet (as the menus, AdaptiveMenu): across the bottom, with a handle to drag it
    // away, the page dimmed; the buttons as big as a finger needs (qt/docs/features/adaptive-layout.md, "Menus")
    readonly property var adaptiveLayout: typeof win !== "undefined" && win ? win.adaptive : null
    property bool asSheet: false
    readonly property int target: adaptiveLayout && adaptiveLayout.touchProfile ? adaptiveLayout.minTarget : 44
    readonly property real safeBottom: typeof win !== "undefined" && win && win.insets.sheetBottomPadding ? win.insets.sheetBottomPadding : 0
    modal: asSheet
    dim: asSheet
    bottomPadding: asSheet ? 12 + safeBottom : 6

    property int page: 0
    /// The document was shown beside itself at the page (the page grid closes then)
    signal referenced()
    // (the selection's count: read again when the selection changes, also for the same page)
    readonly property var pages: (app.pages.selectionCount, app.pages.isSelected(page) ? app.pages.selectedPages() : [page])
    readonly property string what: pages.length > 1 ? qsTr("%1 pages").arg(pages.length) : qsTr("page")
    readonly property int lastPage: pages[pages.length - 1]
    /// Turning them (AppController::rotationOf): possible, and why not (PDF pages in a .xopp)
    readonly property var rotation: visible ? app.rotationOf(pages) : ({})

    /// Open it where the page was pressed (`x`, `y` in the coordinates of `item`), and keep it inside the window.
    /// In the phone classes: a sheet at the bottom instead.
    function openFor(p, item, x, y) {
        page = p
        asSheet = adaptiveLayout !== null && adaptiveLayout.phoneLayout
        if (asSheet) {
            handle.offset = 0
            // (the window's sheet: inside the safe area, on the soft keyboard while it is open)
            menu.width = Qt.binding(function() { return win.insets.sheetWidth })
            menu.x = Qt.binding(function() { return win.insets.sheetX })
            menu.y = Qt.binding(function() { return win.insets.sheetBottom - menu.height + handle.offset })
        } else {
            menu.width = Qt.binding(function() { return menu.implicitWidth })
            // (bindings: its size is known only once it is laid out)
            const at = item.mapToItem(Overlay.overlay, x, y)
            menu.x = Qt.binding(function() { return Math.max(8, Math.min(at.x, Overlay.overlay.width - menu.width - 8)) })
            menu.y = Qt.binding(function() { return Math.max(8, Math.min(at.y, Overlay.overlay.height - menu.height - 8)) })
        }
        open()
    }

    background: Rectangle {
        radius: menu.asSheet ? 16 : 12
        color: "#ffffff"
        border.width: menu.asSheet ? 0 : 1
        border.color: "#d5d8dc"
        Rectangle {  // (a sheet: square at the bottom, where it rests on the window's edge)
            visible: menu.asSheet
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
    }

    component PageAction: IconButton {
        implicitWidth: menu.target
        implicitHeight: menu.target
        icon.width: 21
        icon.height: 21
    }
    component PageLine: ItemDelegate {
        Layout.fillWidth: true
        implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
        font.pixelSize: 14
    }

    ColumnLayout {
        width: parent ? parent.width : implicitWidth  // (a sheet: as wide as the window)
        spacing: 2
        MenuSheetHandle {
            id: handle
            objectName: "pageMenuHandle"
            visible: menu.asSheet
            Layout.fillWidth: true
            onDismissed: menu.close()
        }
        GridLayout {
            columns: 6
            columnSpacing: 0
            rowSpacing: 0
            Layout.alignment: Qt.AlignHCenter
            PageAction {
                objectName: "pageMenuCopy"
                iconName: "xopp-edit-copy"
                tip: qsTr("Copy %1").arg(menu.what)
                onClicked: { app.copyPages(menu.pages); menu.close() }
            }
            PageAction {
                iconName: "xopp-edit-cut"
                tip: qsTr("Cut %1").arg(menu.what)
                onClicked: { app.cutPages(menu.pages); menu.close() }
            }
            PageAction {
                objectName: "pageMenuPaste"
                iconName: "xopp-edit-paste"
                tip: app.copiedPages > 1 ? qsTr("Paste %1 pages after").arg(app.copiedPages) : qsTr("Paste after")
                enabled: app.copiedPages > 0
                onClicked: { app.pastePages(menu.lastPage + 1); menu.close() }
            }
            PageAction {
                iconName: "xqt-duplicate"
                tip: qsTr("Duplicate %1").arg(menu.what)
                onClicked: { app.duplicatePages(menu.pages); menu.close() }
            }
            PageAction {
                objectName: "pageMenuDelete"
                iconName: "xopp-page-delete"
                tip: qsTr("Delete %1").arg(menu.what)
                enabled: menu.pages.length < app.pages.count
                onClicked: { app.deletePages(menu.pages); menu.close() }
            }
            PageAction {
                iconName: "xopp-page-add"
                tip: qsTr("Insert a page after this one")
                onClicked: { app.insertPageAfter(menu.lastPage); menu.close() }
            }
            PageAction {
                objectName: "pageMenuUp"
                iconName: "xqt-chevron-up"
                tip: qsTr("Move %1 up").arg(menu.what)
                enabled: menu.pages[0] > 0
                onClicked: { app.movePages(menu.pages, menu.pages[0] - 1); menu.close() }
            }
            PageAction {
                iconName: "xqt-chevron-down"
                tip: qsTr("Move %1 down").arg(menu.what)
                enabled: menu.lastPage < app.pages.count - 1
                onClicked: { app.movePages(menu.pages, menu.lastPage + 2); menu.close() }
            }
            PageAction {
                iconName: "xopp-document-print"
                tip: menu.pages.length > 1 ? qsTr("Print %1 pages…").arg(menu.pages.length)
                                           : qsTr("Print this page…")
                onClicked: { app.requestPrint(menu.pages); menu.close() }
            }
            PageAction {
                iconName: "xqt-link"
                tip: qsTr("Copy a link to this page")
                onClicked: { app.copyPageLink(menu.page); menu.close() }
            }
            // The document beside itself (qt/self-reference), at this page: a second view with its own scrolling
            // Writing space beside the slide on this page (or the selected pages): qt/docs/features/note-space.md
            PageAction {
                objectName: "pageMenuNoteSpace"
                iconName: "xqt-note-space"
                tip: qsTr("Space for notes beside the slide…")
                enabled: menu.visible && app.noteSpaceOf(menu.page).possible === true
                onClicked: { app.requestNoteSpace(menu.pages, false); menu.close() }
            }
            PageAction {
                objectName: "pageMenuShowBeside"
                iconName: "xqt-reference"
                tip: qsTr("Show this document beside, at this page")
                onClicked: { app.reference.showBeside(menu.page); menu.close(); menu.referenced() }
            }
        }
        Hairline { Layout.fillWidth: true; color: "#e2e5e9" }
        // The page's background and its size, side by side (the menu stays short)
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            PageLine {
                text: menu.pages.length > 1 ? qsTr("Background of %1 pages…").arg(menu.pages.length)
                                            : qsTr("Background…")
                onClicked: { app.requestPageBackground(menu.pages); menu.close() }
            }
            // Another paper size (PageSizeDialog): for the page, or the selection
            PageLine {
                objectName: "pageMenuPageSize"
                Layout.fillWidth: false
                text: qsTr("Size…")
                enabled: menu.visible && app.pageSizeOf(menu.page).possible === true
                onClicked: { app.requestPageSize(menu.pages); menu.close() }
            }
            // A bookmark on the page (qt/docs/features/bookmarks.md): tap to add it (named after the page's first
            // heading or its PDF chapter, else "Page N"); on a bookmarked page: its name, to rename or remove it
            PageAction {
                id: bookmarkAction
                objectName: "pageMenuBookmark"
                visible: app.canBookmark
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                readonly property string mark: (app.bookmarks, menu.visible ? app.bookmarkOf(menu.page) : "")
                iconName: mark !== "" ? "xqt-bookmark-filled" : "xqt-bookmark"
                icon.color: "transparent"
                tip: mark !== "" ? qsTr("Bookmark: %1 (rename or remove)").arg(mark) : qsTr("Bookmark this page")
                onClicked: {
                    if (mark !== "") bookmarkDialog.openFor(menu.page)
                    else app.toggleBookmark(menu.page)
                    menu.close()
                }
            }
        }
        // Inserting pages, and a quarter turn of the page or the selection (qt/docs/features/page-rotation.md; why not:
        // below)
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            PageLine {
                objectName: "insertPagesItem"
                text: qsTr("Insert pages…")
                onClicked: { app.requestInsertPages(menu.lastPage + 1); menu.close() }
            }
            // Pages from a file after this one (PageFiles.qml, qt/docs/features/page-files.md)
            PageAction {
                objectName: "pageMenuInsertFile"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-import"
                tip: qsTr("Insert pages from a file after this one…")
                enabled: app.canInsertTemplate
                onClicked: { win.actions.openPageFiles("insert", [menu.lastPage]); menu.close() }
            }
            PageAction {
                objectName: "pageMenuRotateLeft"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-rotate-left"
                tip: qsTr("Rotate %1 left").arg(menu.what)
                enabled: menu.rotation.possible === true
                onClicked: { app.rotatePages(menu.pages, false); menu.close() }
            }
            PageAction {
                objectName: "pageMenuRotateRight"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-rotate-right"
                tip: qsTr("Rotate %1 right").arg(menu.what)
                enabled: menu.rotation.possible === true
                onClicked: { app.rotatePages(menu.pages, true); menu.close() }
            }
        }
        Label {
            objectName: "pageMenuRotateReason"
            visible: (menu.rotation.reason || "") !== ""
            text: menu.rotation.reason || ""
            Layout.fillWidth: true
            Layout.preferredWidth: 1  // (as wide as the menu, not wider: it wraps)
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: "#5f6368"
        }
        // Its preview in the library and in the overview of open documents
        PageLine {
            objectName: "titlePageItem"
            visible: app.titlePage >= 0
            text: app.titlePage === menu.page ? qsTr("✓ The title page") : qsTr("Make it the title page")
            enabled: app.titlePage !== menu.page
            onClicked: { app.setTitlePage(menu.page); menu.close() }
        }
        // A chapter starting here; the page saved to be added again (qt/docs/features/templates.md)
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            PageLine {
                text: qsTr("Start a chapter here…")
                onClicked: { app.requestChapter(menu.page); menu.close() }
            }
            PageAction {
                objectName: "pageMenuSaveTemplate"
                visible: typeof win !== "undefined" && win !== null
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-file-plus"
                tip: qsTr("Save page as template…")
                onClicked: { win.actions.openTemplateSave(menu.page); menu.close() }
            }
            // The page as a high-resolution picture on the clipboard (qt/docs/features/page-files.md)
            PageAction {
                objectName: "pageMenuCopyImage"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-copy"
                tip: menu.pages.length > 1 ? qsTr("Copy the first page as an image (%1 dpi)").arg(app.pageImageDpi)
                                           : qsTr("Copy as an image (%1 dpi)").arg(app.pageImageDpi)
                onClicked: { app.copyPagesAsImage(menu.pages); menu.close() }
            }
        }
        // Selecting all; the page or the selection as files of their own (PageFiles.qml,
        // qt/docs/features/page-files.md)
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            PageLine {
                text: qsTr("Select all pages")
                onClicked: { app.pages.selectAll(); menu.close() }
            }
            PageAction {
                objectName: "pageMenuExtract"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-file-output"
                tip: qsTr("Extract %1 to a new document…").arg(menu.what)
                onClicked: { win.actions.openPageFiles("extract", menu.pages); menu.close() }
            }
            PageAction {
                objectName: "pageMenuSplit"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-page-break"
                tip: menu.pages.length > 1 ? qsTr("Split the document at the selected pages…")
                                           : qsTr("Split the document…")
                onClicked: { win.actions.openPageFiles("split", menu.pages); menu.close() }
            }
            PageAction {
                objectName: "pageMenuImages"
                implicitHeight: menu.asSheet ? Math.max(48, menu.target) : 38
                iconName: "xqt-file-image"
                tip: qsTr("Export %1 as pictures…").arg(menu.what)
                onClicked: { win.actions.openPageFiles("images", menu.pages); menu.close() }
            }
        }
    }
    BookmarkDialog { id: bookmarkDialog }
}
