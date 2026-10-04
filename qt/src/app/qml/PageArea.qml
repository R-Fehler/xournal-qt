// Input on a page preview (sidebar, page grid): tap / click (with Ctrl and Shift for the selection), right click
// for the page menu, press and hold then move to drag the selected pages (see PageDragOverlay).
// A finger (or a pen) held still on a page selects it (heldToSelect: the view enters its selection mode, as on
// Android home screens and in Google Photos); moving it after that drags the selected pages. With a mouse, holding
// alone changes nothing: letting go without moving is a plain click (Ctrl/Shift+click select, right click: the menu).
import QtQuick

MouseArea {
    id: area
    objectName: "pageArea"
    required property var dragOverlay
    required property int pageIndex
    property Item delegateItem: parent
    signal tapped(int modifiers)
    signal menuRequested(real x, real y)
    signal held()
    /// A finger or a pen was held still on the page: select it, in the selection mode
    signal heldToSelect()

    anchors.fill: parent
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    pressAndHoldInterval: 400  // (the tests shorten it)
    /// Held long enough: moving the finger now drags the pages.
    property bool armed: false
    property bool dragging: false
    property point pressPos
    /// The press is a finger's or a pen's (a mouse event Qt or the system made from touch or tablet input)
    property bool touch: false
    /// How far the finger must move after holding before the pages follow it.
    readonly property real dragThreshold: 8

    onPressed: function(mouse) {
        pressPos = Qt.point(mouse.x, mouse.y)
        touch = mouse.source !== Qt.MouseEventNotSynthesized
        armed = dragging = false
    }
    onClicked: function(mouse) {
        if (mouse.button === Qt.RightButton) menuRequested(mouse.x, mouse.y)
        else tapped(mouse.modifiers)
    }
    // A mouse held only gets ready to drag: neither the selection nor the order of the pages may change before it
    // really moves. A finger held still selects the page right away (and then drags the selection when it moves).
    onPressAndHold: function(mouse) {
        if (mouse.button !== Qt.LeftButton) return
        armed = true
        preventStealing = true  // the view must not take the press away now
        held()
        if (touch) heldToSelect()
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
        } else if (armed && mouse.button === Qt.LeftButton && !touch) {
            tapped(mouse.modifiers)  // held still and let go: that is a click (MouseArea drops the click itself)
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
