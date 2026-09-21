// Input on a page preview (sidebar, page grid): tap / click (with Ctrl and Shift for the selection), right click
// for the page menu, press and hold then move to drag the selected pages (see PageDragOverlay). Holding alone
// changes nothing: letting go without moving is a plain tap.
import QtQuick

MouseArea {
    id: area
    required property var dragOverlay
    required property int pageIndex
    property Item delegateItem: parent
    signal tapped(int modifiers)
    signal menuRequested(real x, real y)
    signal held()

    anchors.fill: parent
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    pressAndHoldInterval: 400
    /// Held long enough: moving the finger now drags the pages.
    property bool armed: false
    property bool dragging: false
    property point pressPos
    /// How far the finger must move after holding before the pages follow it.
    readonly property real dragThreshold: 8

    onPressed: function(mouse) {
        pressPos = Qt.point(mouse.x, mouse.y)
        armed = dragging = false
    }
    onClicked: function(mouse) {
        if (mouse.button === Qt.RightButton) menuRequested(mouse.x, mouse.y)
        else tapped(mouse.modifiers)
    }
    // Holding only gets ready to drag: a finger easily rests longer than this without meaning to move anything, so
    // neither the selection nor the order of the pages may change before it really moves.
    onPressAndHold: function(mouse) {
        if (mouse.button !== Qt.LeftButton) return
        armed = true
        preventStealing = true  // the view must not take the press away now
        held()
    }
    onPositionChanged: function(mouse) {
        if (dragging) {
            dragOverlay.moveTo(mapToItem(dragOverlay, mouse.x, mouse.y))
        } else if (armed && Math.hypot(mouse.x - pressPos.x, mouse.y - pressPos.y) > dragThreshold) {
            if (!app.pages.isSelected(pageIndex)) app.pages.select(pageIndex, 0)
            dragging = true
            dragOverlay.start(mapToItem(dragOverlay, mouse.x, mouse.y), delegateItem)
            dragOverlay.moveTo(mapToItem(dragOverlay, mouse.x, mouse.y))
        }
    }
    onReleased: function(mouse) {
        if (dragging) {
            dragOverlay.finish()
        } else if (armed && mouse.button === Qt.LeftButton) {
            tapped(mouse.modifiers)  // held still and let go: that is a tap (MouseArea drops the click itself)
        }
        dragging = armed = false
        preventStealing = false
    }
    onCanceled: {
        if (dragging) dragOverlay.stop()
        dragging = armed = false
        preventStealing = false
    }
}
