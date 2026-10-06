// The toolbox (qt/docs/toolbox.md): the user's own tools in a rail docked to a side of the canvas (right by default;
// left, top or bottom by choice, per window size), like pens lying sorted in a box on the table. One element in the
// window, in full screen and on phones (the dock: the same rail at the bottom edge).
//   head   undo, redo (pinned)
//   middle the rail's items as the arrangement has them (ToolboxModel): the user's tools, dividers, and the app's own
//          tools (hand, select, snip, mark PDF text by default: the window's buttons, lent to the rail); it scrolls
//   tail   "+" (add a tool), ⋯ (full screen: what the tab strip and the command bar hold there); a phone's dock: "My
//          tools" and the page number (pinned)
// The middle scrolls when its items do not fit (qt/rail-scroll; before, sections folded into stacks): the same order on
// every screen, nothing folded. When it scrolls, it ends through the middle of a cell (half of the next one shows) and
// fades at the end that has more; a tool taken by a key or elsewhere is scrolled into view; where it was scrolled to is
// remembered per window class.
// A tap on a tool picks it up (AppController::applyToolEntry); a tap on the one in hand opens its editor; a drag at once
// scrolls; a long press or a right click: its menu; held, then moved: it is carried to another place; the wheel over a
// tool changes its width, over a gap (or an app tool) it scrolls.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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
    /// The phone's dock (qt/docs/toolbox.md, "On a phone"): undo, redo, the same items scrolling sideways, "My tools"
    /// (the sheet with all of them and "Add a tool") and the page number
    property bool compact: false
    /// The window's buttons of the app's items by name (Main.qml's toolArea.slots): those on the rail are lent to it
    property var appButtons: ({})
    /// It is shown: the buttons of its app items are its own (the command bar leaves them out)
    property bool lending: false
    /// The size of a tool's cell: a finger's target in the touch profile
    readonly property real cell: win.adaptive.touchProfile ? win.adaptive.minTarget : 44
    /// Its thickness across the edge
    readonly property real thickness: cell + 8
    /// Room at its ends that is not its own (a navigation bar under a rail's end, a cut-out)
    property real startInset: 0
    property real endInset: 0
    /// Where the page is, seen from a button (the entry in hand lifts towards it)
    readonly property string towardsPage: edge === "right" ? "left" : edge === "left" ? "right" : edge === "top" ? "down" : "up"
    /// The arrangement of the tools (ToolboxModel)
    readonly property var store: app.toolbox

    signal editRequested(var entry, Item button)
    /// A long press or a right click on an item (a tool entry, an app item): its menu
    signal menuRequested(var entry, Item button, point pos)
    signal addRequested(Item button)
    signal moreRequested(Item button)
    /// The phone's dock: "My tools" (the sheet of every tool) and the page number (all pages)
    signal allToolsRequested()
    signal pagesRequested()
    /// Its grip dragged: to another edge of the window (the window highlights the edge it would go to)
    signal gripMoved(point scenePos)
    signal gripDropped(point scenePos)
    /// A tool carried onto another made a group (`before`: the arrangement to go back to, "Grouped · Undo")
    signal grouped(string groupId, string before)

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
    /// The app item's button (null: none, or not offered here: a text document)
    function appButton(name) {
        const b = appButtons[name]
        return b && b.offered !== false ? b : null
    }
    /// The rail's items as the arrangement has them: {kind: "entry" | "app" | "group" | "divider", key, id, entry, name}
    readonly property var itemsNow: {
        const out = []
        const list = (store.revision, store.entries)
        for (let i = 0; i < list.length; ++i) {
            const e = list[i]
            if (e.divider === true) {
                // (none first, none two in a row once the app items not offered here are left out)
                if (out.length > 0 && out[out.length - 1].kind !== "divider") out.push({ kind: "divider", key: e.id, id: e.id })
            } else if (e.app !== undefined) {
                if (appButton(e.app)) out.push({ kind: "app", key: e.id, id: e.id, entry: e, name: e.app })
            } else if (e.group === true) {
                if (groupMembers(e.id).length > 0) out.push({ kind: "group", key: e.id, id: e.id, entry: e })
            } else {
                out.push({ kind: "entry", key: e.id, id: e.id, entry: e })
            }
        }
        while (out.length > 0 && out[out.length - 1].kind === "divider") out.pop()
        return out
    }

    /// What the rail shows: `itemsNow`, taken only when what is where changes (an entry, an app item, a divider), not
    /// when a tool's color or width does. A new list makes the Repeater build every button anew, and a popup beside one
    /// (its editor) lost its place: it went to the window's corner (2026-10-05). The buttons read their entries from the
    /// store (`entryOf`).
    property var items: []
    property string itemsSignature: ""
    onItemsNowChanged: syncItems()
    Component.onCompleted: { syncItems(); Qt.callLater(restoreScroll) }
    function syncItems() {
        const sig = itemsNow.map(function(it) { return it.kind + ":" + it.key }).join("|")
        if (sig === itemsSignature) return
        itemsSignature = sig
        items = itemsNow
    }
    /// An entry as the store has it now (the items keep the ids)
    function entryOf(e) { return (store.revision, e && e.id ? store.entry(e.id) : ({})) }
    /// The button that shows an item now (null: none)
    function buttonFor(id) {
        const kids = middleGrid.children
        for (let i = 0; i < kids.length; ++i) {
            const it = kids[i].entryItem
            if (it && kids[i].item && (it.id === id || (it.kind === "group" && store.groupOf(id) === it.id)))
                return kids[i].item
        }
        return null
    }

    // --- groups the user makes (a tool carried onto another, held there until it shows a ring) -----------------------
    /// A group's members that are offered here (an app item whose button the window offers)
    function groupMembers(gid) {
        return (store.revision, store.members(gid)).filter(function(m) { return m.app === undefined || appButton(m.app) !== null })
    }
    /// The member a group shows: the one used last
    function groupShown(gid) {
        const ms = groupMembers(gid)
        const id = (store.revision, store.shownOf(gid))
        for (let i = 0; i < ms.length; ++i) if (ms[i].id === id) return ms[i]
        return ms.length > 0 ? ms[0] : ({})
    }
    /// A member is the tool in hand (an app item: its button is checked, and it is a tool)
    function memberInHand(m) {
        if (!m) return false
        if (m.app !== undefined) {
            const b = appButton(m.app)
            return (app.tool, app.snip, b !== null && b.checked === true && isTool(m.app))
        }
        return inHand(m)
    }
    /// Takes a member: a tool entry with all it holds, an app item by its button's tap
    function takeMember(m) {
        if (!m || !m.id) return
        if (m.app !== undefined) {
            const b = appButton(m.app)
            if (b) b.clicked()
        } else {
            app.applyToolEntry(m.id)
        }
        store.use(m.id)
    }
    /// A tap on a group: the member it shows. With one of its members in hand: the next one (two or three members), or
    /// its list (more than three; the author: "A tap should open the group list if more than 3 tools in the group")
    function tapGroup(gid, button) {
        const ms = groupMembers(gid)
        if (ms.length === 0) return
        let at = -1
        for (let i = 0; i < ms.length; ++i) if (memberInHand(ms[i])) at = i
        if (at < 0) takeMember(groupShown(gid))
        else if (ms.length <= 3) takeMember(ms[(at + 1) % ms.length])
        else openGroup(gid, button)
    }
    /// The group's list beside its button: its members (a tap takes one, the one in hand: its editor; held: its menu;
    /// held and moved: carried out of the group)
    function openGroup(gid, button) {
        groupFlyout.gid = gid
        groupFlyout.owner = button
        groupFlyout.open()
    }
    /// An app tool of a group taken another way (a key, the sheet): the group shows it
    function noteAppUse() {
        const list = store.entries
        for (let i = 0; i < list.length; ++i) {
            if (list[i].group !== true) continue
            const ms = list[i].members
            for (let j = 0; j < ms.length; ++j) if (ms[j].app !== undefined && memberInHand(ms[j])) store.use(ms[j].id)
        }
    }
    /// The name of a group: "A group of 4 tools"
    function groupName(gid) { return qsTr("A group of %n tools", "", groupMembers(gid).length) }

    // --- the lengths: the middle scrolls when its items do not fit ----------------------------------------------------
    readonly property real length: vertical ? height : width
    /// Where the middle begins along the rail (after the grip, the head and its line)
    readonly property real middleStart: (vertical ? headGrid.y + headGrid.height : headGrid.x + headGrid.width) + 9
    /// What the rail takes along its length besides the middle (none of it depends on the rail's length)
    readonly property real overhead: middleStart + 9 + (vertical ? tailGrid.height : tailGrid.width) + 4 + endInset
    /// The room of the middle as laid out now
    readonly property real middleRoom: length - overhead
    /// The length the middle's items take (and a little room after the last, for the mark of a place there)
    readonly property real contentLength: itemsLength(items) + 4
    function itemsLength(list) {
        let n = 0
        for (let i = 0; i < list.length; ++i) n += list[i].kind === "divider" ? 9 : cell
        return n
    }
    /// The middle's length in `room`: all of it when everything fits; else cut through the middle of a cell, so that
    /// half of the next one shows (there is more). A room for less than a cell and a half (a phone held sideways):
    /// all of it, a whole cell in sight before the cut.
    function cutFor(room) {
        if (contentLength <= room + 0.5 || room < cell * 1.5) return Math.max(0, room)
        let at = 0, best = room
        for (let i = 0; i < items.length; ++i) {
            if (items[i].kind !== "divider" && at + cell / 2 <= room) best = at + cell / 2
            at += items[i].kind === "divider" ? 9 : cell
        }
        return best
    }
    readonly property real viewLength: cutFor(middleRoom)
    /// It scrolls: its items do not fit
    readonly property bool scrolls: contentLength > middleRoom + 0.5
    /// Its length with every item in sight (a floating rail is no longer than that)
    readonly property real naturalLength: overhead + contentLength
    /// The length it takes when it may have `available` (floating: as long as its items, or cut through a cell)
    function lengthFor(available) {
        return naturalLength <= available ? naturalLength : overhead + cutFor(available - overhead)
    }

    // --- scrolling -------------------------------------------------------------------------------------------------
    /// Where the middle is scrolled to (along the rail)
    readonly property real scrollPos: vertical ? middle.contentY : middle.contentX
    function scrollTo(pos) {
        const size = vertical ? middle.height : middle.width
        const content = vertical ? middle.contentHeight : middle.contentWidth
        const p = Math.max(0, Math.min(content - size, pos))
        if (vertical) middle.contentY = p
        else middle.contentX = p
    }
    function scrollBy(d) { scrollTo(scrollPos + d) }
    /// Scrolls an item of the middle into sight (with half a cell beyond it, clear of the fade)
    function reveal(item) {
        if (!item || !scrolls) return
        let p0 = item
        while (p0 && p0 !== middleGrid) p0 = p0.parent
        if (!p0) return  // (a button of the group's list)
        const p = item.mapToItem(middleGrid, 0, 0)
        const start = vertical ? p.y : p.x
        const end = start + (vertical ? item.height : item.width)
        const size = vertical ? middle.height : middle.width
        const margin = Math.min(cell / 2, Math.max(0, (size - (end - start)) / 2))
        if (start - margin < scrollPos) scrollTo(start - margin)
        else if (end + margin > scrollPos + size) scrollTo(end + margin - size)
    }
    /// The button of the tool in hand on the rail (null: none here)
    function inHandButton() {
        const kids = middleGrid.children
        for (let i = 0; i < kids.length; ++i) {
            const it = kids[i].entryItem
            if (!it || !kids[i].item) continue
            if (it.kind === "entry" && inHand(entryOf(it.entry))) return kids[i].item
            if (it.kind === "group" && groupMembers(it.id).some(memberInHand)) return kids[i].item
            if (it.kind === "app") {
                const b = appButton(it.name)
                if (b && b.checked && isTool(it.name)) return kids[i].item
            }
        }
        return null
    }
    /// An app item that is a tool in hand (not a switch or a command)
    function isTool(name) { return ["hand", "select", "snip", "pdfText", "geometry"].indexOf(name) >= 0 }
    function revealInHand() { reveal(inHandButton()) }
    // A tool taken by a key, the sheet, a menu or the rail itself: in sight
    Connections {
        target: app
        function onToolChanged() { Qt.callLater(box.revealInHand); Qt.callLater(box.noteAppUse) }
        function onSnipChanged() { Qt.callLater(box.revealInHand) }
    }
    Connections {
        target: box.store
        function onActiveChanged() { Qt.callLater(box.revealInHand) }
    }
    // Where it was scrolled to, per window class (a rail of a desktop window and the phone's dock each their own)
    readonly property string sizeClass: win.adaptive.layoutClass
    onSizeClassChanged: Qt.callLater(restoreScroll)
    // (within the contents still, and the tool in hand in sight: a window made smaller, the keyboard, the insets)
    onViewLengthChanged: { scrollBy(0); Qt.callLater(revealInHand) }
    /// (the tool in hand in sight still: it matters more than the place of before)
    function restoreScroll() {
        const v = parseFloat(win.layoutChoice("toolboxScroll"))
        scrollTo(isNaN(v) ? 0 : v)
        revealInHand()
    }
    Timer {
        id: rememberScroll
        interval: 600
        onTriggered: {
            if (middle.moving) return restart()
            win.chooseLayout("toolboxScroll", box.scrollPos > 0.5 ? String(Math.round(box.scrollPos)) : "")
        }
    }
    onScrollPosChanged: if (scrolls) rememberScroll.restart()

    // --- what an entry does ----------------------------------------------------------------------------------------
    /// Its name: "Pen · Body", "Highlighter · Key terms", "Arrow", "Eraser (whiteout)", an app item's label, …
    function entryName(e) {
        if (e && e.group === true) return groupName(e.id)
        if (e && e.app !== undefined) {
            const b = appButtons[e.app]
            return b ? (b.label !== undefined && b.label !== "" ? b.label : b.name || e.app) : e.app
        }
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
                !!e && e.type !== undefined && (e.type === "snip" || store.active === e.id) && app.entryInHand(e))
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

    /// Held (or right-clicked): its menu; a member in the group's list: the list closes, the menu opens at the group
    function held(e, button, pos) {
        if (groupFlyout.opened && e.app === undefined) {
            const owner = groupFlyout.owner
            groupFlyout.close()
            menuRequested(e, owner, Qt.point(owner.width / 2, owner.height / 2))
            return
        }
        menuRequested(e, button, pos)
    }

    // --- carrying: an item held, then moved to another place (qt/docs/toolbox.md, "Carrying") ---------------------------
    /// The item being carried ("": none), where it would go (an index among the rail's items; -1: nowhere, it goes
    /// back), and the mark of that place (along the rail, in the items' coordinates)
    property string dragId: ""
    property var dragEntry: ({})
    property int dropIndex: -1
    property real dropMark: -1
    property point dragPoint
    /// The item the carried one is held over ("": none) and, once it was held there long enough (about 0.6 s), the one
    /// that shows a ring: let go there, the two are a group
    property string hoverId: ""
    property string ringId: ""
    Timer {
        id: dwell
        interval: 600
        onTriggered: box.ringId = box.hoverId
    }
    function hoverOver(id) {
        if (id === hoverId) return
        hoverId = id
        ringId = ""
        if (id !== "") dwell.restart()
        else dwell.stop()
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
            hoverOver("")
            return
        }
        // Near an end of a rail that scrolls: it scrolls along
        const m = middle.mapFromItem(null, scenePos.x, scenePos.y)
        const pos = vertical ? m.y : m.x, size = vertical ? middle.height : middle.width
        if (scrolls) {
            if (pos < cell / 2) scrollBy(-cell / 3)
            else if (pos > size - cell / 2) scrollBy(cell / 3)
        }
        const p = middleGrid.mapFromItem(null, scenePos.x, scenePos.y)
        const along = vertical ? p.y : p.x
        // Over the middle of another item (not a divider, not itself or its own group): it may become a group
        let over = ""
        const kids = middleGrid.children
        for (let i = 0; i < kids.length; ++i) {
            const k = kids[i]
            if (!k.entryItem || !k.visible || k.entryItem.kind === "divider") continue
            const start = vertical ? k.y : k.x
            const extent = vertical ? k.height : k.width
            if (along > start + extent * 0.2 && along < start + extent * 0.8) over = k.entryItem.id
        }
        if (over === e.id || (over !== "" && store.groupOf(e.id) === over)) over = ""
        hoverOver(over)
        let best = -1, mark = 0
        for (let i = 0; i < kids.length; ++i) {
            const k = kids[i]
            if (!k.entryItem || !k.visible) continue
            const start = vertical ? k.y : k.x
            const extent = vertical ? k.height : k.width
            if (along < start + extent / 2) {
                best = store.indexOf(k.entryItem.id)
                mark = start
                break
            }
            mark = start + extent
        }
        dropIndex = best >= 0 ? best : store.entries.length
        dropMark = mark
    }
    function dropAt(e, scenePos) {
        dragTo(e, scenePos)
        if (ringId !== "") {
            const before = store.snapshot()
            const g = store.group(e.id, ringId)
            if (g !== "") grouped(g, before)
        } else if (dropIndex >= 0) {
            store.move(e.id, dropIndex)
        }
        endDrag()
        groupFlyout.close()
    }
    function endDrag() {
        dragId = ""
        dropIndex = -1
        dropMark = -1
        hoverOver("")
    }

    // --- the app's buttons, lent to the rail ---------------------------------------------------------------------------
    /// The buttons of the app items on the rail while it is shown (the command bar leaves them out)
    readonly property var fixedButtons: {
        if (!lending) return []
        const out = []
        const list = (store.revision, store.entries)
        for (let i = 0; i < list.length; ++i) {
            const e = list[i]
            const names = e.app !== undefined ? [e.app]
                          : e.group === true ? e.members.filter(function(m) { return m.app !== undefined })
                                                        .map(function(m) { return m.app }) : []
            names.forEach(function(n) { if (appButtons[n]) out.push(appButtons[n]) })
        }
        return out
    }
    /// The buttons lent now (given back when they leave the list)
    property var placedFixed: []
    onFixedButtonsChanged: {
        for (let i = 0; i < placedFixed.length; ++i) {
            if (fixedButtons.indexOf(placedFixed[i]) < 0) release(placedFixed[i])
        }
        placedFixed = fixedButtons.slice()
    }
    /// A button given back (the toolbox is not shown, or it left the rail): its own size again; its owner (the command
    /// bar) puts it where it belongs
    function release(b) {
        for (let p = b.parent; p; p = p.parent) {
            if (p === box) {
                b.parent = bank
                break
            }
        }
        b.width = Qt.binding(function() { return b.implicitWidth })
        b.height = Qt.binding(function() { return b.implicitHeight })
    }
    /// Takes a button into a cell of the rail
    function lend(b, cellItem) {
        if (!b || !lending) return
        b.parent = cellItem
        b.x = 0
        b.y = 0
        b.width = cell
        b.height = cell
    }
    onLendingChanged: if (lending) Qt.callLater(lendAll)
    /// (shown again: the cells take their buttons back from the command bar)
    function lendAll() {
        const kids = middleGrid.children
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].item && kids[i].item.lendButton) kids[i].item.lendButton()
        }
    }
    // (a button that left a cell going away, until the command bar takes it)
    Item { id: bank; visible: false }

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
        x: box.vertical ? 0 : box.middleStart
        y: box.vertical ? box.middleStart : 0
        width: box.vertical ? box.width : box.viewLength
        height: box.vertical ? box.viewLength : box.height
        contentWidth: box.vertical ? width : box.contentLength
        contentHeight: box.vertical ? box.contentLength : height
        flickableDirection: box.vertical ? Flickable.VerticalFlick : Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: box.scrolls
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
                                     : modelData.kind === "app" ? appComponent
                                     : modelData.kind === "group" ? groupComponent : entryComponent
                    property var entryItem: modelData
                }
            }
        }
        // A sideways rail: the mouse wheel (up and down) scrolls it too
        WheelHandler {
            enabled: !box.vertical && box.scrolls
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                const d = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                box.scrollBy(-d / 2)
            }
        }
    }
    // The fades at the ends that have more
    Rectangle {
        objectName: "toolboxFadeStart"
        visible: box.scrolls && box.scrollPos > 0.5
        x: middle.x
        y: middle.y
        width: box.vertical ? middle.width : 18
        height: box.vertical ? 18 : middle.height
        gradient: Gradient {
            orientation: box.vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0; color: box.color }
            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
        }
    }
    Rectangle {
        objectName: "toolboxFadeEnd"
        visible: box.scrolls && box.scrollPos < box.contentLength - box.viewLength - 0.5
        x: box.vertical ? middle.x : middle.x + middle.width - width
        y: box.vertical ? middle.y + middle.height - height : middle.y
        width: box.vertical ? middle.width : 18
        height: box.vertical ? 18 : middle.height
        gradient: Gradient {
            orientation: box.vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
            GradientStop { position: 1; color: box.color }
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
        appIcon: box.dragEntry && box.dragEntry.app !== undefined && box.appButtons[box.dragEntry.app]
                 ? box.appButtons[box.dragEntry.app].iconName : ""
        dragging: true
        inkColor: (app.colorPalette, box.store.revision, box.dragId !== "" && box.dragEntry.type !== undefined
                   ? app.toolEntryColor(box.dragEntry) : "#303030")
        x: box.vertical ? (box.width - width) / 2 : Math.max(0, Math.min(box.width - width, box.dragPoint.x - width / 2))
        y: box.vertical ? Math.max(0, Math.min(box.height - height, box.dragPoint.y - height / 2)) : (box.height - height) / 2
    }
    Rectangle {
        objectName: "toolDropMark"
        visible: box.dragId !== "" && box.dropIndex >= 0 && box.ringId === ""
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
            ringed: box.ringId !== "" && box.ringId === e.id
            towardsPage: box.towardsPage
            /// (a member in the group's list: its taps are the list's)
            readonly property bool inList: parent ? parent.inList === true : false
            onClicked: {
                if (inList) {
                    const owner = groupFlyout.owner
                    groupFlyout.close()
                    if (box.inHand(e)) box.editRequested(e, owner)
                    else box.takeMember(e)
                    return
                }
                box.tap(e, this)
                box.reveal(this)
            }
            onHeld: function(pos) { box.held(e, this, pos) }
            onSecondaryClicked: function(pos) { box.held(e, this, pos) }
            onWheelStepped: function(steps) { box.stepWidth(e, steps) }
            onDragMoved: function(pos) { box.dragTo(e, pos) }
            onDropped: function(pos) { box.dropAt(e, pos) }
            onDragCanceled: box.endDrag()
        }
    }
    // An app item: the window's own button, lent to the rail, under the rail's gestures (a tap is its tap; held: the
    // rail's menu for it; held and moved: carried)
    Component {
        id: appComponent
        Item {
            id: appCell
            readonly property var it: parent ? parent.entryItem : null
            readonly property string name: it ? it.name : ""
            readonly property Item button: name !== "" ? box.appButton(name) : null
            objectName: "railApp_" + name
            width: box.cell
            height: box.cell
            /// Carried: its place stays faint; held: lifted
            readonly property bool carried: box.dragId !== "" && it !== null && box.dragId === it.id
            opacity: carried ? 0.3 : 1
            scale: carryArea.armed && !carried ? 1.15 : 1
            readonly property bool inList: parent ? parent.inList === true : false
            Rectangle {  // (held over by a carried tool long enough: let go, a group)
                objectName: "toolRing"
                visible: box.ringId !== "" && appCell.it !== null && box.ringId === appCell.it.id
                anchors.fill: parent
                anchors.margins: -2
                z: 2
                radius: 12
                color: "transparent"
                border.width: 2.5
                border.color: Material.accentColor
            }
            Behavior on scale { NumberAnimation { duration: 120 } }
            function lendButton() { box.lend(button, appCell) }
            onButtonChanged: lendButton()
            Component.onCompleted: lendButton()
            Component.onDestruction: if (button && button.parent === appCell) box.release(button)
            MouseArea {
                id: carryArea
                anchors.fill: parent
                z: 1
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                pressAndHoldInterval: 400
                property bool armed: false
                property bool moving: false
                property point start
                // (held: the rail may not take the drag for scrolling any more)
                preventStealing: armed
                onPressed: function(mouse) {
                    armed = false
                    moving = false
                    start = Qt.point(mouse.x, mouse.y)
                    if (appCell.button && mouse.button === Qt.LeftButton) appCell.button.down = true
                }
                onPressAndHold: function(mouse) {
                    if (mouse.button === Qt.RightButton) return
                    armed = true
                    // (its name above the finger while held, as the window's buttons show it)
                    if (appCell.button && appCell.button.heldLabel !== undefined) appCell.button.heldLabel = true
                }
                function quiet() { if (appCell.button && appCell.button.heldLabel === true) appCell.button.heldLabel = false }
                onPositionChanged: function(mouse) {
                    if (!armed) return
                    if (!moving && Math.hypot(mouse.x - start.x, mouse.y - start.y) < 8) return
                    quiet()
                    moving = true
                    box.dragTo(appCell.it.entry, mapToItem(null, mouse.x, mouse.y))
                }
                onReleased: function(mouse) {
                    quiet()
                    if (appCell.button) appCell.button.down = undefined
                    if (moving) box.dropAt(appCell.it.entry, mapToItem(null, mouse.x, mouse.y))
                    else if (armed) box.menuRequested(appCell.it.entry, appCell, Qt.point(mouse.x, mouse.y))
                    armed = false
                    moving = false
                }
                onCanceled: {
                    quiet()
                    if (appCell.button) appCell.button.down = undefined
                    if (moving) box.endDrag()
                    armed = false
                    moving = false
                }
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) box.menuRequested(appCell.it.entry, appCell, Qt.point(mouse.x, mouse.y))
                    else if (!armed && appCell.button) {
                        appCell.button.clicked()
                        if (appCell.inList) {
                            box.store.use(appCell.it.id)
                            groupFlyout.close()
                        } else {
                            box.reveal(appCell)
                        }
                    }
                }
            }
        }
    }
    // A group the user made: the member used last, with dots for how many it holds (qt/docs/toolbox.md, "Groups")
    Component {
        id: groupComponent
        ToolEntryButton {
            id: face
            readonly property string gid: parent ? parent.entryItem.id : ""
            readonly property var members: box.groupMembers(gid)
            readonly property var shownMember: box.groupShown(gid)
            readonly property bool isApp: shownMember.app !== undefined
            objectName: "railGroup_" + gid
            cell: box.cell
            entry: shownMember
            appIcon: isApp && box.appButton(shownMember.app) ? box.appButton(shownMember.app).iconName : ""
            stackCount: members.length
            name: box.entryName(shownMember) + " " + qsTr("(a group of %n: hold for its menu)", "", members.length)
            inkColor: (app.colorPalette, store.revision, isApp ? "#303030" : app.toolEntryColor(shownMember))
            inHand: (app.tool, app.snip, box.memberInHand(shownMember))
            ringed: box.ringId !== "" && box.ringId === gid
            towardsPage: box.towardsPage
            onClicked: { box.tapGroup(gid, this); box.reveal(this) }
            onHeld: function(pos) { box.menuRequested(box.store.entry(gid), this, pos) }
            onSecondaryClicked: function(pos) { box.menuRequested(box.store.entry(gid), this, pos) }
            onWheelStepped: function(steps) { if (!isApp) box.stepWidth(shownMember, steps) }
            // (carried as a whole: to another place, or onto another item)
            readonly property var carriedAs: Object.assign({}, shownMember, { id: gid })
            onDragMoved: function(pos) { box.dragTo(carriedAs, pos) }
            onDropped: function(pos) { box.dropAt(carriedAs, pos) }
            onDragCanceled: box.endDrag()
        }
    }
    // A group's list: its members, beside the group (towards the page)
    Popup {
        id: groupFlyout
        objectName: "toolGroupFlyout"
        property string gid: ""
        property Item owner: null
        parent: owner
        x: !owner ? 0 : box.edge === "right" ? -width - 8 : box.edge === "left" ? owner.width + 8 : 0
        y: !owner ? 0 : box.edge === "bottom" ? -height - 8 : box.edge === "top" ? owner.height + 8 : 0
        padding: 4
        margins: 8
        focus: true  // (Esc closes it)
        background: Rectangle { radius: 12; color: "#ffffff"; border.width: 1; border.color: "#d5d8dc" }
        Grid {
            id: groupGrid
            columns: box.vertical ? 1 : -1
            rows: box.vertical ? -1 : 1
            Repeater {
                model: groupFlyout.visible && groupFlyout.gid !== "" ? box.groupMembers(groupFlyout.gid) : []
                delegate: Loader {
                    required property var modelData
                    readonly property bool inList: true
                    property var entryItem: modelData.app !== undefined
                                            ? { kind: "app", key: modelData.id, id: modelData.id, entry: modelData, name: modelData.app }
                                            : { kind: "entry", key: modelData.id, id: modelData.id, entry: modelData }
                    sourceComponent: modelData.app !== undefined ? appComponent : entryComponent
                }
            }
        }
        // (a tool taken another way while it is open: back to the page, unless a button opened a list of its own)
        Connections {
            target: app
            enabled: groupFlyout.opened && box.dragId === ""
            function onToolChanged() { if (!Popups.hasOpenPopupIn(groupGrid)) groupFlyout.close() }
        }
    }
}
