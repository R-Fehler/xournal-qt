// The gestures on a card's title (qt/rename): a tap or a click opens, as on the rest of the card (a click of the mouse
// a moment later: the time a double click may take); a double click with the mouse, or a press and hold (finger or
// mouse), edits the name in place; a right click opens the card's menu.
import QtQuick

MouseArea {
    id: area
    property bool renamable: true
    signal tapped(int modifiers)
    signal menuRequested(real x, real y)
    signal renameRequested()
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    pressAndHoldInterval: 450
    property bool held: false
    property bool byMouse: false
    property int pendingModifiers: 0
    // Whether the press is the mouse's (a click waits for a second one) or a finger's (a tap opens at once)
    PointHandler {
        id: mousePointer
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.LeftButton | Qt.RightButton
    }
    Timer {
        id: clickLater
        interval: Qt.styleHints.mouseDoubleClickInterval
        onTriggered: area.tapped(area.pendingModifiers)
    }
    onPressed: function(mouse) {
        held = false
        byMouse = mousePointer.active
    }
    onClicked: function(mouse) {
        if (held) return
        if (mouse.button === Qt.RightButton) {
            menuRequested(mouse.x, mouse.y)
        } else if (byMouse && renamable && mouse.modifiers === Qt.NoModifier) {
            pendingModifiers = mouse.modifiers
            clickLater.restart()
        } else {
            tapped(mouse.modifiers)
        }
    }
    onDoubleClicked: function(mouse) {
        if (mouse.button !== Qt.LeftButton || !renamable) return
        clickLater.stop()
        renameRequested()
    }
    onPressAndHold: function(mouse) {
        if (mouse.button !== Qt.LeftButton || !renamable) return
        held = true
        clickLater.stop()
        renameRequested()
    }
}
