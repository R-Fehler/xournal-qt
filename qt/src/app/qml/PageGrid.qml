// Grid of all pages of the current document over the canvas: fling through the whole document, tap a page to go
// there. Zoom (pinch, Ctrl+wheel, −/+) changes the number of columns: bigger previews, fewer per row.
// Search hits are marked on the previews.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: pageGrid
    color: "#4e5156"
    visible: false
    focus: visible

    /// Preview width the user zoomed to; the columns follow from it.
    property real cellTarget: 200
    readonly property int columns: Math.max(1, Math.min(12, Math.round(grid.width / cellTarget)))
    readonly property int spacing: 6
    readonly property int labelHeight: 18

    function open() {
        visible = true
        grid.currentIndex = app.pages.currentPage
        grid.positionViewAtIndex(app.pages.currentPage, GridView.Center)
        grid.forceActiveFocus()
    }
    function close() {
        visible = false
    }
    function choose(index) {
        app.goToPage(index)
        close()
    }
    function setColumns(n) {
        n = Math.max(1, Math.min(12, n))
        const keep = grid.indexAt(grid.contentX + grid.width / 2, grid.contentY + grid.height / 2)
        cellTarget = grid.width / n
        if (keep >= 0) grid.positionViewAtIndex(keep, GridView.Center)
    }

    // Stepping through search hits (Enter in the search bar) moves the grid along.
    Connections {
        target: app
        enabled: pageGrid.visible
        function onSearchChanged() {
            if (app.searchCurrent > 0) grid.positionViewAtIndex(app.pageNumber - 1, GridView.Contain)
        }
    }

    Keys.onEscapePressed: close()

    GridView {
        id: grid
        objectName: "pageGridView"
        anchors.fill: parent
        anchors.leftMargin: pageGrid.spacing / 2
        anchors.rightMargin: pageGrid.spacing / 2
        clip: true
        model: app.pages
        keyNavigationEnabled: true
        cacheBuffer: height
        maximumFlickVelocity: 9000
        cellWidth: Math.floor(width / pageGrid.columns)
        // Room for an A4 portrait page and its number; other formats are fitted in.
        cellHeight: Math.round((cellWidth - pageGrid.spacing) * 1.414) + pageGrid.labelHeight + pageGrid.spacing
        header: Item { height: pageGrid.spacing }
        footer: Item { height: 80 }  // not under the zoom controls
        ScrollBar.vertical: ScrollBar { minimumSize: 0.05 }

        Keys.onReturnPressed: pageGrid.choose(currentIndex)
        Keys.onEnterPressed: pageGrid.choose(currentIndex)
        Keys.onSpacePressed: pageGrid.choose(currentIndex)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                pageGrid.setColumns(pageGrid.columns - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Minus) {
                pageGrid.setColumns(pageGrid.columns + 1)
                event.accepted = true
            }
        }

        delegate: Item {
            id: cell
            required property int index
            required property int pageNumber
            required property real aspect
            required property string thumbnail
            required property bool current
            required property var searchHits
            required property int currentSearchHit
            width: grid.cellWidth
            height: grid.cellHeight

            readonly property real availW: width - pageGrid.spacing
            readonly property real availH: height - pageGrid.labelHeight - pageGrid.spacing
            // Fit the page (any format) into the cell.
            readonly property real frameW: Math.min(availW, availH / Math.max(0.1, aspect))
            readonly property real dpr: Screen.devicePixelRatio

            Rectangle {
                id: frame
                x: (cell.width - width) / 2
                y: pageGrid.spacing / 2 + (cell.availH - height)
                width: Math.round(cell.frameW)
                height: Math.round(cell.frameW * cell.aspect)
                color: "white"
                border.width: cell.current ? 3 : 0
                border.color: Material.accentColor

                // A small preview right away, a sharp one for big cells (loads on top of it).
                Image {
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    source: cell.thumbnail
                    sourceSize.width: 160
                    smooth: true
                }
                Image {
                    anchors.fill: parent
                    visible: cell.frameW * cell.dpr > 180 && status === Image.Ready
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    // In steps, so zooming does not render every size.
                    source: cell.frameW * cell.dpr > 180 ? cell.thumbnail : ""
                    sourceSize.width: Math.ceil(cell.frameW * cell.dpr / 128) * 128
                    smooth: true
                }
                // Search hits
                Repeater {
                    model: cell.searchHits
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        x: modelData.x * frame.width - 2
                        y: modelData.y * frame.height - 2
                        width: Math.max(4, modelData.width * frame.width + 4)
                        height: Math.max(4, modelData.height * frame.height + 4)
                        radius: 2
                        color: index === cell.currentSearchHit ? "#aaff7800" : "#80ffd200"
                        border.width: 1
                        border.color: index === cell.currentSearchHit ? "#ff7800" : "#e0a800"
                    }
                }
                Rectangle {
                    visible: cell.searchHits.length > 0
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    width: hitCount.implicitWidth + 12
                    height: 20
                    radius: 10
                    color: "#ffd200"
                    Label {
                        id: hitCount
                        anchors.centerIn: parent
                        text: cell.searchHits.length
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: "#5a3d00"
                    }
                }
                // Keyboard position
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    visible: cell.GridView.isCurrentItem && grid.activeFocus && !cell.current
                    color: "transparent"
                    border.width: 2
                    border.color: "#c5cae9"
                }
            }
            Label {
                anchors.top: frame.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                height: pageGrid.labelHeight
                verticalAlignment: Text.AlignVCenter
                text: cell.pageNumber
                font.pixelSize: 12
                color: cell.current ? "#ffffff" : "#d0d3d8"
                font.weight: cell.current ? Font.DemiBold : Font.Normal
            }
            TapHandler {
                onTapped: pageGrid.choose(cell.index)
            }
        }

        // Zoom: fewer, bigger previews when spreading the fingers.
        PinchHandler {
            id: pinch
            target: null
            grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType
            property real startTarget: 200
            onActiveChanged: if (active) startTarget = pageGrid.cellTarget
            onActiveScaleChanged: {
                const cols = Math.max(1, Math.min(12, Math.round(grid.width / (startTarget * activeScale))))
                if (cols !== pageGrid.columns) pageGrid.setColumns(cols)
            }
        }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                pageGrid.setColumns(pageGrid.columns + (event.angleDelta.y > 0 ? -1 : 1))
            }
        }
    }

    // Zoom and close
    Pane {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 24
        padding: 2
        leftPadding: 4
        rightPadding: 4
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f2fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            ToolButton {
                text: "−"; font.pixelSize: 22; implicitWidth: 44
                enabled: pageGrid.columns < 12
                onClicked: pageGrid.setColumns(pageGrid.columns + 1)
                ToolTip.visible: hovered; ToolTip.text: qsTr("Smaller previews")
            }
            Label {
                objectName: "pageGridColumns"
                text: pageGrid.columns === 1 ? qsTr("1 column") : qsTr("%1 columns").arg(pageGrid.columns)
                color: "#505050"
                Layout.minimumWidth: 80
                horizontalAlignment: Text.AlignHCenter
            }
            ToolButton {
                text: "+"; font.pixelSize: 22; implicitWidth: 44
                enabled: pageGrid.columns > 1
                onClicked: pageGrid.setColumns(pageGrid.columns - 1)
                ToolTip.visible: hovered; ToolTip.text: qsTr("Bigger previews")
            }
            ToolSeparator {}
            IconButton {
                iconName: "xqt-close"; tip: qsTr("Back to the page (Esc)")
                implicitWidth: 44; implicitHeight: 44
                onClicked: pageGrid.close()
            }
        }
    }
}
