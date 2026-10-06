// The bottom sheet that an AdaptiveMenu becomes in the phone classes (qt/docs/adaptive-layout.md, "Menus"). There is
// one per window (Main.qml's menuSheet). It shows the entries of the menu as rows as tall as a finger needs, and a
// submenu drills in: the sheet shows its entries, with a back arrow and its title. The menu itself stays closed; its
// entries are only read, and a tap on a row triggers the entry. What is not an entry comes along: a separator as a
// line, a label as a caption, anything else (a row of controls) is borrowed from the menu while the sheet shows it.
// At most 85 % of the window high (the rest scrolls), above the bottom safe area. A drag down on the handle or a tap
// beside it closes it; Esc or Android's back go back a level, and close it at the top.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: sheet
    // (open: Android's back key is its; Zen's Back waits, qt/top-bar)
    onOpenedChanged: if (typeof win !== "undefined" && win && win.takeBack !== undefined) win.takeBack(opened)
    objectName: "menuSheet"
    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 0
    // (Esc and back: a level at a time, below)
    closePolicy: Popup.CloseOnPressOutside

    /// The menus shown: the one opened, then the submenus drilled into
    property var stack: []
    readonly property var menu: stack.length > 0 ? stack[stack.length - 1] : null
    /// The rows: {kind: "item", "submenu", "separator", "caption" or "custom", source: the menu's item}
    property var entries: []
    /// The row chosen with the arrow keys (-1: none)
    property int keyIndex: -1
    readonly property var adaptiveLayout: typeof win !== "undefined" && win ? win.adaptive : null
    readonly property int rowHeight: Math.max(48, adaptiveLayout ? adaptiveLayout.minTarget : 48)
    // (the window's safe area and the soft keyboard: Main.qml)
    readonly property bool inWindow: typeof win !== "undefined" && win !== null && win.safeInsets !== undefined
    readonly property real safeTop: inWindow ? win.safeTop : 0
    readonly property real safeLeft: inWindow ? win.safeLeft : 0
    readonly property real safeRight: inWindow ? win.safeRight : 0
    /// Room below its last row for the navigation bar (none while the keyboard is open: the sheet sits on the keyboard)
    readonly property real safeBottom: inWindow && !win.keyboardOpen ? win.safeBottom : 0
    /// Its bottom edge: the window's, or the soft keyboard's top while it is open
    readonly property real bottomEdge: parent ? (inWindow ? Math.min(parent.height, win.keyboardTop) : parent.height) : 0
    /// The window left the phone classes: the menu is a menu again, so the sheet goes
    readonly property bool phoneClass: adaptiveLayout !== null && ["phonePortrait", "phoneShort", "tiny"].indexOf(adaptiveLayout.layoutClass) >= 0
    onPhoneClassChanged: if (!phoneClass && visible) close()

    width: parent ? Math.min(parent.width - safeLeft - safeRight, 640) : 360
    x: parent ? safeLeft + Math.round((parent.width - safeLeft - safeRight - width) / 2) : 0
    height: parent ? Math.min(implicitHeight, Math.round(bottomEdge * 0.85), bottomEdge - safeTop - 8) : implicitHeight
    y: parent ? bottomEdge - height + handle.offset + slide : 0
    /// Slides it in and out
    property real slide: 0
    enter: Transition { NumberAnimation { property: "slide"; from: sheet.height; to: 0; duration: 180; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "slide"; to: sheet.height; duration: 150; easing.type: Easing.InCubic } }

    /// Shows the entries of `m` (an AdaptiveMenu), as the menu would when it opens
    function show(m) {
        if (visible) hideMenus()
        stack = [m]
        m.aboutToShow()
        rebuild()
        handle.offset = 0
        open()
    }
    function drillInto(sub) {
        sub.aboutToShow()
        stack = stack.concat([sub])
        rebuild()
    }
    /// A level up, or closed at the top
    function back() {
        if (stack.length > 1) {
            const top = stack[stack.length - 1]
            stack = stack.slice(0, -1)
            rebuild()
            top.aboutToHide()
            top.closed()
        } else {
            close()
        }
    }
    /// A row tapped: a submenu drills in; an entry is triggered, as the menu does (a checkable one toggles first)
    function activate(source) {
        if (!source || !source.enabled || source.offered === false) return
        if (source.subMenu) {
            drillInto(source.subMenu)
            return
        }
        close()
        if (source.checkable) source.toggle()
        source.triggered()
    }
    /// The menus shown are told that they closed (their onClosed runs as with the menu)
    function hideMenus() {
        const shown = stack
        stack = []
        entries = []
        for (let i = shown.length - 1; i >= 0; --i) {
            shown[i].aboutToHide()
            shown[i].closed()
        }
    }
    onClosed: hideMenus()

    function kindOf(it) {
        if (it instanceof MenuSeparator) return "separator"
        if (it instanceof MenuItem) return it.subMenu ? "submenu" : "item"
        if (typeof it.text === "string") return "caption"
        return "custom"
    }
    function rebuild() {
        const m = menu, list = []
        if (m) {
            for (let i = 0; i < m.count; ++i) {
                const it = m.itemAt(i)
                if (!it || it.offered === false) continue
                const kind = kindOf(it)
                // (no line at the top, at the end or twice)
                if (kind === "separator" && (list.length === 0 || list[list.length - 1].kind === "separator")) continue
                list.push({ kind: kind, source: it })
            }
        }
        while (list.length > 0 && list[list.length - 1].kind === "separator") list.pop()
        keyIndex = -1
        entries = list
        flick.contentY = 0
    }
    function moveKey(step) {
        let i = keyIndex
        for (let n = 0; n < entries.length; ++n) {
            i = (i + step + entries.length) % entries.length
            const e = entries[i]
            if ((e.kind === "item" || e.kind === "submenu") && e.source.enabled) {
                keyIndex = i
                const row = rows.itemAt(i)
                if (row) {
                    if (row.y < flick.contentY) flick.contentY = row.y
                    else if (row.y + row.height > flick.contentY + flick.height)
                        flick.contentY = row.y + row.height - flick.height
                }
                return
            }
        }
    }

    background: Rectangle {
        radius: 16
        color: "#ffffff"
        Rectangle {  // (square at the bottom: the sheet rests on the window's edge)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
    }

    contentItem: FocusScope {
        focus: true
        implicitHeight: column.implicitHeight
        Keys.onPressed: function(event) {
            const current = sheet.keyIndex >= 0 ? sheet.entries[sheet.keyIndex] : null
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                sheet.back()
            } else if (event.key === Qt.Key_Left && sheet.stack.length > 1) {
                sheet.back()
            } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Tab) {
                sheet.moveKey(1)
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Backtab) {
                sheet.moveKey(-1)
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space
                        || (event.key === Qt.Key_Right && current && current.kind === "submenu")) && current) {
                sheet.activate(current.source)
            } else {
                return
            }
            event.accepted = true
        }

        ColumnLayout {
            id: column
            anchors.fill: parent
            spacing: 0
            MenuSheetHandle {
                id: handle
                objectName: "menuSheetHandle"
                Layout.fillWidth: true
                onDismissed: sheet.close()
            }
            // Inside a submenu: the way back, and where we are
            RowLayout {
                visible: sheet.stack.length > 1 || (sheet.menu !== null && sheet.menu.title !== "")
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 16
                spacing: 4
                ToolButton {
                    objectName: "menuSheetBack"
                    visible: sheet.stack.length > 1
                    implicitWidth: sheet.rowHeight
                    implicitHeight: sheet.rowHeight
                    icon.source: app.iconUrl("xqt-chevron-left")
                    icon.width: 22
                    icon.height: 22
                    display: AbstractButton.IconOnly
                    Accessible.name: qsTr("Back")
                    onClicked: sheet.back()
                }
                Label {
                    objectName: "menuSheetTitle"
                    text: sheet.menu ? sheet.menu.title : ""
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.leftMargin: sheet.stack.length > 1 ? 0 : 12
                    Layout.minimumHeight: sheet.rowHeight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Flickable {
                id: flick
                objectName: "menuSheetList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                Layout.minimumHeight: Math.min(contentHeight, sheet.rowHeight)
                contentHeight: rowColumn.height
                contentWidth: width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height + 1
                ScrollBar.vertical: ScrollBar { policy: flick.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                Column {
                    id: rowColumn
                    width: flick.width
                    Repeater {
                        id: rows
                        model: sheet.entries
                        delegate: Item {
                            id: entry
                            required property var modelData
                            required property int index
                            readonly property string kind: modelData.kind
                            readonly property var source: modelData.source
                            width: rowColumn.width
                            height: kind === "item" || kind === "submenu" ? (row.visible ? row.height : 0)
                                  : kind === "separator" ? 17
                                  : kind === "caption" ? caption.implicitHeight
                                  : (holder.borrowed ? holder.borrowed.height : 0)

                            ItemDelegate {
                                id: row
                                objectName: "menuSheetRow"
                                /// The menu's entry this row stands for
                                readonly property var menuEntry: entry.source
                                visible: (entry.kind === "item" || entry.kind === "submenu") && menuEntry.offered !== false
                                /// A second line under the name (an entry's `detail`: the look-up menu's web addresses)
                                readonly property string detail: menuEntry && typeof menuEntry.detail === "string" ? menuEntry.detail : ""
                                width: parent.width
                                height: sheet.rowHeight + (detail !== "" ? 18 : 0)
                                bottomPadding: detail !== "" ? 24 : 6
                                enabled: menuEntry.enabled
                                highlighted: sheet.keyIndex === entry.index
                                text: menuEntry.text !== undefined ? menuEntry.text : ""
                                icon.source: menuEntry.icon ? menuEntry.icon.source : ""
                                icon.color: menuEntry.icon ? menuEntry.icon.color : "transparent"
                                font: menuEntry.font
                                leftPadding: 20
                                rightPadding: 20 + (marker.visible ? marker.width + 12 : 0)
                                focusPolicy: Qt.NoFocus
                                onClicked: sheet.activate(menuEntry)
                                Label {
                                    objectName: "menuSheetRowDetail"
                                    visible: row.detail !== ""
                                    text: row.detail
                                    x: row.leftPadding
                                    width: row.width - row.leftPadding - row.rightPadding
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 8
                                    elide: Text.ElideRight
                                    font.pixelSize: 11
                                    color: "#6b6f75"
                                }
                                // A check mark (a checked choice) or the arrow of a submenu
                                Item {
                                    id: marker
                                    visible: entry.kind === "submenu" || (row.menuEntry.checkable === true && row.menuEntry.checked === true)
                                    anchors.right: parent.right
                                    anchors.rightMargin: 20
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 22
                                    height: 22
                                    opacity: row.enabled ? 1 : 0.4
                                    Image {
                                        visible: entry.kind === "submenu"
                                        anchors.centerIn: parent
                                        source: visible ? app.iconUrl("xqt-chevron-right") : ""
                                        sourceSize.width: 20
                                        sourceSize.height: 20
                                    }
                                    Label {
                                        visible: entry.kind !== "submenu"
                                        anchors.centerIn: parent
                                        text: "✓"
                                        font.pixelSize: 18
                                        color: Material.accentColor
                                    }
                                }
                            }
                            Hairline {
                                visible: entry.kind === "separator"
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                color: "#e2e5e9"
                            }
                            Label {
                                id: caption
                                visible: entry.kind === "caption"
                                width: parent.width
                                text: entry.kind === "caption" ? entry.source.text : ""
                                leftPadding: 20
                                rightPadding: 20
                                topPadding: 8
                                bottomPadding: 4
                                wrapMode: Text.Wrap
                                font.pixelSize: 13
                                color: "#6b6f75"
                            }
                            // A row of controls of the menu (e.g. the columns of the layout menu), shown here while
                            // the sheet shows the menu, then given back
                            Item {
                                id: holder
                                width: parent.width
                                height: borrowed ? borrowed.height : 0
                                property Item borrowed: null
                                property Item home: null
                                property real homeX: 0
                                property real homeY: 0
                                Component.onCompleted: {
                                    if (entry.kind !== "custom") return
                                    home = entry.source.parent
                                    homeX = entry.source.x
                                    homeY = entry.source.y
                                    borrowed = entry.source
                                    borrowed.parent = holder
                                    borrowed.x = 0
                                    borrowed.y = 0
                                }
                                Component.onDestruction: {
                                    if (!borrowed) return
                                    borrowed.parent = home
                                    borrowed.x = homeX
                                    borrowed.y = homeY
                                    borrowed = null
                                }
                            }
                        }
                    }
                }
            }
            Item { implicitHeight: 8 + sheet.safeBottom }
        }
    }
}
