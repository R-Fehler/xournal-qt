// xournal-qt: the toolbox's menus (qt/docs/toolbox.md; part of the main window, Main.qml): ⋯ of the floating
// toolbox, a tool's menu, the catalog ("+"), the tool editor, and the helpers that open a menu or a dialog once
// a phone's menu sheet has gone.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    id: toolboxMenus
    anchors.fill: parent
    visible: false
    readonly property alias toolboxMoreMenu: toolboxMoreMenu
    readonly property alias toolEditor: toolEditor
    readonly property alias toolEntryMenu: toolEntryMenu
    readonly property alias toolTypeMenu: toolTypeMenu
    /// Opens a menu from an entry of another one: on a phone once the sheet of that one has gone
    function openAfterMenus(menu) {
        if (!menuSheet.visible) {
            Popups.openAt(menu)
            return
        }
        const then = function() {
            menuSheet.closed.disconnect(then)
            Popups.openAt(menu)
        }
        menuSheet.closed.connect(then)
    }
    // ⋯ of the floating toolbox (full screen, presenting): what the tab strip and the command bar hold in a window
    AdaptiveMenu {
        id: toolboxMoreMenu
        objectName: "toolboxMoreMenu"
        AdaptiveMenuItem {
            objectName: "toolboxPresentItem"
            text: app.presenting ? qsTr("Stop presenting (Esc)") : qsTr("Present") + win.keyNote("present")
            icon.source: app.iconUrl("xopp-presentation-mode")
            onTriggered: app.presenting ? (app.presenting = false) : win.startPresenting()
        }
        AdaptiveMenuItem {
            objectName: "toolboxPresentCleanItem"
            text: (app.presenting ? qsTr("Hide the tools") : qsTr("Present without controls")) + win.keyNote("presentClean")
            icon.source: app.iconUrl("xqt-eye-off")
            onTriggered: app.presenting ? (win.presentClean = true) : win.startPresenting(true)
        }
        // Read only: the pen does not write, the edges turn the pages (qt/docs/zen.md)
        AdaptiveMenuItem {
            objectName: "toolboxReadOnlyItem"
            text: qsTr("Read only")
            icon.source: app.iconUrl("xqt-lock")
            checkable: true
            checked: win.readOnlyOn
            onTriggered: win.readOnly = !win.readOnlyOn
        }
        // Zen: only the page and the dot (qt/docs/zen.md)
        AdaptiveMenuItem {
            objectName: "toolboxZenItem"
            offered: !app.presenting  // (presenting: "Hide the tools" above)
            text: win.withKeys(qsTr("Zen (only the page)"), "zen")
            icon.source: app.iconUrl("xqt-zen")
            onTriggered: win.setZen(true)
        }
        AdaptiveMenuItem {
            objectName: "toolboxSearchItem"
            text: qsTr("Search") + win.keyNote("find")
            icon.source: app.iconUrl("xqt-search")
            onTriggered: searchBar.openBar()
        }
        AdaptiveMenuItem {
            objectName: "toolboxSettingsItem"
            text: qsTr("Settings")
            icon.source: app.iconUrl("xqt-settings")
            onTriggered: settingsPage.open()
        }
        MenuSeparator {}
        AdaptiveMenuItem {
            objectName: "toolboxLeaveFullScreenItem"
            text: qsTr("Leave full screen") + (app.presenting ? "" : qsTr(" (Esc)"))
            icon.source: app.iconUrl("xopp-fullscreen")
            onTriggered: win.fullScreenMode = false
        }
        // What the top bar holds (full screen hides it; qt/top-bar): its items in its order, the members of a group one
        // by one, without what is above already; and New (the tab strip's "+" is not shown in full screen)
        MenuSeparator {}
        Repeater {
            model: toolboxMenus.topBarCommands()
            delegate: AdaptiveMenuItem {
                required property var modelData
                readonly property Item button: modelData.app !== undefined ? toolArea.slots[modelData.app] || null : null
                readonly property bool isTool: modelData.app === undefined
                objectName: "toolboxMore_" + (isTool ? modelData.id : modelData.app)
                offered: isTool || (button !== null && button.offered !== false)
                text: isTool ? win.toolEntryName(modelData) : button ? (button.label !== "" ? button.label : button.name) : ""
                icon.source: isTool ? "" : button && button.iconName !== "" ? app.iconUrl(button.iconName) : ""
                checkable: !isTool && button !== null && ["hand", "select", "snip", "pdfText", "geometry", "touchDrawing", "write"].indexOf(modelData.app) >= 0
                checked: isTool ? toolboxPane.inHand(modelData) : button !== null && button.checked === true
                onTriggered: {
                    const b = button, e = modelData
                    if (isTool) app.applyToolEntry(e.id)
                    else Qt.callLater(function() { b.clicked() })
                }
            }
        }
        AdaptiveMenuItem {
            objectName: "toolboxNewItem"
            text: win.withKeys(qsTr("New document"), "newDocument")
            icon.source: app.iconUrl("xopp-document-new")
            onTriggered: app.newDocument()
        }
    }
    /// What the top bar holds, for full screen's ⋯: its items in order (the members of a group one by one; no dividers),
    /// without those ⋯ has of its own (present, Zen, search, settings, full screen)
    function topBarCommands() {
        const own = ["present", "zen", "search", "settings", "fullScreen"]
        const out = []
        const list = (app.toolbox.revision, app.toolbox.top)
        for (let i = 0; i < list.length; ++i) {
            const e = list[i]
            const items = e.group === true ? e.members : [e]
            for (let j = 0; j < items.length; ++j) {
                const m = items[j]
                if (m.divider === true || (m.app !== undefined && own.indexOf(m.app) >= 0)) continue
                out.push(m)
            }
        }
        return out
    }
    // A tool's editor: a tap on the tool in hand, Edit in its menu, "+" (qt/docs/toolbox.md, "Editing a tool")
    ToolEntryEditor {
        id: toolEditor
        ownerOf: function(id) { return toolboxPane.buttonFor(id) || topBarPane.buttonFor(id) }
    }
    /// The bar that shows an item of the arrangement (the rail or the top bar)
    function paneOf(id) { return app.toolbox.barOf(id) === "top" ? topBarPane : toolboxPane }
    // A tool's menu (a long press, a right click): edit, move, replace, duplicate, add one here, a divider, to the other
    // bar, remove; for an app item (hand, select, open, …): its own list, move, add one here, a divider, to the other
    // bar, off the bars (into the catalog); for a group: its list, ungroup, …
    AdaptiveMenu {
        id: toolEntryMenu
        objectName: "toolEntryMenu"
        titleShown: true
        property var entry: ({})
        property Item button: null
        readonly property string entryId: entry && entry.id ? entry.id : ""
        readonly property var store: app.toolbox
        /// An app item's (its button: the window's); a group's (qt/docs/toolbox.md, "Groups"); else a tool's
        readonly property bool isApp: entry && entry.app !== undefined
        readonly property bool isGroup: entry && entry.group === true
        readonly property bool isTool: !isApp && !isGroup
        readonly property Item appButton: isApp ? toolArea.slots[entry.app] || null : null
        /// The bar it is on ("rail", "top") and that bar
        readonly property string bar: (store.revision, entryId !== "" ? store.barOf(entryId) : "rail")
        readonly property Item pane: bar === "top" ? topBarPane : toolboxPane
        function openFor(e, b, pos) {
            entry = e
            button = b
            title = win.toolEntryName(e)
            openMenu(pos, b)
        }
        // (an app item with a long press of its own: select's kinds, the snips' resolution, how PDF text is marked, the
        // templates of a new page, present without controls, …: on a bar the hold lifts it, so its own is here)
        AdaptiveMenuItem {
            objectName: "toolOptionsItem"
            offered: toolEntryMenu.appButton !== null && toolEntryMenu.appButton.ownHold === true
            text: toolEntryMenu.appButton && toolEntryMenu.appButton.holdText !== undefined ? toolEntryMenu.appButton.holdText
                                                                                            : qsTr("Options…")
            icon.source: app.iconUrl("xqt-more")
            onTriggered: {
                const b = toolEntryMenu.appButton
                toolboxMenus.afterMenus(function() { b.pressAndHold() })
            }
        }
        // A group: its list, and back to its tools one by one
        AdaptiveMenuItem {
            objectName: "toolGroupListItem"
            offered: toolEntryMenu.isGroup
            text: qsTr("Its tools…")
            icon.source: app.iconUrl("xqt-tools-more")
            onTriggered: {
                const id = toolEntryMenu.entryId, b = toolEntryMenu.button
                const pane = toolEntryMenu.pane
                toolboxMenus.afterMenus(function() { pane.openGroup(id, b) })
            }
        }
        AdaptiveMenuItem {
            objectName: "toolUngroupItem"
            offered: toolEntryMenu.isGroup
            text: qsTr("Ungroup")
            icon.source: app.iconUrl("xqt-column-remove")
            onTriggered: toolEntryMenu.store.ungroup(toolEntryMenu.entryId)
        }
        AdaptiveMenuItem {
            objectName: "toolEditItem"
            offered: toolEntryMenu.isTool
            text: qsTr("Edit…")
            icon.source: app.iconUrl("xqt-pencil")
            onTriggered: {
                const e = toolEntryMenu.entry, b = toolEntryMenu.button, edge = toolEntryMenu.pane.edge
                toolboxMenus.afterMenus(function() { toolEditor.openFor(app.toolbox.entry(e.id), b, edge) })
            }
        }
        AdaptiveMenuItem {
            objectName: "toolMoveEarlierItem"
            text: toolEntryMenu.pane.vertical ? qsTr("Move up") : qsTr("Move left")
            icon.source: app.iconUrl(toolEntryMenu.pane.vertical ? "xqt-chevron-up" : "xqt-chevron-left")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.store.canMoveBy(toolEntryMenu.entryId, -1))
            onTriggered: toolEntryMenu.store.moveBy(toolEntryMenu.entryId, -1)
        }
        AdaptiveMenuItem {
            objectName: "toolMoveLaterItem"
            text: toolEntryMenu.pane.vertical ? qsTr("Move down") : qsTr("Move right")
            icon.source: app.iconUrl(toolEntryMenu.pane.vertical ? "xqt-chevron-down" : "xqt-chevron-right")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.store.canMoveBy(toolEntryMenu.entryId, 1))
            onTriggered: toolEntryMenu.store.moveBy(toolEntryMenu.entryId, 1)
        }
        AdaptiveMenuItem {
            objectName: "toolReplaceItem"
            offered: toolEntryMenu.isTool
            text: qsTr("Replace with…")
            icon.source: app.iconUrl("xqt-rotate-right")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.entry.type !== "eraser" || toolEntryMenu.store.canRemove(toolEntryMenu.entryId))
            onTriggered: { const id = toolEntryMenu.entryId, b = toolEntryMenu.button; toolboxMenus.afterMenus(function() { toolTypeMenu.ask("replace", id, b) }) }
        }
        AdaptiveMenuItem {
            objectName: "toolDuplicateItem"
            offered: toolEntryMenu.isTool
            text: qsTr("Duplicate")
            icon.source: app.iconUrl("xqt-copy")
            onTriggered: toolEntryMenu.store.duplicate(toolEntryMenu.entryId)
        }
        AdaptiveMenuItem {
            objectName: "toolAddHereItem"
            text: qsTr("Add a tool here…")
            icon.source: app.iconUrl("xqt-plus")
            onTriggered: {
                const id = toolEntryMenu.entryId, b = toolEntryMenu.button, bar = toolEntryMenu.bar
                toolboxMenus.afterMenus(function() { toolTypeMenu.ask("addHere", id, b, bar) })
            }
        }
        // To the other bar, at its end (carrying it there by hand does the same)
        AdaptiveMenuItem {
            objectName: "toolOtherBarItem"
            text: toolEntryMenu.bar === "top" ? qsTr("Move to the rail") : qsTr("Move to the top bar")
            icon.source: app.iconUrl(toolEntryMenu.bar === "top" ? "xqt-columns" : "xqt-panel-top")
            onTriggered: {
                const other = toolEntryMenu.bar === "top" ? "rail" : "top"
                toolEntryMenu.store.moveTo(toolEntryMenu.entryId, other, toolEntryMenu.store.items(other).length)
            }
        }
        AdaptiveMenuItem {
            objectName: "toolDividerItem"
            readonly property bool has: (toolEntryMenu.store.revision, toolEntryMenu.store.hasDividerAfter(toolEntryMenu.entryId))
            text: has ? qsTr("Remove the divider after it") : qsTr("Add a divider after it")
            icon.source: app.iconUrl("xqt-column-remove")
            onTriggered: toolEntryMenu.store.setDividerAfter(toolEntryMenu.entryId, !has)
        }
        MenuSeparator {}
        AdaptiveMenuItem {
            objectName: "toolRemoveItem"
            text: toolEntryMenu.isGroup ? qsTr("Remove the group") : toolEntryMenu.isApp ? qsTr("Off the bars (into +)")
                  : (toolEntryMenu.store.revision, toolEntryMenu.store.canRemove(toolEntryMenu.entryId))
                  ? qsTr("Remove") : qsTr("Remove (the last eraser stays)")
            icon.source: app.iconUrl("xqt-close")
            enabled: (toolEntryMenu.store.revision, toolEntryMenu.store.canRemove(toolEntryMenu.entryId))
            onTriggered: toolEntryMenu.store.remove(toolEntryMenu.entryId)
        }
    }
    // The catalog ("+" at the end of either bar, "Add a tool here…" after an item; qt/top-bar): a new tool of a kind
    // (the editor makes it), and every app tool and command on neither bar, by section; a tap puts it at the end of the
    // bar it was opened from (or after the item). "Replace with…": the kinds alone. On a phone a sheet.
    AdaptiveMenu {
        id: toolTypeMenu
        objectName: "toolTypeMenu"
        titleShown: true
        /// "add", "addHere", "replace"
        property string purpose: "add"
        property string entryId: ""
        property Item button: null
        /// The bar it adds to: "rail", "top"
        property string bar: "rail"
        function ask(why, id, b, toBar) {
            purpose = why
            entryId = id
            button = b
            bar = toBar === "top" ? "top" : "rail"
            title = why === "replace" ? qsTr("Replace with") : bar === "top" ? qsTr("Add to the top bar") : qsTr("Add to the rail")
            openMenu(undefined, b)
        }
        /// Where an item goes on the bar: after the item it was asked from, else at the end (-1)
        function place() { return purpose === "addHere" ? app.toolbox.indexOf(entryId) + 1 : -1 }
        readonly property string edge: bar === "top" ? "top" : toolboxPane.edge
        function chosen(type) {
            const store = app.toolbox
            const b = button
            if (purpose === "replace") {
                const fresh = store.prefill(type)
                if (store.replace(entryId, fresh)) {
                    if (type !== "sticky" && type !== "snip") app.applyToolEntry(entryId)
                    const id = entryId, edge = toolboxMenus.paneOf(id).edge
                    toolboxMenus.afterMenus(function() { toolEditor.openFor(store.entry(id), b, edge) })
                }
                return
            }
            const at = place(), toBar = bar, edge = toolTypeMenu.edge
            toolboxMenus.afterMenus(function() { toolEditor.openNew(type, at, b, edge, toBar) })
        }
        /// An app item on neither bar, put on this one
        function placeItem(name) {
            const store = app.toolbox
            const id = store.place(name, bar, place())
            if (id !== "") Qt.callLater(function() { const p = toolboxMenus.paneOf(id); p.reveal(p.buttonFor(id)) })
        }
        // (a new tool)
        AdaptiveMenuItem {
            objectName: "catalogSection_new"
            offered: toolTypeMenu.purpose !== "replace"
            enabled: false
            text: qsTr("A new tool")
        }
        Repeater {
            model: [{ type: "pen", icon: "xopp-tool-pencil", name: qsTr("Pen") },
                    { type: "highlighter", icon: "xopp-tool-highlighter", name: qsTr("Highlighter") },
                    { type: "shape", icon: "xqt-shapes", name: qsTr("Shape (line, arrow, rectangle, …)") },
                    { type: "eraser", icon: "xopp-tool-eraser", name: qsTr("Eraser") },
                    { type: "text", icon: "xqt-text-box", name: qsTr("Text box") },
                    { type: "sticky", icon: "xqt-sticky-note", name: qsTr("Sticky note") },
                    { type: "laser", icon: "xopp-laser-pointer", name: qsTr("Laser pointer") },
                    { type: "snip", icon: "xqt-snip", name: qsTr("Snip (a picture of a rectangle or lasso to copy)") }]
            delegate: AdaptiveMenuItem {
                required property var modelData
                objectName: "toolType_" + modelData.type
                offered: !win.textDoc || toolTypeMenu.purpose === "replace"
                text: modelData.name
                icon.source: app.iconUrl(modelData.icon)
                onTriggered: toolTypeMenu.chosen(modelData.type)
            }
        }
        // Every app tool and command on neither bar, by section (a section's title, then its items)
        Repeater {
            model: toolTypeMenu.purpose === "replace" ? [] : toolboxMenus.catalogRows()
            delegate: AdaptiveMenuItem {
                required property var modelData
                readonly property bool heading: modelData.section !== undefined
                readonly property Item button: heading ? null : toolArea.slots[modelData.name] || null
                objectName: heading ? "catalogSection_" + modelData.section : "catalog_" + modelData.name
                enabled: !heading
                text: heading ? modelData.title : button ? (button.label !== "" ? button.label : button.name) : ""
                icon.source: !heading && button && button.iconName !== "" ? app.iconUrl(button.iconName) : ""
                onTriggered: if (!heading) toolTypeMenu.placeItem(modelData.name)
            }
        }
    }
    /// The catalog's rows of the app's items on neither bar (and offered here): [{section, title}, {name}, …]
    function catalogRows() {
        const sections = [
            { section: "tools", title: qsTr("Tools"), names: ["hand", "select", "snip", "pdfText", "write", "geometry", "touchDrawing"] },
            { section: "insert", title: qsTr("Insert"), names: ["image", "sticker", "addPage", "record"] },
            { section: "view", title: qsTr("View"), names: ["search", "read", "replay", "present", "fullScreen", "zen"] },
            { section: "document", title: qsTr("Document"), names: ["new", "open", "save", "milestone", "share", "print",
                                                                    "tags", "favourite", "bookmark", "settings"] }
        ]
        const free = (app.toolbox.revision, app.toolbox.unplaced())
        const out = []
        sections.forEach(function(sec) {
            const names = sec.names.filter(function(n) {
                const b = toolArea.slots[n]
                return free.indexOf(n) >= 0 && b && b.offered !== false
            })
            if (names.length === 0) return
            out.push({ section: sec.section, title: sec.title })
            names.forEach(function(n) { out.push({ name: n }) })
        })
        return out
    }
    /// Runs `then` once the menus (and a phone's menu sheet) have gone: a dialog or editor opened from a menu entry
    function afterMenus(then) {
        if (!menuSheet.visible) {
            Qt.callLater(then)
            return
        }
        const after = function() {
            menuSheet.closed.disconnect(after)
            then()
        }
        menuSheet.closed.connect(after)
    }
    /// A carried item made a group: "Grouped · Undo"
    function grouped(before) {
        snackbar.show(qsTr("Grouped"), false, qsTr("Undo"), function() { app.toolbox.restore(before) })
    }
    /// A carried item let go away from both bars left them: "Removed · Undo" (an app item: "… is in + now")
    function leftTheBars(entry, before) {
        const text = entry && entry.app !== undefined ? qsTr("%1 is in + now").arg(toolEntryName(entry))
                                                       : qsTr("Removed: %1").arg(toolEntryName(entry))
        snackbar.show(text, false, qsTr("Undo"), function() { app.toolbox.restore(before) })
    }
}
