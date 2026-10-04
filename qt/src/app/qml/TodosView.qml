// The library's To-dos view (home screen, qt/docs/todos.md): the to-dos of all documents of the library (task lines
// "- [ ] …" of Markdown boxes, sticky notes, Markdown files, PDF text documents; which of them: Settings → To-dos),
// grouped by document (or folder, or not at all) with the count per group, sorted by due date, document or last
// change, filtered by state, due date, a text and the library's current folder. A tap opens the document at the
// to-do's line; its check box ticks it in the document. It follows the library's "Show" filter and its Favourites
// chip; the list comes from the library's index (no document is opened to make it).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

FocusScope {
    id: view
    objectName: "todosView"
    /// Shown now: the list follows the index while it is
    property bool shown: false
    readonly property var todos: app.libraryTodos
    /// Room below the last to-do (the home screen's floating "+" on a phone)
    property real bottomSpace: 0
    /// A phone class: the filters scroll sideways in one row, the rows are taller
    readonly property bool phone: typeof win !== "undefined" && win && win.adaptive
                                  ? ["phonePortrait", "phoneShort", "tiny"].indexOf(win.adaptive.layoutClass) >= 0 : false
    readonly property int rowHeight: phone ? 52 : 44

    Binding { target: view.todos; property: "active"; value: view.shown }

    function statusLabel(s) { return s === "done" ? qsTr("Done") : s === "all" ? qsTr("Open and done") : qsTr("Open") }
    function dueLabel(d) {
        return d === "overdue" ? qsTr("Overdue") : d === "today" ? qsTr("Due today") : d === "week" ? qsTr("Due this week")
             : d === "none" ? qsTr("No due date") : qsTr("Any due date")
    }
    function groupLabel(g) { return g === "folder" ? qsTr("By folder") : g === "none" ? qsTr("Not grouped") : qsTr("By document") }
    function sortLabel(s) { return s === "document" ? qsTr("Document") : s === "changed" ? qsTr("Last changed") : qsTr("Due date") }
    /// A due date as the list shows it: "Today", "Tomorrow", the weekday this week, else the date
    function dueText(due) {
        if (due === "") return ""
        const d = new Date(due + "T00:00:00")
        if (isNaN(d.getTime())) return due
        const today = new Date()
        today.setHours(0, 0, 0, 0)
        const days = Math.round((d.getTime() - today.getTime()) / 86400000)
        if (days === 0) return qsTr("Today")
        if (days === 1) return qsTr("Tomorrow")
        if (days === -1) return qsTr("Yesterday")
        if (days > 1 && days < 7) return d.toLocaleDateString(Qt.locale(), "dddd")
        return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    // A filter or sort choice: its current value on a button, the choices in its menu
    component Choice: ToolButton {
        id: choice
        property string value
        property var options: []  // [{ value, text }]
        property var labelOf: function(v) { return v }
        signal picked(string value)
        text: labelOf(value) + " ▾"
        font.pixelSize: 13
        implicitHeight: view.phone ? 44 : 36
        onClicked: Popups.openAt(choiceMenu)
        background: Rectangle {
            radius: height / 2
            color: choice.pressed ? "#d5d8dc" : "#e8eaed"
        }
        AdaptiveMenu {
            id: choiceMenu
            objectName: choice.objectName + "Menu"
            title: choice.Accessible.name
            Repeater {
                model: choice.options
                delegate: AdaptiveMenuItem {
                    required property var modelData
                    objectName: choice.objectName + "_" + modelData.value
                    text: modelData.text
                    checkable: true
                    checked: choice.value === modelData.value
                    onTriggered: choice.picked(modelData.value)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // --- the filters: a text, the state, the due date, this folder; grouping and sorting ---
        Flickable {
            id: filterFlick
            Layout.fillWidth: true
            Layout.preferredHeight: filterRow.implicitHeight
            contentWidth: filterRow.implicitWidth
            contentHeight: filterRow.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            RowLayout {
                id: filterRow
                width: Math.max(implicitWidth, filterFlick.width)
                spacing: 6
                TextField {
                    id: queryField
                    objectName: "todosQueryField"
                    Layout.preferredWidth: view.phone ? 170 : 240
                    Layout.fillWidth: !view.phone
                    Layout.maximumWidth: 360
                    placeholderText: qsTr("Filter to-dos")
                    text: view.todos.query
                    selectByMouse: true
                    onTextEdited: view.todos.query = text
                    Keys.onEscapePressed: { text = ""; view.todos.query = "" }
                }
                Choice {
                    objectName: "todosStatus"
                    Accessible.name: qsTr("Show")
                    value: view.todos.status
                    labelOf: view.statusLabel
                    options: [{ value: "open", text: qsTr("Open") }, { value: "done", text: qsTr("Done") },
                              { value: "all", text: qsTr("Open and done") }]
                    onPicked: function(v) { view.todos.status = v }
                }
                Choice {
                    objectName: "todosDue"
                    Accessible.name: qsTr("Due")
                    value: view.todos.due
                    labelOf: view.dueLabel
                    options: [{ value: "any", text: qsTr("Any due date") }, { value: "overdue", text: qsTr("Overdue") },
                              { value: "today", text: qsTr("Due today") }, { value: "week", text: qsTr("Due this week") },
                              { value: "none", text: qsTr("No due date") }]
                    onPicked: function(v) { view.todos.due = v }
                }
                ToolButton {
                    id: folderOnly
                    objectName: "todosFolderOnly"
                    // (the library's current folder: where its breadcrumbs are)
                    enabled: app.library.folder !== ""
                    checkable: true
                    checked: view.todos.folderOnly
                    onToggled: view.todos.folderOnly = checked
                    text: app.library.folder !== "" ? qsTr("Only in %1").arg(app.library.folder.split("/").pop())
                                                    : qsTr("Only in this folder")
                    font.pixelSize: 13
                    implicitHeight: view.phone ? 44 : 36
                    background: Rectangle {
                        radius: height / 2
                        color: folderOnly.checked ? "#d2e3fc" : (folderOnly.pressed ? "#d5d8dc" : "#e8eaed")
                    }
                }
                Item { Layout.fillWidth: true }
                Choice {
                    objectName: "todosGrouping"
                    Accessible.name: qsTr("Group")
                    value: view.todos.grouping
                    labelOf: view.groupLabel
                    options: [{ value: "document", text: qsTr("By document") }, { value: "folder", text: qsTr("By folder") },
                              { value: "none", text: qsTr("Not grouped") }]
                    onPicked: function(v) { view.todos.grouping = v }
                }
                Choice {
                    objectName: "todosSort"
                    Accessible.name: qsTr("Sort")
                    value: view.todos.sortBy
                    labelOf: function(v) { return qsTr("Sort: %1").arg(view.sortLabel(v)) }
                    options: [{ value: "due", text: qsTr("By due date") }, { value: "document", text: qsTr("By document") },
                              { value: "changed", text: qsTr("Last changed first") }]
                    onPicked: function(v) { view.todos.sortBy = v }
                }
                // Export the open to-dos listed: to a calendar (.ics), or as a Markdown list (one way)
                IconButton {
                    id: todosMore
                    objectName: "todosMoreButton"
                    label: qsTr("More")
                    iconName: "xqt-more"
                    tip: qsTr("Export the open to-dos")
                    onClicked: Popups.openAt(todosMenu)
                    AdaptiveMenu {
                        id: todosMenu
                        objectName: "todosMenu"
                        title: qsTr("To-dos")
                        AdaptiveMenuItem {
                            objectName: "exportTodosIcsItem"
                            text: qsTr("Export open to-dos for a calendar (.ics)…")
                            enabled: view.todos.count > 0
                            onTriggered: { exportDialog.calendar = true; exportDialog.open() }
                        }
                        AdaptiveMenuItem {
                            objectName: "exportTodosMdItem"
                            text: qsTr("Export open to-dos as Markdown (.md)…")
                            enabled: view.todos.count > 0
                            onTriggered: { exportDialog.calendar = false; exportDialog.open() }
                        }
                    }
                }
            }
        }

        ListView {
            id: list
            objectName: "todosList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: view.todos
            spacing: 0
            bottomMargin: view.bottomSpace
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            TouchpadMomentum { flickable: list }
            section.property: "group"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                id: header
                required property string section
                // (the first to-do of the group says what the group is)
                readonly property var info: view.todos.groupOf(section)
                objectName: "todoGroup"
                width: list.width
                height: section === "" ? 0 : 38
                visible: section !== ""
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 8
                    anchors.topMargin: 8
                    spacing: 6
                    Label {
                        objectName: "todoGroupLabel"
                        text: header.info.label || ""
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#202124"
                        elide: Text.ElideRight
                        Layout.maximumWidth: list.width * 0.6
                    }
                    Label {
                        visible: (header.info.folder || "") !== "" && view.todos.grouping === "document"
                        text: header.info.folder || ""
                        font.pixelSize: 12
                        color: "#80868b"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Item { Layout.fillWidth: true; visible: !((header.info.folder || "") !== "" && view.todos.grouping === "document") }
                    Label {
                        objectName: "todoGroupCount"
                        text: String(header.info.count || 0)
                        font.pixelSize: 12
                        color: "#5f6368"
                        leftPadding: 8
                        rightPadding: 8
                        background: Rectangle { radius: height / 2; color: "#e8eaed" }
                    }
                }
            }
            delegate: ItemDelegate {
                id: row
                required property int index
                required property var model
                objectName: "todoRow"
                width: list.width
                implicitHeight: Math.max(view.rowHeight, rowContent.implicitHeight + 8)
                leftPadding: 4
                rightPadding: 8
                Accessible.name: model.text
                onClicked: app.openTodo(model.path, model.rawText, model.occurrence, model.page)
                onPressAndHold: rowMenu.openMenu()
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) { rowMenu.openMenu(point.position) }
                }
                AdaptiveMenu {
                    id: rowMenu
                    objectName: "todoMenu"
                    title: row.model.text
                    AdaptiveMenuItem {
                        objectName: "openTodoItem"
                        text: qsTr("Open at this line")
                        onTriggered: app.openTodo(row.model.path, row.model.rawText, row.model.occurrence, row.model.page)
                    }
                    AdaptiveMenuItem {
                        objectName: "addTodoToCalendarItem"
                        text: qsTr("Add to calendar")
                        offered: row.model.due !== ""
                        onTriggered: app.addTodoToCalendar(view.todos.get(row.index))
                    }
                    AdaptiveMenuItem {
                        objectName: "toggleTodoItem"
                        text: row.model.done ? qsTr("Mark as open") : qsTr("Mark as done")
                        onTriggered: app.setTodoDone(row.model.path, row.model.rawText, row.model.occurrence, !row.model.done)
                    }
                }
                contentItem: RowLayout {
                    id: rowContent
                    spacing: 6
                    CheckBox {
                        id: check
                        objectName: "todoCheck"
                        checked: row.model.done
                        // (the document is changed; the list follows the index)
                        onClicked: app.setTodoDone(row.model.path, row.model.rawText, row.model.occurrence, checked)
                        Accessible.name: row.model.done ? qsTr("Mark as open") : qsTr("Mark as done")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        // A stamp: the handwriting beside its check box, as a picture (its line of the page)
                        Image {
                            objectName: "todoInk"
                            visible: row.model.picture !== ""
                            readonly property real aspect: row.model.place.size > 0
                                ? Math.max(1, (row.model.place.pageWidth * 0.96 - row.model.place.x) / (4.5 * row.model.place.size)) : 8
                            Layout.preferredHeight: view.phone ? 40 : 34
                            Layout.preferredWidth: Math.min(Layout.preferredHeight * aspect, list.width - 120)
                            Layout.maximumWidth: list.width - 120
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            asynchronous: true
                            source: view.shown && visible ? row.model.picture : ""
                            sourceSize.width: Math.ceil(Layout.preferredWidth * Screen.devicePixelRatio)
                            opacity: row.model.done ? 0.5 : 1
                        }
                        Label {
                            objectName: "todoText"
                            visible: row.model.picture === "" || row.model.text !== ""
                            Layout.fillWidth: true
                            text: row.model.text !== "" ? row.model.text
                                                        : (row.model.stamp ? qsTr("Handwritten to-do") : qsTr("(no text)"))
                            font.pixelSize: 14
                            font.strikeout: row.model.done
                            font.italic: row.model.text === ""
                            color: row.model.done ? "#80868b" : "#202124"
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                        }
                        // Where it is: its document (when the groups do not say it) and its page
                        Label {
                            objectName: "todoWhere"
                            Layout.fillWidth: true
                            text: {
                                const page = row.model.page >= 0 ? qsTr("page %1").arg(row.model.page + 1) : ""
                                const doc = view.todos.grouping === "document" ? "" : row.model.name
                                return [doc, page].filter(function(s) { return s !== "" }).join(" · ")
                            }
                            visible: text !== ""
                            font.pixelSize: 12
                            color: "#80868b"
                            elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        objectName: "todoDue"
                        visible: row.model.due !== ""
                        text: view.dueText(row.model.due)
                        font.pixelSize: 12
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 2
                        bottomPadding: 2
                        readonly property string state: row.model.done ? "" : row.model.dueState
                        color: state === "overdue" ? "#b3261e" : state === "today" ? "#8a5a00" : "#3c4043"
                        background: Rectangle {
                            radius: height / 2
                            color: parent.state === "overdue" ? "#fce8e6" : parent.state === "today" ? "#fdf1d0"
                                 : parent.state === "week" ? "#e8f0fe" : "#f1f3f4"
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        objectName: "todosExportDialog"
        property bool calendar: true
        title: calendar ? qsTr("Export open to-dos for a calendar") : qsTr("Export open to-dos as Markdown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: calendar ? "ics" : "md"
        nameFilters: calendar ? [qsTr("Calendar (*.ics)")] : [qsTr("Markdown (*.md)")]
        onAccepted: app.exportTodos(selectedFile)
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: 10
        width: Math.min(parent.width - 40, 460)
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: app.iconUrl("xqt-list-todo")
            sourceSize.width: 56; sourceSize.height: 56
            opacity: 0.5
        }
        Label {
            objectName: "todosEmpty"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: 16
            color: "#5f6368"
            text: view.todos.total > 0 ? qsTr("No to-dos match") : qsTr("No to-dos yet")
        }
        Label {
            visible: view.todos.total === 0
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#80868b"
            text: view.todos.collectAll
                  ? qsTr("Check box lines of Markdown (- [ ] …) in notes, sticky notes and Markdown files are listed here.")
                  : qsTr("Lines like “- [ ] %1 call the lab 📅 2026-10-12” in Markdown boxes, sticky notes and "
                         + "Markdown files are listed here (Settings → To-dos).").arg(view.todos.marker)
        }
    }
}
