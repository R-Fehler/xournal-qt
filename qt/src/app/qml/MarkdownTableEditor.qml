// The table editor of the formatting bar (qt/docs/md-editor.md, "Tables"): a table as a grid of cells, edited as in
// a word processor instead of typing pipes. Tab / Shift+Tab go to the next / previous cell (Tab in the last cell adds
// a row), the arrows go to the cell next to it at the edge of a cell's text, Enter to the cell below, Ctrl+Enter is
// OK. Rows and columns are added and removed with the buttons or a cell's menu (right-click, press and hold), and a
// column is aligned left, centered or right. The current row and column are tinted and named ("Row 3, Column 2").
// OK writes a GFM pipe table (app.writeMarkdownTable: the columns padded to line up, a "|" in a cell escaped) over
// the table at the cursor or as a new one, as one undo step; Cancel changes nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: editor
    objectName: named("tableEditor")
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }
    parent: Overlay.overlay
    modal: true
    anchors.centerIn: parent
    // As big as the grid (with room for a column and a row more), within the window
    width: Math.min(parent ? parent.width - 24 : 720, Math.max(480, grid.width + 200))
    height: Math.min(parent ? parent.height - 24 : 560, Math.max(320, grid.height + 200))
    padding: 12
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        radius: 12
        color: "#ffffff"
        border.width: 1
        border.color: "#d5d8dc"
    }

    /// The cells: rows of texts, row 0 the header
    property var cells: []
    /// One per column: "left", "center", "right" or "" (none)
    property var aligns: []
    property int rowCount: 0
    property int columnCount: 0
    /// The cell with the cursor (row 0: the header)
    property int row: 0
    property int column: 0
    /// It edits a table of the text (else it inserts a new one)
    property bool editing: false
    /// OK: called with the cells and the alignments
    property var onAccept: null
    /// New arrays for the Repeaters each time the grid changes (they make their cells again)
    property var rowKeys: []
    property var columnKeys: []

    /// Opens with the table at the cursor (app.markdownTable()), or a new one of 3 columns and 2 rows when there is
    /// none; `accept(cells, aligns)` writes it.
    function openFor(found, accept) {
        if (found && found.found) {
            cells = found.cells.map(function(r) { return r.map(function(c) { return String(c) }) })
            aligns = found.aligns.map(function(a) { return String(a) })
            row = found.row
            column = found.column
            editing = true
        } else {
            cells = [["", "", ""], ["", "", ""], ["", "", ""]]
            aligns = ["", "", ""]
            row = 0
            column = 0
            editing = false
        }
        onAccept = accept
        rebuild()
        open()
    }
    function rebuild() {
        rowCount = cells.length
        columnCount = cells.length > 0 ? cells[0].length : 0
        row = Math.max(0, Math.min(row, rowCount - 1))
        column = Math.max(0, Math.min(column, columnCount - 1))
        rowKeys = cells.map(function(r, i) { return i })
        columnKeys = aligns.map(function(a, i) { return i })
        Qt.callLater(focusCell)
    }
    function cellAt(r, c) {
        const rowItem = rows.itemAt(r)
        return rowItem ? rowItem.cellAt(c) : null
    }
    function focusCell() {
        const cell = cellAt(row, column)
        if (cell) cell.forceActiveFocus()
    }
    /// To a cell (the cursor at the start or the end of its text)
    function go(r, c, atEnd) {
        if (r < 0 || r >= rowCount || c < 0 || c >= columnCount) return
        row = r
        column = c
        const cell = cellAt(r, c)
        if (cell) {
            cell.forceActiveFocus()
            cell.cursorPosition = atEnd ? cell.length : 0
        }
    }
    /// The next (or previous) cell, row by row; Tab in the last cell adds a row
    function step(forward) {
        let r = row, c = column + (forward ? 1 : -1)
        if (c >= columnCount) { c = 0; r++ }
        if (c < 0) { c = columnCount - 1; r-- }
        if (r >= rowCount && forward) { insertRow(rowCount); r = rowCount - 1; c = 0; column = 0; return }
        if (r < 0) return
        go(r, c, !forward)
        const cell = cellAt(r, c)
        if (cell) cell.selectAll()
    }

    // --- rows and columns ------------------------------------------------------------------------------------------
    function insertRow(at) {
        at = Math.max(1, Math.min(at, rowCount))  // (never above the header)
        const r = []
        for (let c = 0; c < columnCount; ++c) r.push("")
        cells.splice(at, 0, r)
        row = at
        rebuild()
    }
    function removeRow(at) {
        if (at <= 0 || rowCount <= 1) return  // (the header stays)
        cells.splice(at, 1)
        row = Math.min(at, rowCount - 2)
        rebuild()
    }
    function insertColumn(at) {
        at = Math.max(0, Math.min(at, columnCount))
        for (let r = 0; r < rowCount; ++r) cells[r].splice(at, 0, "")
        aligns.splice(at, 0, "")
        column = at
        rebuild()
    }
    function removeColumn(at) {
        if (columnCount <= 1) return
        for (let r = 0; r < rowCount; ++r) cells[r].splice(at, 1)
        aligns.splice(at, 1)
        column = Math.min(at, columnCount - 2)
        rebuild()
    }
    function setAlign(a) {
        const copy = aligns.slice()
        copy[column] = copy[column] === a ? "" : a
        aligns = copy
        focusCell()
    }
    function openMenu(item, x, y) { cellMenu.popup(item, x, y) }
    function accept() {
        const writer = onAccept
        const out = cells.map(function(r) { return r.slice() })
        const al = aligns.slice()
        close()
        if (writer) writer(out, al)
    }

    readonly property string place: row === 0 ? qsTr("Header row, column %1").arg(column + 1)
                                              : qsTr("Row %1, Column %2").arg(row).arg(column + 1)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: editor.editing ? qsTr("Edit table") : qsTr("Insert table")
                font.weight: Font.DemiBold
                font.pixelSize: 16
            }
            Label {
                objectName: editor.named("tablePlace")
                Layout.fillWidth: true
                Layout.leftMargin: 12
                text: editor.place
                color: Material.accentColor
                elide: Text.ElideRight
            }
        }

        // Rows, columns, alignment of the current column
        Flow {
            Layout.fillWidth: true
            spacing: 2
            component TableButton: IconButton {
                implicitWidth: 40
                implicitHeight: 40
                icon.width: 20
                icon.height: 20
                focusPolicy: Qt.NoFocus
            }
            TableButton { objectName: editor.named("tableAddRow"); iconName: "xqt-row-add"; tip: qsTr("Add a row below this one"); onClicked: editor.insertRow(editor.row + 1) }
            TableButton { objectName: editor.named("tableRemoveRow"); iconName: "xqt-row-remove"; tip: qsTr("Remove this row"); enabled: editor.row > 0; onClicked: editor.removeRow(editor.row) }
            TableButton { objectName: editor.named("tableAddColumn"); iconName: "xqt-column-add"; tip: qsTr("Add a column right of this one"); onClicked: editor.insertColumn(editor.column + 1) }
            TableButton { objectName: editor.named("tableRemoveColumn"); iconName: "xqt-column-remove"; tip: qsTr("Remove this column"); enabled: editor.columnCount > 1; onClicked: editor.removeColumn(editor.column) }
            ToolSeparator {}
            TableButton { objectName: editor.named("tableAlignLeft"); iconName: "xqt-align-left"; tip: qsTr("Align the column left"); checked: editor.aligns[editor.column] === "left"; onClicked: editor.setAlign("left") }
            TableButton { objectName: editor.named("tableAlignCenter"); iconName: "xqt-align-center"; tip: qsTr("Center the column"); checked: editor.aligns[editor.column] === "center"; onClicked: editor.setAlign("center") }
            TableButton { objectName: editor.named("tableAlignRight"); iconName: "xqt-align-right"; tip: qsTr("Align the column right"); checked: editor.aligns[editor.column] === "right"; onClicked: editor.setAlign("right") }
        }

        // The grid: column numbers on top, row numbers at the left, the current ones tinted
        Flickable {
            id: gridFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: grid.width
            contentHeight: grid.height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
            readonly property int cellWidth: 150
            readonly property int labelWidth: 44

            Column {
                id: grid
                spacing: 0
                Row {
                    Item { width: gridFlick.labelWidth; height: 32 }
                    Repeater {
                        model: editor.columnKeys
                        // The column's number and alignment; a tap: its menu
                        delegate: ItemDelegate {
                            id: columnHead
                            required property int index
                            width: gridFlick.cellWidth
                            height: 32
                            padding: 0
                            focusPolicy: Qt.NoFocus
                            readonly property string align: editor.aligns[index] || ""
                            readonly property bool current: index === editor.column
                            contentItem: Label {
                                text: qsTr("Column %1").arg(columnHead.index + 1)
                                      + (columnHead.align === "left" ? " · " + qsTr("left")
                                         : columnHead.align === "center" ? " · " + qsTr("centered")
                                         : columnHead.align === "right" ? " · " + qsTr("right") : "")
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                                font.bold: columnHead.current
                                color: columnHead.current ? Material.accentColor : "#5f6368"
                            }
                            background: Rectangle { color: columnHead.current ? "#e8ecf8" : "transparent"; radius: 4 }
                            onClicked: { editor.column = index; editor.openMenu(columnHead, 0, height) }
                        }
                    }
                }
                Repeater {
                    id: rows
                    model: editor.rowKeys
                    delegate: Row {
                        id: rowItem
                        required property int index
                        function cellAt(c) { return cellRepeater.itemAt(c) }
                        // The row's number (the header: "H"); a tap: its menu
                        ItemDelegate {
                            id: rowHead
                            width: gridFlick.labelWidth
                            height: 40
                            padding: 0
                            focusPolicy: Qt.NoFocus
                            readonly property bool current: rowItem.index === editor.row
                            contentItem: Label {
                                text: rowItem.index === 0 ? qsTr("H") : rowItem.index
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                                font.bold: rowHead.current
                                color: rowHead.current ? Material.accentColor : "#5f6368"
                            }
                            background: Rectangle { color: rowHead.current ? "#e8ecf8" : "transparent"; radius: 4 }
                            onClicked: { editor.row = rowItem.index; editor.openMenu(rowHead, width, 0) }
                        }
                        Repeater {
                            id: cellRepeater
                            model: editor.columnKeys
                            delegate: TextField {
                                id: cell
                                required property int index
                                readonly property int r: rowItem.index
                                readonly property int c: index
                                readonly property bool current: r === editor.row && c === editor.column
                                objectName: editor.named("tableCell") + r + "_" + c
                                width: gridFlick.cellWidth
                                height: 40
                                leftPadding: 8
                                rightPadding: 8
                                topPadding: 0
                                bottomPadding: 0
                                verticalAlignment: TextInput.AlignVCenter
                                text: editor.cells[r] ? editor.cells[r][c] : ""
                                font.bold: r === 0
                                selectByMouse: true
                                horizontalAlignment: editor.aligns[c] === "center" ? TextInput.AlignHCenter
                                                   : editor.aligns[c] === "right" ? TextInput.AlignRight : TextInput.AlignLeft
                                background: Rectangle {
                                    color: cell.r === 0 ? (cell.current ? "#dde3f8" : cell.r === editor.row || cell.c === editor.column ? "#e8ecf8" : "#f1f3f4")
                                                        : cell.current ? "#e3e8fb" : cell.r === editor.row || cell.c === editor.column ? "#f3f5fd" : "#ffffff"
                                    border.width: cell.current ? 2 : 1
                                    border.color: cell.current ? Material.accentColor : "#d5d8dc"
                                }
                                onTextEdited: editor.cells[r][c] = text
                                onActiveFocusChanged: if (activeFocus) { editor.row = r; editor.column = c }
                                onPressAndHold: { editor.row = r; editor.column = c; editor.openMenu(cell, 0, height) }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                                    onTapped: function(point) {
                                        editor.row = cell.r
                                        editor.column = cell.c
                                        editor.openMenu(cell, point.position.x, point.position.y)
                                    }
                                }
                                Keys.onPressed: function(event) {
                                    const ctrl = event.modifiers & Qt.ControlModifier
                                    if (event.key === Qt.Key_Tab) {
                                        editor.step(true); event.accepted = true
                                    } else if (event.key === Qt.Key_Backtab) {
                                        editor.step(false); event.accepted = true
                                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && ctrl) {
                                        editor.accept(); event.accepted = true
                                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                        editor.go(r + 1, c, true); event.accepted = true
                                    } else if (event.key === Qt.Key_Up) {
                                        editor.go(r - 1, c, true); event.accepted = true
                                    } else if (event.key === Qt.Key_Down) {
                                        editor.go(r + 1, c, true); event.accepted = true
                                    } else if (event.key === Qt.Key_Left && cursorPosition === 0 && selectedText === "") {
                                        editor.go(r, c - 1, true); event.accepted = true
                                    } else if (event.key === Qt.Key_Right && cursorPosition === length && selectedText === "") {
                                        editor.go(r, c + 1, false); event.accepted = true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Tab: next cell · Enter: cell below · Ctrl+Enter: OK")
                color: "#5f6368"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Button { objectName: editor.named("tableCancel"); text: qsTr("Cancel"); flat: true; onClicked: editor.close() }
            Button {
                objectName: editor.named("tableOk")
                text: editor.editing ? qsTr("OK") : qsTr("Insert")
                highlighted: true
                onClicked: editor.accept()
            }
        }
    }

    // A cell's, a row's or a column's menu
    Menu {
        id: cellMenu
        objectName: editor.named("tableCellMenu")
        onClosed: editor.focusCell()
        MenuItem { text: qsTr("Row above"); enabled: editor.row > 0; onTriggered: editor.insertRow(editor.row) }
        MenuItem { text: qsTr("Row below"); onTriggered: editor.insertRow(editor.row + 1) }
        MenuItem { text: qsTr("Remove row"); enabled: editor.row > 0; onTriggered: editor.removeRow(editor.row) }
        MenuSeparator {}
        MenuItem { text: qsTr("Column left"); onTriggered: editor.insertColumn(editor.column) }
        MenuItem { text: qsTr("Column right"); onTriggered: editor.insertColumn(editor.column + 1) }
        MenuItem { text: qsTr("Remove column"); enabled: editor.columnCount > 1; onTriggered: editor.removeColumn(editor.column) }
        MenuSeparator {}
        MenuItem { text: qsTr("Align left"); checkable: true; checked: editor.aligns[editor.column] === "left"; onTriggered: editor.setAlign("left") }
        MenuItem { text: qsTr("Center"); checkable: true; checked: editor.aligns[editor.column] === "center"; onTriggered: editor.setAlign("center") }
        MenuItem { text: qsTr("Align right"); checkable: true; checked: editor.aligns[editor.column] === "right"; onTriggered: editor.setAlign("right") }
    }
}
