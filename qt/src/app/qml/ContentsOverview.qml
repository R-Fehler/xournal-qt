// Contents overview over the canvas: the table of contents with its headings (chapters big and bold, sections
// smaller and indented) and, under each heading, its pages side by side (swipe sideways), to skim through chapters
// by what they look like. Tap a heading or a page to go there; the arrows collapse a chapter (then its row has all
// its pages). − / + (Ctrl+wheel, pinch) make the pages smaller or bigger.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: overview
    objectName: "contentsOverview"
    color: "#4e5156"
    visible: false
    focus: visible
    property real thumbHeight: 190

    function open() {
        visible = true
        list.positionViewAtIndex(Math.max(0, app.outline.currentRow), ListView.Beginning)
        list.forceActiveFocus()
    }
    function close() { visible = false }
    function choose(page) {
        if (page < 0) return
        app.jumpToPage(page)
        close()
    }
    function zoom(factor) { thumbHeight = Math.max(90, Math.min(700, thumbHeight * factor)) }

    ListView {
        id: list
        objectName: "contentsList"
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        clip: true
        model: app.outline
        spacing: 6
        cacheBuffer: height
        boundsBehavior: Flickable.StopAtBounds
        header: Item { height: 12 }
        footer: Item { height: 90 }  // not under the zoom controls
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: list }
        Keys.onEscapePressed: overview.close()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { overview.zoom(1.25); event.accepted = true }
            else if (event.key === Qt.Key_Minus) { overview.zoom(0.8); event.accepted = true }
        }

        delegate: Column {
            id: section
            required property int index
            required property string title
            required property int level
            required property int page
            required property int pageEnd
            required property bool hasChildren
            required property bool expanded
            readonly property int indent: Math.min(level, 6) * 28
            width: ListView.view.width - 12
            spacing: 6
            topPadding: level === 0 ? 14 : 4

            RowLayout {
                x: section.indent
                width: section.width - section.indent
                spacing: 4
                ToolButton {
                    visible: section.hasChildren
                    implicitWidth: 36; implicitHeight: 36
                    icon.source: app.iconUrl(section.expanded ? "xqt-chevron-down-small" : "xqt-chevron-right")
                    icon.color: "#e8eaed"
                    display: AbstractButton.IconOnly
                    onClicked: app.outline.toggle(section.index)
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: section.hasChildren ? 0 : 40
                    text: section.title
                    elide: Text.ElideRight
                    color: section.index === app.outline.currentRow ? "#ffffff" : "#e8eaed"
                    font.pixelSize: section.level === 0 ? 24 : section.level === 1 ? 19 : section.level === 2 ? 16 : 14
                    font.weight: section.level === 0 ? Font.Bold : section.level === 1 ? Font.DemiBold : Font.Normal
                    TapHandler { onTapped: overview.choose(section.page) }
                }
                Label {
                    text: section.page >= 0 ? qsTr("p. %1").arg(section.page + 1) : ""
                    color: "#b8bcc2"
                    font.pixelSize: 13
                }
            }
            // The pages of this heading
            ListView {
                id: strip
                objectName: "contentsPages"
                x: section.indent + (section.hasChildren ? 0 : 40)
                width: section.width - x
                height: count > 0 ? overview.thumbHeight + 22 : 0
                visible: count > 0
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: Math.max(0, width)
                model: section.page >= 0 && section.pageEnd > section.page ? section.pageEnd - section.page : 0
                ScrollBar.horizontal: ScrollBar { height: 6 }
                delegate: Item {
                    id: pageItem
                    required property int index
                    readonly property int pageNo: section.page + index
                    width: Math.round(overview.thumbHeight / app.pages.aspectOf(pageNo))
                    height: strip.height
                    Rectangle {
                        id: paper
                        width: parent.width
                        height: overview.thumbHeight
                        color: "#ffffff"
                        border.width: pageItem.pageNo === app.pageNumber - 1 ? 3 : 0
                        border.color: Material.accentColor
                        Image {
                            anchors.fill: parent
                            anchors.margins: paper.border.width
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            source: overview.visible ? app.pages.thumbnailUrl(pageItem.pageNo) : ""
                            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                        }
                        TapHandler { onTapped: overview.choose(pageItem.pageNo) }
                    }
                    Label {
                        anchors.top: paper.bottom
                        anchors.topMargin: 3
                        anchors.horizontalCenter: paper.horizontalCenter
                        text: pageItem.pageNo + 1
                        font.pixelSize: 12
                        color: "#d0d3d8"
                    }
                }
            }
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) { overview.zoom(event.angleDelta.y > 0 ? 1.15 : 1 / 1.15) }
        }
        PinchHandler {
            target: null
            property real startHeight: 190
            onActiveChanged: if (active) startHeight = overview.thumbHeight
            onActiveScaleChanged: overview.thumbHeight = Math.max(90, Math.min(700, startHeight * activeScale))
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !app.outline.available
        text: qsTr("This document has no table of contents")
        color: "#e8eaed"
        font.pixelSize: 18
    }

    // Zoom and close
    Pane {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        padding: 2
        Material.foreground: "#303030"
        background: Rectangle { radius: height / 2; color: "#f2fafafa"; border.width: 1; border.color: "#40000000" }
        RowLayout {
            spacing: 0
            ToolButton { text: qsTr("Expand all"); font.pixelSize: 13; onClicked: app.outline.expandAll(true) }
            ToolButton { text: qsTr("Chapters only"); font.pixelSize: 13; onClicked: app.outline.expandAll(false) }
            ToolSeparator {}
            ToolButton { text: "−"; font.pixelSize: 22; implicitWidth: 44; onClicked: overview.zoom(0.8) }
            ToolButton { text: "+"; font.pixelSize: 22; implicitWidth: 44; onClicked: overview.zoom(1.25) }
            ToolSeparator {}
            IconButton { iconName: "xqt-close"; tip: qsTr("Close (Esc)"); implicitWidth: 44; implicitHeight: 44; onClicked: overview.close() }
        }
    }
}
