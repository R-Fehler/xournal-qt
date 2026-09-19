// Input on a page preview (sidebar, page grid): tap / click (with Ctrl and Shift for the selection), right click
// for the page menu, press and hold then move to drag the selected pages (see PageDragOverlay).
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
    property bool dragging: false
    property point pressPos

    onPressed: function(mouse) { pressPos = Qt.point(mouse.x, mouse.y) }
    onClicked: function(mouse) {
        if (mouse.button === Qt.RightButton) menuRequested(mouse.x, mouse.y)
        else tapped(mouse.modifiers)
    }
    onPressAndHold: function(mouse) {
        if (mouse.button !== Qt.LeftButton) return
        if (!app.pages.isSelected(pageIndex)) app.pages.select(pageIndex, 0)
        held()
        dragging = true
        preventStealing = true  // the view must not scroll now
        dragOverlay.start(mapToItem(dragOverlay, mouse.x, mouse.y), delegateItem)
    }
    onPositionChanged: function(mouse) {
        if (dragging) dragOverlay.moveTo(mapToItem(dragOverlay, mouse.x, mouse.y))
    }
    onReleased: {
        if (dragging) {
            dragOverlay.finish()
            dragging = false
            preventStealing = false
        }
    }
    onCanceled: {
        if (dragging) {
            dragOverlay.stop()
            dragging = false
            preventStealing = false
        }
    }
}
