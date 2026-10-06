// The toolbox (qt/docs/toolbox.md): the user's own tools in a rail docked to a side of the canvas (right by default;
// left, top or bottom by choice, per window size), like pens lying sorted in a box on the table. One element in the
// window, in full screen and on phones.
//   head   undo, redo (pinned)
//   middle the user's tools, in sections (dividers between them); then the fixed tools (hand, select, write on the
//          page, setsquare and curtain, mark PDF text, the finger draws: the window's own buttons, lent to the rail)
//   tail   "+" (add a tool), ⋯ (full screen: what the tab strip and the command bar hold there) (pinned)
// The rail uses all the room it has: every tool on its own while they fit; when they do not, sections fold into stacks
// (ToolboxPlan.js): the fixed tools first, then the user's sections from the end; only when all are folded the middle
// scrolls. A stack shows the entry used last of its section; a tap uses it, a tap again or a long press opens the
// section's list. The entry in hand is always shown.
// A tap on a tool picks it up (AppController::applyToolEntry); a tap on the one in hand opens its editor; a long press
// or a right click its menu; the wheel over it changes its width.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "ToolboxPlan.js" as ToolboxPlan
import "Popups.js" as Popups
import "DevicePixels.js" as DevicePixels

Rectangle {
    id: box
    objectName: "toolbox"
    /// The edge it is docked to: "left", "right", "top", "bottom"
    property string edge: "right"
    readonly property bool vertical: edge === "left" || edge === "right"
    /// Floating over the page (full screen): rounded, a little off the edge
    property bool floating: false
    /// ⋯ at its end (full screen: present, leave full screen, search, settings)
    property bool moreShown: false
    /// The phone's dock (qt/docs/toolbox.md, "On a phone"): undo, redo, the first tools that fit (the one in hand
    /// always among them), "My tools" (the sheet with all of them, the fixed tools and "+") and the page number
    property bool compact: false
    /// The buttons of the fixed tools (the window's own, lent to the rail while it is shown), in their order
    property var fixedButtons: []
    /// The size of a tool's cell: a finger's target in the touch profile
    readonly property real cell: win.adaptive.touchProfile ? win.adaptive.minTarget : 44
    /// Its thickness across the edge
    readonly property real thickness: cell + 8
    /// Room at its ends that is not its own (a navigation bar under a rail's end, a cut-out)
    property real startInset: 0
    property real endInset: 0
    /// Where the page is, seen from a button (the entry in hand lifts towards it)
    readonly property string towardsPage: edge === "right" ? "left" : edge === "left" ? "right" : edge === "top" ? "down" : "up"
    /// The user's tools (ToolboxModel)
    readonly property var store: app.toolbox

    signal editRequested(var entry, Item button)
    signal menuRequested(var entry, Item button, point pos)
    signal addRequested(Item button)
    signal moreRequested(Item button)
    /// The phone's dock: "My tools" (the sheet of every tool) and the page number (all pages)
    signal allToolsRequested()
    signal pagesRequested()
    /// Its grip dragged: to another edge of the window (the window highlights the edge it would go to)
    signal gripMoved(point scenePos)
    signal gripDropped(point scenePos)

    color: "#ffffff"
    radius: floating ? 14 : 0
    border.width: floating ? 1 : 0
    border.color: "#d5d8dc"
    /// A line's thickness: a whole number of device pixels (one at 100 % to 175 %, two at 200 %; qt/docs/hidpi.md)
    readonly property real hair: DevicePixels.whole(1, Screen.devicePixelRatio)
    // The line towards the pages (docked)
    Rectangle {
        visible: !box.floating
        color: "#d5d8dc"
        x: box.edge === "left" ? parent.width - box.hair : 0
        y: box.edge === "top" ? parent.height - box.hair : 0
        width: box.vertical ? box.hair : parent.width
        height: box.vertical ? parent.height : box.hair
    }

    // --- what is shown ----------------------------------------------------------------------------------------------
    /// The user's sections ([[entry, …], …]) as the model has them now
    readonly property var sections: (store.revision, store.sections())
    /// The plan for the room there is (ToolboxPlan.plan)
    property var plan: ({ folded: [], fixedFolded: false, scroll: false })
    property var lastPlanInput: null
    readonly property real length: vertical ? height : width
    /// What the rail takes along its length besides the middle: its start (an inset, the grip), the head, the gaps, the
    /// tail, the inset at its end and the middle's own margin. None of it depends on the rail's length.
    readonly property real overhead: (vertical ? headGrid.y + headGrid.height : headGrid.x + headGrid.width) + 9 + 9
                                     + (vertical ? tailGrid.height : tailGrid.width) + 4 + endInset + 8
    /// The room the middle has for its cells as laid out now: the plan's length (qt/rail-fill: the plan followed the
    /// rail's length alone, 22 px more than the middle really had, and a change of the insets at one length — the
    /// navigation bar, the insets of a phone unfolded arriving after its new size — left the plan of before: the rail
    /// stayed folded with the room to spare, or cut its last cells off)
    readonly property real middleRoom: length - overhead
    readonly property var planKey: [middleRoom, cell, sections.map(function(s) { return s.length }).join(","),
                                    fixedButtons.length]
    onPlanKeyChanged: Qt.callLater(relayout)
    Component.onCompleted: { relayout(); syncItems() }
    function planInput(room) {
        return { length: room, cell: cell, divider: 9, head: 0, tail: 0,
                 sections: sections.map(function(s) { return s.length }), fixed: fixedButtons.length, slack: 0 }
    }
    /// The plan anew for the room there is: on every change of it (a resize, folding or unfolding the phone, turning
    /// it, the safe area, the keyboard, another edge). A rail that grows unfolds only with 16 px to spare, against
    /// flicker at an edge; nothing else keeps a plan of before.
    function relayout() {
        const input = planInput(middleRoom)
        let p = ToolboxPlan.plan(input)
        const last = lastPlanInput
        if (plan && plan.need !== undefined && last && last.sections.join() === input.sections.join()
                && last.fixed === input.fixed && last.cell === input.cell && input.length > last.length
                && ToolboxPlan.cells(input, p) > ToolboxPlan.cells(last, plan)) {
            const lean = ToolboxPlan.plan(Object.assign({}, input, { slack: 16 }))
            if (ToolboxPlan.cells(input, lean) <= ToolboxPlan.cells(last, plan) && plan.need <= input.length)
                p = plan
        }
        lastPlanInput = input
        plan = p
        placeFixed()
    }
    /// Its length with nothing folded (a floating rail is no longer than that)
    readonly property real naturalLength: ToolboxPlan.plan(planInput(1e9)).need + overhead
    /// The items of the middle part: entries, dividers, stacks
    readonly property var itemsNow: {
        const out = []
        const act = store.active
        if (compact) {
            // As many of the user's tools as fit, in their order; the one in hand replaces the last if it is further on
            const all = store.tools()
            const n = Math.max(0, Math.min(all.length, Math.floor(middleRoom / cell)))
            let shown = all.slice(0, n)
            const inHandAt = all.findIndex(function(e) { return e.id === act })
            if (n > 0 && inHandAt >= n) shown[n - 1] = all[inHandAt]
            for (let j = 0; j < shown.length; ++j) out.push({ kind: "entry", key: shown[j].id, entry: shown[j] })
            return out
        }
        const dividers = store.entries.filter(function(e) { return e.divider === true }).map(function(e) { return e.id })
        for (let i = 0; i < sections.length; ++i) {
            const s = sections[i]
            if (i > 0) out.push({ kind: "divider", key: "d" + i, id: dividers[i - 1] || "" })
            if (plan.folded && plan.folded[i]) {
                const ids = s.map(function(e) { return e.id })
                const shown = (act, store.recentAmong(ids))
                let entry = s[0]
                for (let j = 0; j < s.length; ++j) if (s[j].id === shown) entry = s[j]
                out.push({ kind: "stack", key: "s" + i, entry: entry, section: s })
            } else {
                for (let j = 0; j < s.length; ++j) out.push({ kind: "entry", key: s[j].id, entry: s[j] })
            }
        }
        return out
    }

    /// What the rail shows: `itemsNow`, taken only when what is where changes (an entry, a stack and the one it shows, a
    /// divider), not when a tool's color or width does. A new list makes the Repeater build every button anew, and a
    /// popup beside one (its editor, a stack's list) lost its place: it went to the window's corner (2026-10-05). The
    /// buttons read their entries from the store (`entryOf`).
    property var items: []
    property string itemsSignature: ""
    onItemsNowChanged: syncItems()
    function syncItems() {
        const sig = itemsNow.map(function(it) {
            return it.kind + ":" + it.key + ":" + (it.entry ? it.entry.id : "")
                   + (it.section ? ":" + it.section.map(function(e) { return e.id }).join(",") : "")
        }).join("|")
        if (sig === itemsSignature) return
        itemsSignature = sig
        items = itemsNow
    }
    /// An entry as the store has it now (the items keep the ids)
    function entryOf(e) { return (store.revision, e && e.id ? store.entry(e.id) : ({})) }
    /// The button that shows an entry now: its own, or the stack that holds it (null: none)
    function buttonFor(id) {
        const kids = middleGrid.children
        for (let i = 0; i < kids.length; ++i) {
            const it = kids[i].entryItem
            if (!it || !kids[i].item) continue
            if (it.kind === "entry" && it.entry.id === id) return kids[i].item
            if (it.kind === "stack" && it.section.some(function(e) { return e.id === id })) return kids[i].item
        }
        return null
    }

    // --- what an entry does ----------------------------------------------------------------------------------------
    /// Its name: "Pen · Body", "Highlighter · Key terms", "Arrow", "Eraser (whiteout)", …
    function entryName(e) {
        if (!e || !e.type) return ""
        const role = e.role ? roleName(e.role) : ""
        const kinds = {
            "pen": qsTr("Pen"), "highlighter": qsTr("Highlighter"), "text": qsTr("Text box"),
            "sticky": qsTr("Sticky note"), "laser": e.base === "highlighter" ? qsTr("Laser highlighter") : qsTr("Laser pointer")
        }
        if (e.type === "eraser")
            return e.variant === "whiteout" ? qsTr("Eraser (whiteout)") : e.variant === "deleteStroke"
                   ? qsTr("Eraser (whole strokes)") : qsTr("Eraser")
        if (e.type === "shape") {
            const v = win.toolGroups.variant("shape", e.variant)
            return role !== "" ? v.name + " · " + role : v.name
        }
        if (e.type === "snip") return win.toolGroups.variant("snip", e.variant === "lasso" ? "snipLasso" : "snipRect").name
        const kind = kinds[e.type] || e.type
        return role !== "" ? kind + " · " + role : kind
    }
    function roleName(key) {
        const palettes = app.colorPalettes
        for (let i = 0; i < palettes.length; ++i) {
            const roles = palettes[i].roles
            for (let j = 0; j < roles.length; ++j) if (roles[j].key === key) return roles[j].name
        }
        return key
    }
    /// The entry is the tool in hand (the active entry, and the tool is still its tool)
    /// (a snip is never the active entry: it is in hand while it is armed with its shape)
    function inHand(e) {
        return (app.tool, app.drawingType, app.snip, app.settings.revision, store.revision,
                !!e && (e.type === "snip" || store.active === e.id) && app.entryInHand(e))
    }
    function tap(e, button) {
        if (e && e.type === "snip") cycleSnip(e)
        else if (inHand(e)) editRequested(e, button)
        else app.applyToolEntry(e.id)
    }
    /// A snip (a cycling tool, ToolGroups' "snip"): a tap snips with its shape; a tap while it is armed takes the
    /// other shape, which the entry keeps (its editor: Edit… in its menu)
    function cycleSnip(e) {
        if (inHand(e)) store.update(e.id, { variant: e.variant === "lasso" ? "rect" : "lasso" })
        app.applyToolEntry(e.id)
    }
    /// The width one wheel step further (a fifth more or less), within 0.1–150 pt
    function stepWidth(e, steps) {
        if (!e || e.width === undefined) return
        const w = Math.max(0.1, Math.min(150, e.width * Math.pow(1.25, steps)))
        store.update(e.id, { width: Math.round(w * 100) / 100 })
        if (inHand(e) || store.active === e.id) app.applyToolEntry(e.id)
    }
    function openStack(item, section) {
        stackFlyout.section = section
        stackFlyout.owner = item
        stackFlyout.open()
    }

    // --- reordering: a tool held, then carried to another place (qt/docs/toolbox.md, "Reordering") -----------------
    /// The entry being carried ("": none), where it would go (an index among entries and dividers; -1: nowhere, it
    /// goes back), and the mark of that place (along the rail, in the tools' coordinates)
    property string dragId: ""
    property var dragEntry: ({})
    property int dropIndex: -1
    property real dropMark: -1
    property point dragPoint
    function indexOfItem(it) {
        if (it.kind === "entry") return store.indexOf(it.entry.id)
        if (it.kind === "stack") return store.indexOf(it.section[0].id)
        if (it.kind === "divider") return store.indexOf(it.id)
        return -1
    }
    function dragTo(e, scenePos) {
        dragId = e.id
        dragEntry = e
        const local = box.mapFromItem(null, scenePos.x, scenePos.y)
        dragPoint = local
        // Let go away from the rail: nothing changes (it goes back)
        const away = vertical ? (local.x < -cell * 1.5 || local.x > width + cell * 1.5)
                              : (local.y < -cell * 1.5 || local.y > height + cell * 1.5)
        if (away) {
            dropIndex = -1
            dropMark = -1
            return
        }
        // Near an end of a rail that scrolls: it scrolls along
        const m = middle.mapFromItem(null, scenePos.x, scenePos.y)
        const pos = vertical ? m.y : m.x, size = vertical ? middle.height : middle.width
        if (middle.interactive) {
            if (pos < cell / 2) scrollBy(-cell / 3)
            else if (pos > size - cell / 2) scrollBy(cell / 3)
        }
        const p = middleGrid.mapFromItem(null, scenePos.x, scenePos.y)
        const along = vertical ? p.y : p.x
        let best = -1, mark = 0
        const kids = middleGrid.children
        for (let i = 0; i < kids.length; ++i) {
            const k = kids[i]
            if (!k.entryItem || !k.visible) continue
            const start = vertical ? k.y : k.x
            const extent = vertical ? k.height : k.width
            if (along < start + extent / 2) {
                best = indexOfItem(k.entryItem)
                mark = start
                break
            }
            mark = start + extent
        }
        dropIndex = best >= 0 ? best : store.entries.length
        dropMark = mark
    }
    function scrollBy(d) {
        if (vertical) middle.contentY = Math.max(0, Math.min(middle.contentHeight - middle.height, middle.contentY + d))
        else middle.contentX = Math.max(0, Math.min(middle.contentWidth - middle.width, middle.contentX + d))
    }
    function dropAt(e, scenePos) {
        dragTo(e, scenePos)
        if (dropIndex >= 0) store.move(e.id, dropIndex)
        endDrag()
    }
    function endDrag() {
        dragId = ""
        dropIndex = -1
        dropMark = -1
    }

    // --- the fixed tools (the window's buttons, lent to the rail) -----------------------------------------------------
    /// The buttons placed in the rail now (given back when they leave the list)
    property var placedFixed: []
    /// How many of the fixed tools are shown on their own (the others are in the stack after them)
    readonly property int fixedShown: plan.fixedShown === undefined ? fixedButtons.length : plan.fixedShown
    function placeFixed() {
        for (let i = 0; i < placedFixed.length; ++i) {
            if (fixedButtons.indexOf(placedFixed[i]) < 0) release(placedFixed[i])
        }
        for (let i = 0; i < fixedButtons.length; ++i) {
            const b = fixedButtons[i]
            b.parent = i < fixedShown ? fixedGrid : fixedFlyoutGrid
            b.width = cell
            b.height = cell
        }
        placedFixed = fixedButtons.slice()
    }
    /// A button given back (the toolbox is not shown): its own size again; its owner puts it where it belongs
    function release(b) {
        b.width = Qt.binding(function() { return b.implicitWidth })
        b.height = Qt.binding(function() { return b.implicitHeight })
    }
    /// The fixed tool in use among those in the stack (the stack shows it)
    readonly property Item fixedInUse: {
        for (let i = fixedShown; i < fixedButtons.length; ++i) if (fixedButtons[i].checked) return fixedButtons[i]
        return null
    }

    // --- the layout ---------------------------------------------------------------------------------------------------
    // The grip (a dotted cap at its start): the only place that moves the rail itself, to another edge
    Item {
        id: grip
        objectName: "toolboxGrip"
        visible: !box.compact  // (a phone's dock stays where it is)
        x: box.vertical ? 0 : box.startInset
        y: box.vertical ? box.startInset : 0
        width: box.vertical ? box.width : 14
        height: box.vertical ? 14 : box.height
        Grid {
            anchors.centerIn: parent
            columns: box.vertical ? 3 : 2
            spacing: 3
            Repeater {
                model: 6
                delegate: Rectangle {
                    width: 3; height: 3; radius: 1.5
                    color: gripDrag.active || gripHover.hovered ? Material.accentColor : "#b4b8bd"
                }
            }
        }
        HoverHandler { id: gripHover; cursorShape: Qt.SizeAllCursor }
        DragHandler {
            id: gripDrag
            target: null
            onCentroidChanged: if (active) box.gripMoved(grip.mapToItem(null, centroid.position.x, centroid.position.y))
            property point last
            onActiveChanged: {
                if (active) return
                box.gripDropped(last)
            }
            onTranslationChanged: last = grip.mapToItem(null, centroid.position.x, centroid.position.y)
        }
        ToolTip.visible: gripHover.hovered && !gripDrag.active
        ToolTip.text: qsTr("Drag to another edge")
        ToolTip.delay: 600
    }
    Grid {
        id: headGrid
        objectName: "toolboxHead"
        columns: box.vertical ? 1 : -1
        rows: box.vertical ? -1 : 1
        spacing: 0
        x: box.vertical ? 4 : (grip.visible ? grip.x + grip.width : box.startInset + 4)
        y: box.vertical ? (grip.visible ? grip.y + grip.height : box.startInset + 4) : 4
        IconButton {
            objectName: "toolboxUndoButton"
            width: box.cell; height: box.cell
            icon.width: 22; icon.height: 22
            iconName: "xopp-edit-undo"
            label: qsTr("Undo")
            tip: win.withKeys(qsTr("Undo"), "undo")
            enabled: app.canUndo
            onClicked: app.undo()
        }
        IconButton {
            objectName: "toolboxRedoButton"
            width: box.cell; height: box.cell
            icon.width: 22; icon.height: 22
            iconName: "xopp-edit-redo"
            label: qsTr("Redo")
            tip: win.withKeys(qsTr("Redo"), "redo")
            enabled: app.canRedo
            onClicked: app.redo()
        }
    }
    Rectangle {  // (between the head and the tools)
        color: "#e3e5e8"
        x: box.vertical ? 10 : headGrid.x + headGrid.width + 4
        y: box.vertical ? headGrid.y + headGrid.height + 4 : 10
        width: box.vertical ? box.width - 20 : box.hair
        height: box.vertical ? box.hair : box.height - 20
    }

    Flickable {
        id: middle
        objectName: "toolboxMiddle"
        x: box.vertical ? 0 : headGrid.x + headGrid.width + 9
        y: box.vertical ? headGrid.y + headGrid.height + 9 : 0
        width: box.vertical ? box.width : tailGrid.x - x - 9
        height: box.vertical ? tailGrid.y - y - 9 : box.height
        contentWidth: middleGrid.width + (box.vertical ? 0 : 8)
        contentHeight: middleGrid.height + (box.vertical ? 8 : 0)
        flickableDirection: box.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: box.vertical ? contentHeight > height : contentWidth > width
        clip: true
        Grid {
            id: middleGrid
            objectName: "toolboxTools"
            columns: box.vertical ? 1 : -1
            rows: box.vertical ? -1 : 1
            x: box.vertical ? 4 : 0
            y: box.vertical ? 0 : 4
            spacing: 0
            Repeater {
                model: box.items
                delegate: Loader {
                    required property var modelData
                    sourceComponent: modelData.kind === "divider" ? dividerComponent
                                     : modelData.kind === "stack" ? stackComponent : entryComponent
                    property var entryItem: modelData
                }
            }
            // The fixed tools, after a divider: in a row of their own, the last ones (or all) folded into one button
            Loader { active: box.fixedButtons.length > 0 && !box.compact; visible: active; sourceComponent: dividerComponent }
            Grid {
                id: fixedGrid
                objectName: "toolboxFixed"
                columns: box.vertical ? 1 : -1
                rows: box.vertical ? -1 : 1
                visible: box.fixedShown > 0
            }
            IconButton {
                id: fixedStack
                objectName: "toolboxFixedStack"
                visible: box.plan.fixedFolded === true && box.fixedButtons.length > 0
                width: box.cell; height: visible ? box.cell : 0
                icon.width: 22; icon.height: 22
                iconName: box.fixedInUse ? box.fixedInUse.iconName : "xqt-tools-more"
                checked: box.fixedInUse !== null
                label: qsTr("More tools")
                tip: qsTr("More of the fixed tools (snip, write on the page, setsquare, mark PDF text, …)")
                ownHold: true
                onClicked: fixedFlyout.visible ? fixedFlyout.close() : fixedFlyout.open()
                onPressAndHold: fixedFlyout.open()
            }
        }
    }

    // The carried tool, under the pointer (along the rail), and the mark of where it would go
    ToolEntryButton {
        id: dragGhost
        objectName: "toolDragGhost"
        visible: box.dragId !== ""
        enabled: false
        z: 10
        cell: box.cell
        entry: box.dragEntry
        dragging: true
        inkColor: (app.colorPalette, box.store.revision, box.dragId !== "" ? app.toolEntryColor(box.dragEntry) : "#303030")
        x: box.vertical ? (box.width - width) / 2 : Math.max(0, Math.min(box.width - width, box.dragPoint.x - width / 2))
        y: box.vertical ? Math.max(0, Math.min(box.height - height, box.dragPoint.y - height / 2)) : (box.height - height) / 2
    }
    Rectangle {
        objectName: "toolDropMark"
        visible: box.dragId !== "" && box.dropIndex >= 0
        z: 9
        color: Material.accentColor
        radius: 2
        readonly property point at: middleGrid.mapToItem(box, box.vertical ? 0 : box.dropMark, box.vertical ? box.dropMark : 0)
        x: box.vertical ? 6 : at.x - 2
        y: box.vertical ? at.y - 2 : 6
        width: box.vertical ? box.width - 12 : 4
        height: box.vertical ? 4 : box.height - 12
    }

    Grid {
        id: tailGrid
        objectName: "toolboxTail"
        columns: box.vertical ? 1 : -1
        rows: box.vertical ? -1 : 1
        x: box.vertical ? 4 : box.width - width - 4 - box.endInset
        y: box.vertical ? box.height - height - 4 - box.endInset : 4
        IconButton {
            id: addButton
            objectName: "toolboxAddButton"
            visible: !box.compact  // (a phone: in the sheet of every tool)
            width: box.cell; height: box.cell
            icon.width: 22; icon.height: 22
            iconName: "xqt-plus"
            label: qsTr("Add a tool")
            tip: qsTr("Add a tool (a pen, highlighter, shape, eraser, text box, sticky note, laser pointer, snip)")
            onClicked: box.addRequested(addButton)
        }
        IconButton {
            id: moreButton
            objectName: "toolboxMoreButton"
            visible: box.moreShown
            width: box.cell; height: visible ? box.cell : 0
            icon.width: 22; icon.height: 22
            iconName: "xqt-more"
            label: qsTr("More")
            tip: qsTr("More (present, search, settings, leave full screen)")
            onClicked: box.moreRequested(moreButton)
        }
        // The phone's dock: every tool, and the page number
        IconButton {
            objectName: "toolboxAllButton"
            visible: box.compact
            width: box.cell; height: box.cell
            icon.width: 22; icon.height: 22
            iconName: "xqt-tools-more"
            label: qsTr("My tools")
            tip: qsTr("My tools: every tool, add one, the other tools")
            onClicked: box.allToolsRequested()
        }
        ToolButton {
            objectName: "toolboxPageButton"
            visible: box.compact && !app.homeVisible
            width: box.cell + 8; height: box.cell
            focusPolicy: Qt.NoFocus
            text: app.pageNumber + "/" + app.pageCount
            font.pixelSize: 13
            Accessible.name: qsTr("All pages")
            onClicked: box.pagesRequested()
        }
    }

    Component {
        id: dividerComponent
        Item {
            width: box.vertical ? box.cell : 9
            height: box.vertical ? 9 : box.cell
            Rectangle {
                anchors.centerIn: parent
                width: box.vertical ? box.cell - 16 : box.hair
                height: box.vertical ? box.hair : box.cell - 16
                color: "#d5d8dc"
            }
        }
    }
    Component {
        id: entryComponent
        ToolEntryButton {
            readonly property var e: parent ? box.entryOf(parent.entryItem.entry) : ({})
            objectName: "toolEntry_" + (e ? e.id : "")
            cell: box.cell
            entry: e
            name: box.entryName(e)
            inkColor: (app.colorPalette, store.revision, app.toolEntryColor(e))
            inHand: box.inHand(e)
            towardsPage: box.towardsPage
            onClicked: box.tap(e, this)
            onHeld: function(pos) { box.menuRequested(e, this, pos) }
            onSecondaryClicked: function(pos) { box.menuRequested(e, this, pos) }
            onWheelStepped: function(steps) { box.stepWidth(e, steps) }
            onDragMoved: function(pos) { box.dragTo(e, pos) }
            onDropped: function(pos) { box.dropAt(e, pos) }
            onDragCanceled: box.endDrag()
        }
    }
    Component {
        id: stackComponent
        ToolEntryButton {
            readonly property var e: parent ? box.entryOf(parent.entryItem.entry) : ({})
            readonly property var section: parent ? parent.entryItem.section.map(box.entryOf) : []
            objectName: "toolStack_" + (e ? e.id : "")
            reorderable: false
            cell: box.cell
            entry: e
            stackCount: section.length
            name: box.entryName(e) + " " + qsTr("(and %n more: hold)", "", section.length - 1)
            inkColor: (app.colorPalette, store.revision, app.toolEntryColor(e))
            inHand: box.inHand(e)
            towardsPage: box.towardsPage
            onClicked: box.inHand(e) ? box.openStack(this, section) : app.applyToolEntry(e.id)
            onHeld: box.openStack(this, section)
            onSecondaryClicked: box.openStack(this, section)
            onWheelStepped: function(steps) { box.stepWidth(e, steps) }
        }
    }

    // A folded section's list: its tools, beside the stack (towards the page)
    Popup {
        id: stackFlyout
        objectName: "toolStackFlyout"
        property var section: []
        property Item owner: null
        parent: owner
        x: !owner ? 0 : box.edge === "right" ? -width - 8 : box.edge === "left" ? owner.width + 8 : 0
        y: !owner ? 0 : box.edge === "bottom" ? -height - 8 : box.edge === "top" ? owner.height + 8 : 0
        padding: 4
        margins: 8
        focus: true  // (Esc closes it)
        background: Rectangle { radius: 12; color: "#ffffff"; border.width: 1; border.color: "#d5d8dc" }
        Grid {
            columns: box.vertical ? 1 : -1
            rows: box.vertical ? -1 : 1
            Repeater {
                model: stackFlyout.section
                delegate: ToolEntryButton {
                    required property var modelData
                    objectName: "stackEntry_" + modelData.id
                    reorderable: false
                    cell: box.cell
                    entry: modelData
                    name: box.entryName(modelData)
                    inkColor: (app.colorPalette, store.revision, app.toolEntryColor(modelData))
                    inHand: box.inHand(modelData)
                    towardsPage: box.towardsPage
                    onClicked: {
                        stackFlyout.close()
                        if (box.inHand(modelData)) box.editRequested(modelData, stackFlyout.owner)
                        else app.applyToolEntry(modelData.id)
                    }
                    onHeld: function(pos) { stackFlyout.close(); box.menuRequested(modelData, stackFlyout.owner, pos) }
                    onSecondaryClicked: function(pos) { stackFlyout.close(); box.menuRequested(modelData, stackFlyout.owner, pos) }
                }
            }
        }
    }
    // The fixed tools folded: their list
    Popup {
        id: fixedFlyout
        objectName: "toolboxFixedFlyout"
        parent: fixedStack
        x: box.edge === "right" ? -width - 8 : box.edge === "left" ? fixedStack.width + 8 : 0
        y: box.edge === "bottom" ? -height - 8 : box.edge === "top" ? fixedStack.height + 8 : 0
        padding: 4
        margins: 8
        focus: true
        background: Rectangle { radius: 12; color: "#ffffff"; border.width: 1; border.color: "#d5d8dc" }
        Grid {
            id: fixedFlyoutGrid
            columns: box.vertical ? 1 : -1
            rows: box.vertical ? -1 : 1
        }
        // (a tool taken in it: back to the page, unless the button opened a menu of its own)
        Connections {
            target: app
            enabled: fixedFlyout.opened
            function onToolChanged() { if (!Popups.hasOpenPopupIn(fixedFlyoutGrid)) fixedFlyout.close() }
        }
    }
}
