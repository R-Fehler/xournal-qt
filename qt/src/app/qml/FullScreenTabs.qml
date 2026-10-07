// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Full screen (editing): the open documents as dots in a slim bar at the top; a tap shows them all, a swipe along
// the bar or its small arrows at both ends go to the next or previous one (only on the bar: the pages keep every
// touch)
Rectangle {
    id: fullScreenTabs
    objectName: "fullScreenTabs"
    visible: win.chromeMode === "compact" && !win.zenShown && !app.presenting && !app.homeVisible
             && app.tabs.count > 1 && !searchBar.visible
    z: 59
    // at the top, in the middle of the window (over the notes and a reference beside them alike), below the status bar
    anchors.horizontalCenter: parent.horizontalCenter
    y: win.controlsTop
    // (thin to look at; for fingers (the touch profile) taller, with arrows as wide as a finger)
    height: win.adaptive.touchProfile ? 36 : 26
    width: Math.max(120, (tabDots.visible ? tabDots.implicitWidth : tabCountLabel.implicitWidth) + 36) + 2 * arrowWidth
    readonly property int arrowWidth: win.adaptive.touchProfile ? win.adaptive.minTarget : 26
    radius: height / 2
    color: "#b3303134"
    readonly property bool manyTabs: app.tabs.count > 12
    PageIndicator {
        id: tabDots
        objectName: "fullScreenTabDots"
        anchors.centerIn: parent
        visible: !fullScreenTabs.manyTabs
        count: app.tabs.count
        currentIndex: app.currentTab
        interactive: false
        padding: 0
        spacing: 7
        delegate: Item {
            required property int index
            implicitWidth: 9
            implicitHeight: 9
            // (read again when a document is changed or another one comes to the front)
            readonly property bool unsaved: (app.modified, app.currentTab, app.tabModified(index))
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: index === tabDots.currentIndex ? "#ffffff" : "transparent"
                border.width: 1.5
                border.color: "#e8eaed"
            }
            Rectangle {  // not saved: a small orange mark
                visible: parent.unsaved
                width: 5; height: 5; radius: 2.5
                x: parent.width - 3; y: -2
                color: "#ffb74d"
            }
        }
    }
    Label {
        id: tabCountLabel
        objectName: "fullScreenTabCount"
        anchors.centerIn: parent
        visible: fullScreenTabs.manyTabs
        text: (app.currentTab + 1) + " / " + app.tabs.count
        color: "#ffffff"
        font.pixelSize: 13
    }
    // The previous / next document: small arrows at the ends of the bar
    component TabArrow: Item {
        id: arrow
        property bool forward
        readonly property bool atEnd: forward ? app.currentTab >= app.tabs.count - 1 : app.currentTab <= 0
        width: fullScreenTabs.arrowWidth
        height: fullScreenTabs.height
        Label {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -1
            text: arrow.forward ? "›" : "‹"
            color: "#ffffff"
            opacity: arrow.atEnd ? 0.35 : (arrowHover.hovered ? 1 : 0.8)
            font.pixelSize: 20
            font.bold: true
        }
        HoverHandler { id: arrowHover }
        TapHandler {
            enabled: !arrow.atEnd
            onTapped: {
                if (arrow.forward) app.nextTab()
                else app.previousTab()
                tabToast.show()
            }
        }
    }
    TabArrow { objectName: "fullScreenTabPrevious"; forward: false; anchors.left: parent.left }
    TabArrow { objectName: "fullScreenTabNext"; forward: true; anchors.right: parent.right }
    // Between the arrows, a tap: the overview of all of them
    Item {
        anchors.fill: parent
        anchors.leftMargin: fullScreenTabs.arrowWidth
        anchors.rightMargin: fullScreenTabs.arrowWidth
        TapHandler { onTapped: tabOverview.open() }
    }
    DragHandler {
        id: tabSwipe
        target: null
        yAxis.enabled: false
        onActiveChanged: {
            if (active) return
            const dx = centroid.position.x - centroid.pressPosition.x
            if (Math.abs(dx) < 30) return
            if (dx < 0) app.nextTab()
            else app.previousTab()
            tabToast.show()
        }
    }
    ToolTip.visible: tabHover.hovered
    ToolTip.text: qsTr("Open documents: tap for all of them; the arrows or a swipe for the next or previous one")
    ToolTip.delay: 800
    HoverHandler { id: tabHover }
}
