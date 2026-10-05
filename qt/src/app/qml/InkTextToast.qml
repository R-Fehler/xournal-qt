// The handwriting just copied as text (qt/copy-tools; qt/docs/handwriting-search.md, "Copy handwriting as text"): a
// small card near the words swept over or selected, with the text now on the clipboard. The words the recogniser was
// unsure of are grey. The text can be selected (and copied again in part), not edited: the clipboard has it as it is
// shown. It hides by itself after a while (longer for a longer text), not while the pointer is on it or text in it is
// selected; × closes it. While the words are still being read (lines not read before): "Reading the handwriting…".
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: toast
    /// The canvas the place is on (`reference`: the reference's), and the room it may take (the window's coordinates)
    property Item canvasItem
    property Item referenceItem
    property real roomTop: 0
    property real roomBottom: parent ? parent.height : 0
    property string state_: ""   // "reading", "copied"
    property string text: ""
    property string html: ""
    property int unsure: 0
    property bool partial: false
    /// Where the words are (the window's coordinates)
    property rect place: Qt.rect(0, 0, 0, 0)
    visible: false
    z: 90
    padding: 0
    width: Math.min(420, Math.max(220, (parent ? parent.width : 420) - 32))
    Material.foreground: "#303030"
    background: Rectangle {
        radius: 12
        color: "#fbfbfc"
        border.width: 1
        border.color: "#30000000"
    }

    /// AppController::inkTextCopy
    function show(result) {
        const item = result.reference && referenceItem ? referenceItem : canvasItem
        if (!item) return
        const p = item.mapToItem(parent, result.x || 0, result.y || 0)
        place = Qt.rect(p.x, p.y, result.width || 0, result.height || 0)
        if (result.state === "reading") {
            // (shown only when the reading takes a moment: words read before come at once)
            state_ = "reading"
            text = ""
            html = ""
            readingDelay.restart()
            hideTimer.stop()
            return
        }
        readingDelay.stop()
        state_ = "copied"
        text = result.text || ""
        html = result.html || ""
        unsure = result.unsure || 0
        partial = result.partial === true
        open_()
        hideTimer.interval = Math.min(15000, 5000 + 40 * text.length)
        hideTimer.restart()
    }
    function open_() {
        visible = true
        Qt.callLater(placeIt)
    }
    function close() {
        visible = false
        readingDelay.stop()
        hideTimer.stop()
        state_ = ""
    }
    /// Above the words where there is room, else below them; inside the window
    function placeIt() {
        if (!parent) return
        x = Math.max(8, Math.min(place.x + (place.width - width) / 2, parent.width - width - 8))
        const above = place.y - height - 12
        y = above >= roomTop + 8 ? above : Math.min(place.y + place.height + 12, roomBottom - height - 8)
        y = Math.max(roomTop + 8, y)
    }
    onHeightChanged: if (visible) placeIt()
    Timer {
        id: readingDelay
        interval: 300
        onTriggered: toast.open_()
    }
    Timer {
        id: hideTimer
        // (not while it is pointed at, or text in it is selected)
        onTriggered: if (hover.hovered || textView.selectedText !== "") restart(); else toast.close()
    }
    HoverHandler { id: hover }

    ColumnLayout {
        width: toast.width
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.topMargin: 4
            spacing: 4
            Image {
                source: app.iconUrl("xqt-copy-ink-text")
                sourceSize: Qt.size(18, 18)
            }
            Label {
                objectName: "inkTextToastTitle"
                Layout.fillWidth: true
                text: toast.state_ === "reading" ? qsTr("Reading the handwriting…") : qsTr("Copied as text")
                font.weight: Font.DemiBold
                color: "#5f6368"
                elide: Text.ElideRight
            }
            BusyIndicator {
                visible: toast.state_ === "reading"
                running: visible
                implicitWidth: 28
                implicitHeight: 28
            }
            ToolButton {
                objectName: "inkTextToastClose"
                text: "×"
                font.pixelSize: 18
                implicitWidth: 36
                implicitHeight: 36
                onClicked: toast.close()
            }
        }
        // The text (scrolls when it is long)
        ScrollView {
            id: scroller
            visible: toast.state_ === "copied"
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 6
            Layout.preferredHeight: Math.min(textView.implicitHeight + 4, 200)
            contentWidth: availableWidth
            clip: true
            TextEdit {
                id: textView
                objectName: "inkTextToastText"
                width: scroller.availableWidth - 8
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.RichText
                text: toast.html
                font.pixelSize: 15
                color: "#202124"
                selectionColor: Material.accentColor
                selectedTextColor: "#ffffff"
            }
        }
        Label {
            objectName: "inkTextToastNote"
            visible: toast.state_ === "copied" && text !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 4
            text: [toast.unsure > 0 ? qsTr("Grey: the recogniser was unsure of these words") : "",
                   toast.partial ? qsTr("Some lines were left out: no handwriting model to read them") : ""]
                  .filter(function(t) { return t !== "" }).join(" · ")
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#80868b"
        }
        Item { implicitHeight: 10 }
    }
}
