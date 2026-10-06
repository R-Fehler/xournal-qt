// Every tool of the phone chrome in one sheet, "My tools" (qt/docs/adaptive-layout.md, "The phone chrome";
// qt/docs/toolbox.md, "On a phone"): the dock shows the first tools of the toolbox; its "My tools" button opens this.
// The user's tools first (a tap takes one, a tap on the one in hand edits it, a long press: its menu), "+", then the
// other tools: each tool and each variant of a cycling button (ToolGroups.qml) is a cell with its icon and its name,
// one tap takes it. The other buttons of the command bar (insert, search, present, settings, new, open, save, …)
// follow; a tap on them is a tap on the bar's button, a long press is its long press (Add a page: Insert pages…;
// Present: without controls; Write on the page: its source).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

BottomSheet {
    id: sheet
    objectName: "phoneToolSheet"
    title: qsTr("My tools")

    /// The command bar's buttons by name (Main.qml's toolArea.slots)
    readonly property var slots: toolArea.slots
    readonly property var groups: win.toolGroups
    /// The group's button (a fixed tool of the toolbox)
    readonly property var groupSlot: ({ select: "select", snip: "snip", geometry: "geometry", text: "pdfText" })

    function variantCells(group) {
        return groups.variants(group).map(function(v) { return { group: group, key: v.key } })
    }
    readonly property var sections: [
        { name: "draw", title: qsTr("Other tools"),
          cells: [{ slot: "hand" }, { slot: "touchDrawing" }, { slot: "write" }].concat(variantCells("text"), [{ slot: "emoji" }]) },
        // (the snips beside the selection: the same rectangle or lasso, its picture copied)
        { name: "select", title: qsTr("Select and snip"), cells: variantCells("select").concat(variantCells("snip")) },
        { name: "geometry", title: qsTr("Setsquare, compass and curtain"), cells: variantCells("geometry") },
        { name: "insert", title: qsTr("Insert"), cells: [{ slot: "image" }, { slot: "sticker" }, { slot: "record" }, { slot: "addPage" }] },
        { name: "document", title: qsTr("Document and view"),
          cells: [{ slot: "search" }, { slot: "present" }, { slot: "fullScreen" }, { slot: "settings" }, { slot: "new" },
                  { slot: "open" }, { slot: "save" }, { slot: "editAsNotes" }, { slot: "openExternally" }] }
    ]

    function buttonOf(cell) { return slots[cell.slot !== undefined ? cell.slot : groupSlot[cell.group]] }
    function offered(cell) {
        const b = buttonOf(cell)
        return b !== undefined && b !== null && b.offered !== false
    }
    /// A cell was tapped: its variant, or the button's tap (once the sheet is gone)
    function take(cell) {
        close()
        if (cell.slot === undefined) {
            groups.activate(cell.group, cell.key)
            return
        }
        const b = buttonOf(cell)
        Qt.callLater(function() { b.clicked() })
    }
    /// ... held (or right-clicked): the button's own long press, if it has one
    function hold(cell) {
        const b = buttonOf(cell)
        // (a variant: only the text tools' own list, which also says how PDF text is marked)
        if (cell.slot === undefined ? cell.group !== "text" : !b.ownHold) return false
        close()
        Qt.callLater(function() { b.pressAndHold() })
        return true
    }

    Column {
        id: grid
        width: parent.width
        bottomPadding: 4
        readonly property int columns: Math.max(4, Math.floor((width - 16) / 76))
        readonly property real cellWidth: Math.floor((width - 16) / columns)
        // The user's tools (the toolbox), and "+"
        Column {
            objectName: "phoneToolSection_mine"
            visible: !win.textDoc
            width: parent.width
            Flow {
                x: 8
                width: parent.width - 16
                Repeater {
                    model: (app.toolbox.revision, app.toolbox.tools())
                    delegate: Item {
                        id: mine
                        required property var modelData
                        width: grid.cellWidth
                        height: 76
                        ToolEntryButton {
                            objectName: "sheetEntry_" + mine.modelData.id
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 2
                            cell: 48
                            reorderable: false
                            entry: mine.modelData
                            name: win.toolEntryName(mine.modelData)
                            inkColor: (app.colorPalette, app.toolbox.revision, app.toolEntryColor(mine.modelData))
                            inHand: toolboxPane.inHand(mine.modelData)
                            towardsPage: "up"
                            onClicked: {
                                const e = mine.modelData
                                const editing = toolboxPane.inHand(e)
                                sheet.close()
                                if (editing) Qt.callLater(function() { toolEditor.openFor(e, toolboxPane, "bottom") })
                                else app.applyToolEntry(e.id)
                            }
                            onHeld: {
                                const e = mine.modelData
                                sheet.close()
                                Qt.callLater(function() { toolEntryMenu.openFor(e, toolboxPane, undefined) })
                            }
                            onSecondaryClicked: held(Qt.point(0, 0))
                        }
                        Label {
                            x: 2
                            y: 50
                            width: parent.width - 4
                            horizontalAlignment: Text.AlignHCenter
                            text: win.toolEntryName(mine.modelData)
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            lineHeight: 0.9
                            color: "#3c4043"
                        }
                    }
                }
                AbstractButton {
                    objectName: "sheetAddTool"
                    width: grid.cellWidth
                    height: 76
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        sheet.close()
                        Qt.callLater(function() { toolTypeMenu.ask("add", "", toolboxPane) })
                    }
                    contentItem: Item {
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 12
                            source: app.iconUrl("xqt-plus")
                            sourceSize.width: 26
                            sourceSize.height: 26
                        }
                        Label {
                            y: 50
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Add a tool")
                            font.pixelSize: 11
                            color: "#3c4043"
                        }
                    }
                }
            }
        }
        Repeater {
            model: sheet.sections
            delegate: Column {
                id: section
                required property var modelData
                readonly property var cells: modelData.cells.filter(function(c) { return sheet.offered(c) })
                objectName: "phoneToolSection_" + modelData.name
                visible: cells.length > 0
                width: parent.width
                Label {
                    text: section.modelData.title
                    leftPadding: 20
                    topPadding: 8
                    bottomPadding: 2
                    font.pixelSize: 13
                    color: "#6b6f75"
                }
                Flow {
                    x: 8
                    width: parent.width - 16
                    Repeater {
                        model: section.cells
                        delegate: AbstractButton {
                            id: cellButton
                            required property var modelData
                            readonly property var button: sheet.buttonOf(modelData)
                            readonly property bool isVariant: modelData.slot === undefined
                            readonly property var variant: isVariant ? sheet.groups.variant(modelData.group, modelData.key) : null
                            objectName: isVariant ? "toolCell_" + modelData.group + "_" + modelData.key : "toolCell_" + modelData.slot
                            /// The name under the icon
                            readonly property string name: isVariant ? variant.name : (button ? button.name : "")
                            width: grid.cellWidth
                            height: 72
                            focusPolicy: Qt.NoFocus
                            enabled: !button || button.enabled
                            checked: isVariant ? (variant.curtain === true ? app.curtain === modelData.key
                                                  : sheet.groups.isActive(modelData.group)
                                                    && sheet.groups.current(modelData.group) === modelData.key)
                                               : (button ? button.checked : false)
                            Accessible.name: name
                            onClicked: sheet.take(modelData)
                            onPressAndHold: sheet.hold(modelData)
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                                onTapped: sheet.hold(cellButton.modelData)
                            }
                            background: Rectangle {
                                radius: 12
                                color: cellButton.checked ? "#e0e3f5" : cellButton.pressed ? "#eceef1" : "transparent"
                            }
                            contentItem: Item {
                                opacity: cellButton.enabled ? 1 : 0.4
                                Image {
                                    visible: cellButton.isVariant || (cellButton.button && cellButton.button.iconName !== "")
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 10
                                    source: !visible ? "" : app.iconUrl(cellButton.isVariant ? cellButton.variant.icon
                                                                                              : cellButton.button.iconName)
                                    sourceSize.width: 26
                                    sourceSize.height: 26
                                }
                                Label {  // (the emoji button has a character, not an icon)
                                    visible: !cellButton.isVariant && cellButton.button && cellButton.button.iconName === ""
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 8
                                    text: visible ? cellButton.button.text : ""
                                    font.family: "Xournal Qt Emoji"
                                    font.pixelSize: 22
                                }
                                Label {
                                    x: 2
                                    y: 40
                                    width: parent.width - 4
                                    horizontalAlignment: Text.AlignHCenter
                                    text: cellButton.name
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    font.pixelSize: 11
                                    lineHeight: 0.9
                                    color: cellButton.checked ? Material.accentColor : "#3c4043"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
