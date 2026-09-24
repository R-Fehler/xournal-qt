// Going to a page by its number: digits typed while the page is at hand (not while text is typed) show
// "Go to page: 12"; Enter goes there (the last page at most), Escape or anything else cancels.
// It takes the keyboard while it is open, and gives it back to `returnFocus` (the canvas) when it closes.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: jump
    objectName: "pageJump"
    /// The digits typed so far
    property string digits: ""
    property int pageCount: 1
    property Item returnFocus: null
    /// Enter: the page to go to (1-based, within the document)
    signal jumpRequested(int page)

    function start(digit) {
        digits = digit
        visible = true
        forceActiveFocus()
        forgotten.restart()
    }
    function cancel() {
        const hadFocus = activeFocus
        visible = false
        digits = ""
        forgotten.stop()
        if (hadFocus && returnFocus) returnFocus.forceActiveFocus()
    }
    function accept() {
        const n = parseInt(digits, 10)
        cancel()
        if (!isNaN(n)) jumpRequested(Math.max(1, Math.min(pageCount, n)))
    }
    function isDigit(event) {
        return event.key >= Qt.Key_0 && event.key <= Qt.Key_9
               && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
    }

    visible: false
    radius: 12
    color: "#f2303134"
    implicitWidth: row.implicitWidth + 36
    implicitHeight: 52

    // While it is open, every key comes to it before the window's shortcuts (Escape leaves full screen, Backspace
    // deletes, P takes the pen, ...): one that is not part of a page number just closes it.
    Keys.onShortcutOverride: function(event) { event.accepted = true }
    Keys.onPressed: function(event) {
        forgotten.restart()
        if (event.key === Qt.Key_Shift || event.key === Qt.Key_Control || event.key === Qt.Key_Alt
                || event.key === Qt.Key_Meta || event.key === Qt.Key_AltGr) {
            return  // (a modifier alone: the key it goes with decides)
        }
        if (isDigit(event)) {
            if (digits.length < 6) digits += String(event.key - Qt.Key_0)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            accept()
        } else if (event.key === Qt.Key_Backspace) {
            digits = digits.slice(0, -1)
            if (digits === "") cancel()
        } else if (event.key === Qt.Key_Escape) {
            cancel()
        } else {
            cancel()  // not a page number after all
        }
        event.accepted = true
    }
    // The focus went elsewhere (a tap on the page, a dialog): no jump
    onActiveFocusChanged: if (!activeFocus && visible) cancel()

    Timer {  // typed and forgotten: it goes away by itself
        id: forgotten
        interval: 5000
        onTriggered: jump.cancel()
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Label {
            text: qsTr("Go to page:")
            color: "#e8eaed"
            font.pixelSize: 17
        }
        Label {
            objectName: "pageJumpDigits"
            text: jump.digits
            color: "#ffffff"
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Label {
            text: qsTr("of %1").arg(jump.pageCount)
            color: "#9aa0a6"
            font.pixelSize: 15
        }
    }
}
