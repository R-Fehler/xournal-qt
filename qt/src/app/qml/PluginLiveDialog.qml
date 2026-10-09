// xournal-qt: part of the main window (Main.qml): a plugin's live dialog (qt/docs/features/plugins.md, "Dialogs").
// Not modal: the page stays visible and can be scrolled, and the preview's frame on it can be moved and sized
// (PluginFrame.qml). Every edit asks the plugin for a new preview (app.plugins.liveEdited); Insert puts it on the page
// as one undo step. Beside the page at the right on a desktop or tablet; a bottom sheet on a phone, the page above it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: dlg
    objectName: "pluginLiveDialog"
    readonly property var plugins: app.plugins
    readonly property var spec: plugins.liveSpec
    readonly property var answer: plugins.liveState
    /// The canvas it is beside
    property Item canvasItem
    readonly property bool sheet: win.layout.phoneLayout
    parent: Overlay.overlay
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: 0
    visible: plugins.liveOpen
    onVisibleChanged: if (visible) fields.values = Object.assign({}, spec.values || {})

    // (the window's coordinates: the overlay's; the layout's are the content's, below the window's header)
    readonly property real roomTop: win.contentItem.y + win.layout.canvasControlsTop
    readonly property real roomBottom: Math.min(win.contentItem.y + win.layout.canvasControlsBottom,
                                                win.insets.keyboardOpen ? win.insets.keyboardTop : Infinity)
    width: sheet ? win.width : Math.min(380, win.width - 32)
    height: sheet ? Math.round(Math.min(win.height * 0.46, implicitHeight))
                  : Math.min(implicitHeight, roomBottom - roomTop - 24)
    x: sheet ? 0 : Math.max(8, win.layout.canvasControlsRight - width - 12)
    y: sheet ? win.height - height - (win.insets.keyboardOpen ? win.height - win.insets.keyboardTop : win.insets.bottom)
             : roomTop + 12

    background: Rectangle {
        color: "#fdfdfd"
        radius: dlg.sheet ? 0 : 12
        border.width: dlg.sheet ? 0 : 1
        border.color: "#d5d8dc"
        Rectangle {  // the sheet's line towards the page
            visible: dlg.sheet
            width: parent.width
            height: 1
            color: "#d5d8dc"
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        implicitHeight: header.implicitHeight + body.implicitHeight + footer.implicitHeight + 8
        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 4
            Layout.topMargin: 4
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    objectName: "pluginLiveTitle"
                    text: dlg.spec.title || ""
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: dlg.spec.plugin || ""
                    visible: text !== "" && text !== dlg.spec.title
                    color: "#80868b"
                    font.pixelSize: 11
                }
            }
            IconButton {
                objectName: "pluginLiveClose"
                iconName: "xqt-close"
                tip: qsTr("Cancel")
                onClicked: dlg.plugins.liveCancel()
            }
        }
        Flickable {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: fields.implicitHeight + 16
            contentHeight: fields.implicitHeight + 16
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: body.contentHeight > body.height + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            PluginFields {
                id: fields
                objectName: "pluginLiveFields"
                x: 16
                y: 8
                width: body.width - 32
                fields: dlg.answer.fields || dlg.spec.fields || []
                errors: dlg.answer.errors || ({})
                onEdited: function(id, value, action) {
                    if (action !== "") dlg.plugins.liveEdited(values, action)
                    else pending.restart()
                }
                // (the plugin's own changes of the values, e.g. a new function's color)
                Connections {
                    target: dlg.plugins
                    function onLiveStateChanged() {
                        if (dlg.answer.values) fields.values = Object.assign({}, fields.values, dlg.answer.values)
                    }
                }
            }
            // A burst of edits (a slider dragged, typing) is one preview every few milliseconds
            Timer {
                id: pending
                interval: 40
                onTriggered: dlg.plugins.liveEdited(fields.values, "")
            }
        }
        Label {
            objectName: "pluginLiveMessage"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: text !== ""
            text: dlg.answer.error ? dlg.answer.error : (dlg.answer.message || "")
            color: dlg.answer.error ? "#c5221f" : "#5f6368"
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
        RowLayout {
            id: footer
            Layout.fillWidth: true
            Layout.margins: 8
            Item { Layout.fillWidth: true }
            Button {
                objectName: "pluginCancelButton"
                text: qsTr("Cancel")
                flat: true
                onClicked: dlg.plugins.liveCancel()
            }
            Button {
                objectName: "pluginInsertButton"
                text: dlg.spec.insertLabel || qsTr("Insert")
                highlighted: true
                flat: true
                onClicked: {
                    pending.stop()
                    dlg.plugins.liveInsert(fields.values)
                }
            }
        }
    }
}
