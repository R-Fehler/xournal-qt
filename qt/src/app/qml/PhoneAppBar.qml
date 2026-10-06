// The phone's app bar (qt/docs/adaptive-layout.md, "The phone chrome"): in the phone classes a slim bar at the top
// instead of the tab strip and the tool bar, as a phone's browser has it:
//   the library (home) | the document's title, with dots for the open documents under it | the tab count | ⋮
// - a swipe along the bar (only the bar: the page keeps every touch) goes to the next or the previous document;
// - the tab count: a tap shows all open documents (after the double-tap time, so a double tap does not flash them), a
//   double tap goes back to the document used before (Alt+Tab), a long press lists the documents used lately;
// - ⋮ is the top bar's own (Main.qml puts it into `moreSlot`); on the home screen there is none.
// It hosts the top bar (qt/top-bar: the same items as on a larger screen, scrolling sideways, "+" at its end; Main.qml
// puts it into `toolsSlot`): a second row under the title where the bar is narrow (a phone upright), else in the row
// between the title and the tab count (a phone held sideways). New documents come from the overview and the library.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    objectName: "phoneAppBar"
    /// Room at the top for the system's status bar
    property real topInset: 0
    /// Room at the sides for a camera cut-out or the navigation bar (a phone held sideways)
    property real leftInset: 0
    property real rightInset: 0
    /// Where ⋮ goes (Main.qml puts the tool bar's end there)
    readonly property Item moreSlot: moreHolder
    /// Where the top bar goes (Main.qml puts it there), and whether it is shown, and how tall it is
    readonly property Item toolsSlot: toolsHolder
    property bool toolsShown: false
    property real toolsHeight: 52
    /// The top bar in a row of its own under the title (a narrow bar: a phone upright)
    readonly property bool twoRows: width < 600
    /// The title's row: as tall as the top bar where that sits in it
    readonly property real rowHeight: toolsShown && !twoRows ? Math.max(48, toolsHeight) : 48
    signal overviewRequested()
    signal recentRequested()
    /// The page number in the bar (a phone held sideways), and its tap: all pages
    property bool pageShown: false
    signal pagesRequested()
    readonly property int target: win.adaptive.touchProfile ? win.adaptive.minTarget : 44
    implicitHeight: rowHeight + topInset + (toolsShown && twoRows ? toolsHeight : 0)
    color: "#f1f3f4"

    Hairline {  // the line towards the page
        anchors.bottom: parent.bottom
        width: parent.width
        color: "#d5d8dc"
    }

    RowLayout {
        id: titleRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: bar.rowHeight
        anchors.topMargin: bar.topInset
        anchors.leftMargin: 2 + bar.leftInset
        anchors.rightMargin: 2 + bar.rightInset
        spacing: 0

        IconButton {
            objectName: "phoneHomeButton"
            iconName: "xqt-library"
            label: app.library.available ? app.library.name : qsTr("Library")
            tip: app.library.available ? qsTr("Library “%1” and recent documents").arg(app.library.name)
                                       : qsTr("Recent documents")
            implicitWidth: bar.target
            implicitHeight: bar.target
            icon.width: 24
            icon.height: 24
            checked: app.homeVisible
            visible: !app.secondaryWindow
            onClicked: app.homeVisible = true
        }

        // The title of the document (on the home screen: the one behind it; a tap goes back to it), and the dots
        Item {
            id: titleArea
            objectName: "phoneTitleArea"
            Layout.fillWidth: bar.twoRows || !bar.toolsShown
            Layout.preferredWidth: bar.twoRows || !bar.toolsShown ? -1 : Math.max(120, Math.round(bar.width * 0.24))
            Layout.fillHeight: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            // A swipe along the title: the next document (to the left) or the previous one (to the right) (only there:
            // the top bar beside it scrolls)
            DragHandler {
                id: swipe
                objectName: "phoneTabSwipe"
                target: null
                yAxis.enabled: false
                property real dx: 0
                onCentroidChanged: if (active) dx = centroid.scenePosition.x - centroid.scenePressPosition.x
                onActiveChanged: {
                    if (active) {
                        dx = 0
                        return
                    }
                    if (Math.abs(dx) < 40 || app.tabs.count < 2) return
                    if (dx < 0) app.nextTab()
                    else app.previousTab()
                }
            }
            Label {
                id: titleLabel
                objectName: "phoneTitle"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: dots.visible || countLine.visible ? -5 : 0
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: app.tabs.count === 0 ? qsTr("No open documents") : (app.modified ? "● " : "") + app.title
                color: app.homeVisible || app.tabs.count === 0 ? "#80868b" : "#202124"
                font.pixelSize: 15
                font.weight: app.homeVisible ? Font.Normal : Font.DemiBold
            }
            PageIndicator {
                id: dots
                objectName: "phoneTabDots"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: titleLabel.bottom
                anchors.topMargin: 3
                visible: app.tabs.count > 1 && app.tabs.count <= 12
                count: app.tabs.count
                currentIndex: app.homeVisible ? -1 : app.currentTab
                interactive: false
                padding: 0
                spacing: 5
                delegate: Rectangle {
                    required property int index
                    implicitWidth: 6
                    implicitHeight: 6
                    radius: 3
                    color: index === dots.currentIndex ? "#5f6368" : "transparent"
                    border.width: 1
                    border.color: "#80868b"
                }
            }
            Label {  // many documents: where this one is, as a number
                id: countLine
                objectName: "phoneTabPosition"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: titleLabel.bottom
                visible: app.tabs.count > 12 && !app.homeVisible
                text: (app.currentTab + 1) + " / " + app.tabs.count
                font.pixelSize: 11
                color: "#5f6368"
            }
            TapHandler {
                enabled: app.homeVisible && app.tabs.count > 0
                onTapped: app.homeVisible = false  // (the home screen: back to the document behind it)
            }
            ToolTip.visible: titleHover.hovered && titleLabel.truncated
            ToolTip.text: app.title
            ToolTip.delay: 600
            HoverHandler { id: titleHover }
        }

        // (the top bar beside the title: a phone held sideways)
        Item {
            id: inlineTools
            visible: bar.toolsShown && !bar.twoRows
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        // The page number (a phone held sideways: the dock at the side gives its room to the tools): all pages
        ToolButton {
            objectName: "phonePageButton"
            visible: bar.pageShown && !app.homeVisible
            implicitHeight: bar.target
            focusPolicy: Qt.NoFocus
            text: app.pageNumber + " / " + app.pageCount
            font.pixelSize: 13
            leftPadding: 8
            rightPadding: 8
            Material.foreground: "#3c4043"
            Accessible.name: qsTr("All pages")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("All pages, the contents and the zoom (Ctrl+Alt+G)")
            ToolTip.delay: 600
            onClicked: bar.pagesRequested()
        }
        // The open documents: their number in a square. A tap: all of them; a double tap: the one used before; a long
        // press: the ones used lately
        AbstractButton {
            id: tabCount
            objectName: "phoneTabCount"
            implicitWidth: bar.target
            implicitHeight: bar.target
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            Accessible.name: qsTr("All open documents")
            /// A tap waits for a second one before the overview opens
            readonly property alias pending: overviewTimer.running
            Timer {
                id: overviewTimer
                objectName: "phoneTabCountTimer"
                interval: Qt.styleHints.mouseDoubleClickInterval
                onTriggered: bar.overviewRequested()
            }
            TapHandler {
                objectName: "phoneTabCountTaps"
                acceptedButtons: Qt.LeftButton
                onTapped: function(point, button) {
                    if (tapCount >= 2) {
                        overviewTimer.stop()
                        app.previousUsedTab()
                    } else {
                        overviewTimer.restart()
                    }
                }
                onLongPressed: {
                    overviewTimer.stop()
                    bar.recentRequested()
                }
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("All open documents (tap); the one before (double tap); used lately (hold)")
            ToolTip.delay: 600
            background: Rectangle {
                radius: 10
                color: tabCount.pressed ? "#e0e3e7" : "transparent"
            }
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    radius: 5
                    color: "transparent"
                    border.width: 2
                    border.color: "#3c4043"
                    Label {
                        objectName: "phoneTabCountText"
                        anchors.centerIn: parent
                        text: app.tabs.count > 99 ? "99+" : app.tabs.count
                        font.pixelSize: app.tabs.count > 99 ? 8 : app.tabs.count > 9 ? 11 : 13
                        font.weight: Font.DemiBold
                        color: "#3c4043"
                    }
                }
            }
        }

        // ⋮ (the tool bar's, with its menu): not on the home screen
        Item {
            id: moreHolder
            objectName: "phoneMoreSlot"
            visible: !app.homeVisible
            implicitWidth: children.length > 0 ? children[0].width : 0
            implicitHeight: children.length > 0 ? children[0].height : bar.target
        }
    }

    // The top bar: under the title (upright), or in the row (sideways)
    Item {
        id: toolsHolder
        objectName: "phoneToolsSlot"
        visible: bar.toolsShown
        x: bar.twoRows ? bar.leftInset : titleRow.x + inlineTools.x
        y: bar.twoRows ? bar.topInset + bar.rowHeight : bar.topInset
        width: bar.twoRows ? bar.width - bar.leftInset - bar.rightInset : inlineTools.width
        height: bar.toolsHeight
    }
}
