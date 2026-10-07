// The sticker picker (qt/docs/features/stickers.md): the stickers of the library's Stickers folder ("This library") or
// of the app-wide set ("All libraries") as a grid of covers, by folder, found by name, sorted by last use, the own
// order, the name or the date added. A tap pastes the sticker on the current page (and puts it on the clipboard),
// selected; the card's menu (press and hold, right click) renames, reorders, moves, opens, copies and deletes it. "+
// Save selection" makes a sticker of what is selected. A bottom sheet in the phone classes; elsewhere it opens beside
// `owner` (none: in the middle of the window).
//
// With `mode: "templates"` it is the page template picker (qt/docs/features/templates.md): the library's Templates
// folder and the app-wide set; a tap adds the template's page at `insertAt` (or, `pickOnly`, only says which:
// `chosen`), and "+ Save this page" saves the current page as a template. The object names begin with "template" then
// ("templateChooser" with `pickOnly`).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

Popup {
    id: picker
    // (open: Android's back key is its; Zen's Back waits, qt/top-bar)
    onOpenedChanged: if (typeof win !== "undefined" && win && win.takeBack !== undefined) win.takeBack(opened)
    objectName: key + "Picker"
    /// "stickers" or "templates"
    property string mode: "stickers"
    readonly property bool templates: mode === "templates"
    /// The start of its object names (the choosers of the dialogs have their own)
    readonly property string key: templates ? (pickOnly ? "templateChooser" : "template") : "sticker"
    /// Templates: where a chosen template's page goes (a page index; -1: after the current page)
    property int insertAt: -1
    /// Templates: a tap only chooses one (chosen), adding nothing (the Insert pages and New document dialogs)
    property bool pickOnly: false
    /// A sticker or template was chosen (its file)
    signal chosen(string path)
    /// The item it opens beside (not a sheet), and where in its coordinates
    property Item owner: null
    property real ownerX: 0
    property real ownerY: 0
    readonly property var model: templates ? app.templates : app.stickers
    /// A bottom sheet in the phone classes (Main.qml's sheet geometry)
    readonly property bool asSheet: typeof win !== "undefined" && win !== null && win.layout.phoneLayout === true
    /// Something is selected that can become a sticker (asked when it opens)
    property bool canSave: false
    parent: asSheet || !owner ? Overlay.overlay : owner
    modal: asSheet || !owner
    dim: asSheet
    x: asSheet ? win.insets.sheetX : owner ? ownerX : Math.round((parent.width - width) / 2)
    y: asSheet ? win.insets.sheetBottom - height : owner ? ownerY : Math.round((parent.height - height) / 2)
    width: asSheet ? win.insets.sheetWidth : 400
    height: asSheet ? Math.min(560, Math.round((win.insets.sheetBottom - win.insets.top) * 0.85)) : 480
    margins: asSheet ? 0 : 8
    padding: 8
    bottomPadding: asSheet ? 8 + win.insets.sheetBottomPadding : 8
    focus: true
    closePolicy: asSheet || !owner ? Popup.CloseOnEscape | Popup.CloseOnPressOutside
                                   : Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        color: "#ffffff"
        radius: picker.asSheet ? 16 : 12
        border.width: picker.asSheet ? 0 : 1
        border.color: "#d5d8dc"
        Rectangle {  // (a sheet: square at the bottom)
            visible: picker.asSheet
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
    }
    Shortcut {
        sequence: "Back"
        enabled: picker.opened
        onActivated: picker.close()
    }
    onOpened: {
        search.text = ""
        model.refresh()
        canSave = templates ? !pickOnly && app.templateDraft(app.pageNumber - 1, false).offered
                            : app.stickerDraft(false).offered
    }
    /// Templates: open it to add a template's page before page index `position` (-1: after the current page)
    function openToInsert(position) {
        insertAt = position
        open()
    }

    /// Paste a sticker, add a template's page (the picker closes first)
    function choose(path) {
        close()
        chosen(path)
        if (templates) {
            if (pickOnly) return
            if (!app.canInsertTemplate) {
                app.pageActionDone(qsTr("No pages can be added here (the document is open for reading only)"), false)
                return
            }
            app.insertTemplate(path, insertAt, 1)
            return
        }
        if (!app.canPasteSticker) {
            app.pageActionDone(qsTr("Stickers cannot be pasted here (the document is open for reading only)"), false)
            return
        }
        app.pasteSticker(path)
    }
    /// "+ Save selection" ("+ Save this page")
    function saveSelection() {
        close()
        if (templates) {
            // (the window's dialog: one for the page menus, the add-page button's list and this)
            if (typeof win !== "undefined" && win) win.openTemplateSave(app.pageNumber - 1)
            return
        }
        if (!saveLoader.item.openForSelection())
            app.pageActionDone(qsTr("Select something on the page first: it becomes the sticker"), false)
    }

    Loader {
        id: saveLoader
        active: !picker.templates
        sourceComponent: StickerSaveDialog { parent: Overlay.overlay }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            TabBar {
                id: scopes
                Layout.fillWidth: true
                currentIndex: picker.model.scope === "app" ? 1 : 0
                TabButton {
                    objectName: picker.key + "ScopeLibrary"
                    text: qsTr("This library")
                    enabled: picker.model.hasLibrary
                    onClicked: picker.model.scope = "library"
                }
                TabButton {
                    objectName: picker.key + "ScopeApp"
                    text: qsTr("All libraries")
                    onClicked: picker.model.scope = "app"
                }
            }
            Button {
                objectName: picker.key + "SaveSelection"
                text: picker.templates ? qsTr("+ Save this page") : qsTr("+ Save selection")
                flat: true
                visible: !picker.pickOnly
                enabled: picker.canSave
                onClicked: picker.saveSelection()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            TextField {
                id: search
                objectName: picker.key + "Search"
                Layout.fillWidth: true
                placeholderText: picker.templates ? qsTr("Search templates") : qsTr("Search stickers")
                onTextChanged: picker.model.search = text
                Keys.onReturnPressed: if (picker.model.count > 0) picker.choose(picker.model.pathAt(0))
                Keys.onEnterPressed: if (picker.model.count > 0) picker.choose(picker.model.pathAt(0))
            }
            ComboBox {
                id: sortBox
                objectName: picker.key + "Sort"
                readonly property var keys: ["used", "own", "name", "added"]
                model: [qsTr("Last used"), qsTr("Own order"), qsTr("Name"), qsTr("Date added")]
                currentIndex: Math.max(0, keys.indexOf(picker.model.sort))
                onActivated: function(index) { picker.model.sort = keys[index] }
                implicitWidth: 140
            }
        }
        // The folders of the set (lectures, topics): "All", then each
        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            visible: picker.model.folders.length > 0
            contentWidth: chips.width
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            Row {
                id: chips
                spacing: 4
                Repeater {
                    model: [""].concat(picker.model.folders)
                    delegate: Button {
                        required property string modelData
                        objectName: picker.key + "FolderChip_" + modelData
                        text: modelData === "" ? qsTr("All") : modelData
                        checkable: true
                        checked: picker.model.folder === modelData
                        flat: !checked
                        height: 34
                        font.pixelSize: 13
                        onClicked: picker.model.folder = modelData
                    }
                }
            }
        }
        GridView {
            id: grid
            objectName: picker.key + "Grid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            readonly property int columns: picker.asSheet ? 3 : Math.max(3, Math.floor(width / 120))
            cellWidth: Math.floor(width / columns)
            cellHeight: cellWidth + 22
            model: picker.model
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: card
                required property string name
                required property string path
                required property string folder
                required property string preview
                required property int index
                objectName: picker.key + "Card_" + name
                width: grid.cellWidth
                height: grid.cellHeight
                focusPolicy: Qt.NoFocus
                Accessible.name: name
                ToolTip.visible: hovered && folder !== ""
                ToolTip.text: folder + "/" + name
                ToolTip.delay: 600
                onClicked: picker.choose(path)
                onPressAndHold: cardMenu.openFor(card)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: cardMenu.openFor(card)
                }
                background: Rectangle {
                    radius: 10
                    color: card.pressed ? "#eceef1" : card.hovered ? "#f4f5f7" : "transparent"
                }
                contentItem: Column {
                    spacing: 2
                    Rectangle {
                        width: card.width - 8
                        height: card.width - 8
                        x: 4
                        radius: 8
                        color: "#f7f8fa"
                        border.width: 1
                        border.color: "#e3e5e8"
                        Image {
                            anchors.fill: parent
                            anchors.margins: 6
                            source: card.preview
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 240
                            smooth: true
                        }
                    }
                    Label {
                        width: card.width - 4
                        x: 2
                        text: card.name
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 12
                        color: "#303030"
                    }
                }
            }
        }
        Label {
            objectName: picker.key + "Empty"
            visible: picker.model.count === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#5f6368"
            text: picker.templates
                  ? (!picker.model.setEmpty ? qsTr("No template found")
                     : picker.model.scope === "app"
                       ? qsTr("No templates in all libraries yet. Save a page with “In all libraries”, or copy one here from a library (its menu).")
                       : qsTr("No templates yet. Choose “Save page as template…” in the page's menu (⋮ › Page), or “+ Save this page” here."))
                  : !picker.model.setEmpty ? qsTr("No sticker found")
                  : picker.model.scope === "app"
                    ? qsTr("No stickers in all libraries yet. Save one with “In all libraries”, or copy one here from a library (its menu).")
                    : qsTr("No stickers yet. Select something on a page (ink, text, pictures, notes) and choose “Save as sticker…” in its pill, or “+ Save selection” here.")
        }
    }

    // The card's menu
    AdaptiveMenu {
        id: cardMenu
        objectName: picker.key + "CardMenu"
        property string path: ""
        property string name: ""
        readonly property string scope: path !== "" ? picker.model.scopeOf(path) : ""
        title: name
        titleShown: true
        function openFor(card) {
            path = card.path
            name = card.name
            Popups.openAt(cardMenu)
        }
        AdaptiveMenuItem {
            objectName: picker.key + "Rename"
            text: qsTr("Rename…")
            onTriggered: ask.openFor("rename", cardMenu.path, cardMenu.name)
        }
        AdaptiveMenuItem {
            objectName: picker.key + "MoveUp"
            text: qsTr("Move up (own order)")
            onTriggered: picker.model.moveBy(cardMenu.path, -1)
        }
        AdaptiveMenuItem {
            objectName: picker.key + "MoveDown"
            text: qsTr("Move down (own order)")
            onTriggered: picker.model.moveBy(cardMenu.path, 1)
        }
        AdaptiveMenuItem {
            objectName: picker.key + "MoveToFolder"
            text: qsTr("Move to folder…")
            onTriggered: ask.openFor("folder", cardMenu.path, "")
        }
        AdaptiveMenuItem {
            objectName: picker.key + "Open"
            text: qsTr("Open (to change it)")
            onTriggered: { picker.close(); app.openPath(cardMenu.path) }
        }
        AdaptiveMenuItem {
            objectName: picker.key + "CopyOther"
            offered: picker.model.hasLibrary
            text: cardMenu.scope === "app" ? qsTr("Copy to this library") : qsTr("Copy to all libraries")
            onTriggered: {
                if (picker.model.copyToOtherSet(cardMenu.path))
                    app.pageActionDone(picker.templates
                                       ? (cardMenu.scope === "app" ? qsTr("Copied to this library's templates")
                                                                   : qsTr("Copied to the templates of all libraries"))
                                       : (cardMenu.scope === "app" ? qsTr("Copied to this library's stickers")
                                                                   : qsTr("Copied to the stickers of all libraries")), false)
            }
        }
        AdaptiveMenuItem {
            objectName: picker.key + "CopyToLibrary"
            text: qsTr("Copy to library…")
            onTriggered: ask.openFor("library", cardMenu.path, "")
        }
        AdaptiveMenuItem {
            objectName: picker.key + "Delete"
            text: qsTr("Delete")
            icon.source: app.iconUrl("xqt-delete")
            onTriggered: picker.model.remove(cardMenu.path)
        }
    }

    // A name, a folder or a library for the card's menu
    AdaptiveDialog {
        id: ask
        objectName: picker.key + "AskDialog"
        parent: Overlay.overlay
        kind: "card"
        preferredWidth: 400
        property string what: ""
        property string path: ""
        property var libraries: []
        function openFor(kind, file, text) {
            what = kind
            path = file
            field.text = text
            if (kind === "library") {
                libraries = app.libraries().filter(function(l) { return !l.current })
                libraryBox.currentIndex = 0
            }
            open()
        }
        title: what === "rename" ? (picker.templates ? qsTr("Rename template") : qsTr("Rename sticker"))
               : what === "folder" ? qsTr("Move to folder") : qsTr("Copy to library")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: if (what !== "library") { field.forceActiveFocus(); field.selectAll() }
        onAccepted: {
            let ok = false
            if (what === "rename") ok = picker.model.rename(path, field.text)
            else if (what === "folder") ok = picker.model.moveToFolder(path, field.text.trim())
            else if (libraries.length > 0) ok = picker.model.copyToLibrary(path, libraries[libraryBox.currentIndex].path)
            if (!ok) app.pageActionDone(qsTr("That did not work (the name may be taken)"), false)
            else if (what === "library")
                app.pageActionDone(picker.templates ? qsTr("Copied to the library's templates")
                                                    : qsTr("Copied to the library's stickers"), false)
        }
        ColumnLayout {
            width: ask.availableWidth
            TextField {
                id: field
                objectName: picker.key + "AskField"
                visible: ask.what !== "library"
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: ask.what === "folder" ? qsTr("Folder (empty: no folder)") : ""
                Keys.onReturnPressed: ask.accept()
                Keys.onEnterPressed: ask.accept()
            }
            ComboBox {
                id: libraryBox
                objectName: picker.key + "LibraryBox"
                visible: ask.what === "library"
                Layout.fillWidth: true
                model: ask.libraries.map(function(l) { return l.name })
            }
            Label {
                visible: ask.what === "library" && ask.libraries.length === 0
                text: qsTr("There is no other library.")
                color: "#5f6368"
            }
        }
    }
}
