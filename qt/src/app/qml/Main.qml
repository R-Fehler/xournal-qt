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

    onClosing: function(close) {
        if (app.modified && !quitting) {
            close.accepted = false
            withSavedChanges(function() { quitting = true; win.close() })
        }
    }

    header: ToolBar {
        Material.background: "#fafafa"
        Material.foreground: "#303030"
        height: 56
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 2

            IconButton { iconName: "xopp-document-new"; tip: qsTr("New"); onClicked: withSavedChanges(app.newDocument) }
            IconButton { iconName: "xopp-document-open"; tip: qsTr("Open"); onClicked: withSavedChanges(function() { openDialog.open() }) }
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
            Label { text: app.pageNumber + " / " + app.pageCount; color: "#505050"; Layout.rightMargin: 8 }
            ToolButton { text: "−"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomOut() }
            ToolButton { text: app.zoomPercent + " %"; onClicked: app.fitWidth(); ToolTip.visible: hovered; ToolTip.text: qsTr("Fit page width") }
            ToolButton { text: "+"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomIn() }
        }
    }

    DocumentCanvas {
        id: canvas
        anchors.fill: parent
        view: app.view
    }

    // Scroll bars over the canvas: wide enough to be dragged with a finger or the pen.
    ScrollBar {
        id: vbar
        orientation: Qt.Vertical
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: hbar.visible ? hbar.height : 0
        visible: canvas.contentHeight > canvas.height + 1
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
        visible: canvas.contentWidth > canvas.width + 1
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
        onAccepted: app.openFile(selectedFile)
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
        Label { text: qsTr("\"%1\" has unsaved changes.").arg(app.title) }
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

    Shortcut { sequences: [StandardKey.Undo]; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; onActivated: app.redo() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: saveOrAsk(null) }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: openSaveDialog(null) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: withSavedChanges(function() { openDialog.open() }) }
    Shortcut { sequences: [StandardKey.New]; onActivated: withSavedChanges(app.newDocument) }
    Shortcut { sequences: [StandardKey.ZoomIn]; onActivated: app.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: app.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: app.fitWidth() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: win.close() }
}
