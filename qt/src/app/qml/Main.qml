// xournal-qt: main window (first usable version: one document, pen / highlighter / eraser / hand).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import XournalQt
import XournalQt.Canvas

ApplicationWindow {
    id: win
    width: 1280
    height: 900
    visible: true
    title: (app.modified ? "• " : "") + app.title + " — Xournal Qt"
    Material.theme: Material.Light
    Material.accent: Material.Indigo
    color: "#5f6368"

    property var afterDiscardCheck: null
    property bool sidebarShown: width >= 900
    property bool quitting: false

    function withSavedChanges(action) {
        if (!app.modified) {
            action()
            return
        }
        afterDiscardCheck = action
        unsavedDialog.open()
    }
    function openSaveDialog(then) {
        // Upstream Xournal++ suggestion: next to the annotated PDF ("lecture.pdf" -> "lecture.xopp"), else the
        // document's own path, else the default name in the last used folder.
        const suggestion = app.suggestedSaveFile().toString()
        saveDialog.afterSave = then
        if (suggestion !== "") {
            saveDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            saveDialog.selectedFile = suggestion
        }
        saveDialog.open()
    }
    function saveOrAsk(then) {
        if (app.hasFilePath) {
            if (app.save() && then) then()
        } else {
            openSaveDialog(then)
        }
    }

    // Close a tab; unsaved changes are asked about first (with that tab shown).
    function requestCloseTab(index) {
        if (!app.tabModified(index)) {
            app.closeTab(index)
            return
        }
        app.currentTab = index
        withSavedChanges(function() { app.closeTab(app.currentTab) })
    }
    // Quitting: go through the tabs with unsaved changes one by one.
    function closeWindow() {
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            quitting = true
            win.close()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeWindow() })
    }

    onClosing: function(close) {
        if (!quitting && app.modifiedTabs().length > 0) {
            close.accepted = false
            closeWindow()
        }
    }

    Connections {
        target: app
        function onRaiseRequested() {
            if (win.visibility === Window.Minimized) win.showNormal()
            win.raise()
            win.requestActivate()
        }
    }

    header: Column {
      TabStrip {
        width: parent.width
        onCloseRequested: function(index) { requestCloseTab(index) }
      }
      ToolBar {
        width: parent.width
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: 56
        // Scrolls sideways when the window is too narrow for all tools (portrait tablet).
        Flickable {
            id: toolFlick
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            contentWidth: toolRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width
            clip: true
        RowLayout {
            id: toolRow
            height: toolFlick.height
            width: Math.max(toolFlick.width, implicitWidth)
            spacing: 2

            IconButton { iconName: "xopp-sidebar-page-preview"; tip: qsTr("Pages"); checked: sidebarShown; onClicked: sidebarShown = !sidebarShown }
            IconButton { objectName: "pageGridButton"; iconName: "xqt-pages-grid"; tip: qsTr("All pages (Ctrl+Alt+G)"); checked: pageGrid.visible; onClicked: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
            ToolSeparator {}
            IconButton { iconName: "xopp-document-new"; tip: qsTr("New document (new tab)"); onClicked: app.newDocument() }
            IconButton { iconName: "xopp-document-open"; tip: qsTr("Open (in a new tab)"); onClicked: openDialog.open() }
            IconButton { iconName: "xopp-document-save"; tip: qsTr("Save"); onClicked: saveOrAsk(null) }
            ToolSeparator {}
            IconButton { iconName: "xopp-edit-undo"; tip: qsTr("Undo"); enabled: app.canUndo; onClicked: app.undo() }
            IconButton { iconName: "xopp-edit-redo"; tip: qsTr("Redo"); enabled: app.canRedo; onClicked: app.redo() }
            ToolSeparator {}
            IconButton { iconName: "xopp-tool-pencil"; tip: qsTr("Pen"); checked: app.tool === "pen"; onClicked: app.selectTool("pen") }
            IconButton { iconName: "xopp-tool-highlighter"; tip: qsTr("Highlighter"); checked: app.tool === "highlighter"; onClicked: app.selectTool("highlighter") }
            IconButton { iconName: "xopp-tool-eraser"; tip: qsTr("Eraser"); checked: app.tool === "eraser"; onClicked: app.selectTool("eraser") }
            IconButton { iconName: "xopp-hand"; tip: qsTr("Hand"); checked: app.tool === "hand"; onClicked: app.selectTool("hand") }
            ToolSeparator {}
            Repeater {
                model: app.palette.slice(0, 8)
                delegate: AbstractButton {
                    required property color modelData
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setColor(modelData)
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: modelData
                            border.width: Qt.colorEqual(app.color, modelData) ? 3 : 1
                            border.color: Qt.colorEqual(app.color, modelData) ? Material.accentColor : "#9e9e9e"
                        }
                    }
                }
            }
            ToolSeparator {}
            Repeater {
                model: [ { size: 1, dot: 6 }, { size: 2, dot: 10 }, { size: 3, dot: 15 } ]
                delegate: AbstractButton {
                    required property var modelData
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setSize(modelData.size)
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 32; height: 32; radius: 6
                            color: app.size === modelData.size ? "#e0e3f5" : "transparent"
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: modelData.dot; height: modelData.dot; radius: modelData.dot / 2
                            color: "#303030"
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconButton { iconName: "xopp-page-add"; tip: qsTr("Add page after the current one"); onClicked: app.addPageAfterCurrent() }
            ToolSeparator {}
            IconButton { objectName: "searchButton"; iconName: "xqt-search"; tip: qsTr("Search (Ctrl+F)"); checked: searchBar.visible; onClicked: searchBar.visible ? searchBar.closeBar() : searchBar.openBar() }
            IconButton { objectName: "overviewButton"; iconName: "xqt-tabs-grid"; tip: qsTr("All open documents (Ctrl+Shift+E)"); onClicked: tabOverview.open() }
            IconButton { objectName: "settingsButton"; iconName: "xqt-settings"; tip: qsTr("Settings (Ctrl+,)"); onClicked: settingsPage.open() }
        }
        }
      }
    }

    PageSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: 210
        visible: sidebarShown
    }

    DocumentCanvas {
        id: canvas
        objectName: "canvas"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: sidebar.visible ? sidebar.right : parent.left
        clip: true  // zoomed-in pages must not paint over the sidebar
        view: app.view
    }

    // Page and zoom status, floating over the canvas.
    Pane {
        id: viewPill
        visible: !pageGrid.visible
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 24
        padding: 2
        leftPadding: 14
        rightPadding: 4
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f2fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            Label { text: app.pageNumber + " / " + app.pageCount; color: "#505050"; Layout.rightMargin: 6 }
            ToolSeparator {}
            ToolButton { text: "−"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomOut() }
            ToolButton {
                text: app.zoomPercent + " %"
                implicitWidth: 72
                onClicked: app.fitWidth()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit page width")
            }
            ToolButton { text: "+"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomIn() }
        }
    }

    PageGrid {
        id: pageGrid
        objectName: "pageGrid"
        anchors.fill: canvas
    }

    SearchBar {
        id: searchBar
        objectName: "searchBar"
        anchors.top: canvas.top
        anchors.topMargin: 12
        anchors.horizontalCenter: canvas.horizontalCenter
    }

    // Scroll bars over the canvas: wide enough to be dragged with a finger or the pen.
    ScrollBar {
        id: vbar
        orientation: Qt.Vertical
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: hbar.visible ? hbar.height : 0
        visible: canvas.contentHeight > canvas.height + 1 && !pageGrid.visible
        policy: ScrollBar.AlwaysOn
        padding: 6
        minimumSize: 0.05
        size: canvas.contentHeight > 0 ? Math.min(1, canvas.height / canvas.contentHeight) : 1
        position: canvas.contentHeight > 0 ? canvas.contentY / canvas.contentHeight : 0
        onPositionChanged: if (pressed) canvas.scrollTo(canvas.contentX, position * canvas.contentHeight)
        contentItem: Rectangle {
            implicitWidth: vbar.pressed || vbar.hovered ? 10 : 7
            implicitHeight: 48
            radius: width / 2
            // Light handle with a dark outline: visible on the grey background and on white pages.
            color: vbar.pressed ? "#ffffff" : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            opacity: vbar.pressed || vbar.hovered ? 1.0 : 0.9
        }
        background: Rectangle { color: vbar.pressed || vbar.hovered ? "#30ffffff" : "transparent" }
    }
    ScrollBar {
        id: hbar
        orientation: Qt.Horizontal
        anchors.left: canvas.left
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.rightMargin: vbar.visible ? vbar.width : 0
        visible: canvas.contentWidth > canvas.width + 1 && !pageGrid.visible
        policy: ScrollBar.AlwaysOn
        padding: 6
        minimumSize: 0.05
        size: canvas.contentWidth > 0 ? Math.min(1, canvas.width / canvas.contentWidth) : 1
        position: canvas.contentWidth > 0 ? canvas.contentX / canvas.contentWidth : 0
        onPositionChanged: if (pressed) canvas.scrollTo(position * canvas.contentWidth, canvas.contentY)
        contentItem: Rectangle {
            implicitWidth: 48
            implicitHeight: hbar.pressed || hbar.hovered ? 10 : 7
            radius: height / 2
            // Light handle with a dark outline: visible on the grey background and on white pages.
            color: hbar.pressed ? "#ffffff" : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            opacity: hbar.pressed || hbar.hovered ? 1.0 : 0.9
        }
        background: Rectangle { color: hbar.pressed || hbar.hovered ? "#30ffffff" : "transparent" }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open document or PDF")
        currentFolder: app.openFolder()
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf)"), qsTr("Xournal++ files (*.xopp *.xoj)"), qsTr("PDF files (*.pdf)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: app.openUrls(selectedFiles)
    }
    FileDialog {
        id: saveDialog
        property var afterSave: null
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xopp"
        nameFilters: [qsTr("Xournal++ files (*.xopp)")]
        onAccepted: {
            if (app.saveAs(selectedFile) && afterSave) afterSave()
            afterSave = null
        }
        onRejected: afterSave = null
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Unsaved changes")
        width: Math.min(win.width * 0.9, 480)
        Label {
            width: unsavedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("\"%1\" has unsaved changes.").arg(app.title)
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onAccepted: { unsavedDialog.close(); saveOrAsk(afterDiscardCheck) }
            onRejected: { unsavedDialog.close(); afterDiscardCheck = null }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    unsavedDialog.close()
                    var action = afterDiscardCheck
                    afterDiscardCheck = null
                    if (action) action()
                }
            }
        }
    }

    // After a crash: offer the documents with unsaved changes (from emergency and autosave files).
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(win.width * 0.9, 560)
        title: qsTr("Recover unsaved changes?")
        ColumnLayout {
            width: recoveryDialog.availableWidth
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Xournal Qt did not close properly. These documents have changes that were not saved:")
            }
            Repeater {
                model: app.recoveryItems
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.title; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Label { text: modelData.time; color: "#6b6f75" }
                }
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Recover"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard changes"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            onAccepted: { recoveryDialog.close(); app.recover(true) }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    recoveryDialog.close()
                    app.recover(false)
                }
            }
        }
    }
    Component.onCompleted: if (app.recoveryItems.length > 0) recoveryDialog.open()

    Dialog {
        id: messageDialog
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width * 0.8, 640)
        standardButtons: Dialog.Ok
        property alias text: messageLabel.text
        Label { id: messageLabel; wrapMode: Text.Wrap; width: parent.width }
    }
    Connections {
        target: app
        function onMessage(title, text, error) {
            messageDialog.title = title !== "" ? title : (error ? qsTr("Error") : qsTr("Information"))
            messageDialog.text = text
            messageDialog.open()
        }
    }

    SettingsPage { id: settingsPage; objectName: "settingsPage" }
    TabOverview {
        id: tabOverview
        objectName: "tabOverview"
        onCloseRequested: function(index) { requestCloseTab(index) }
    }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; onActivated: app.redo() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: saveOrAsk(null) }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: openSaveDialog(null) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.New, StandardKey.AddTab]; onActivated: app.newDocument() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: requestCloseTab(app.currentTab) }
    // Tabs: Ctrl+Tab / Ctrl+Shift+Tab (Shift+Tab arrives as Backtab), like browsers; also Ctrl+PgDown / Ctrl+PgUp.
    Shortcut {
        sequences: ["Ctrl+Tab", "Ctrl+PgDown"]
        context: Qt.ApplicationShortcut
        onActivated: app.nextTab()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Ctrl+Backtab", "Ctrl+Shift+Backtab", "Ctrl+PgUp"]
        context: Qt.ApplicationShortcut
        onActivated: app.previousTab()
    }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: tabOverview.visible ? tabOverview.close() : tabOverview.open() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsPage.open() }
    Shortcut { sequence: "Ctrl+Alt+G"; onActivated: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: searchBar.openBar() }
    Shortcut { sequences: [StandardKey.FindNext]; onActivated: app.searchNext() }
    Shortcut { sequences: [StandardKey.FindPrevious]; onActivated: app.searchPrevious() }
    Shortcut { sequences: [StandardKey.ZoomIn]; onActivated: app.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: app.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: app.fitWidth() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: win.close() }
}
