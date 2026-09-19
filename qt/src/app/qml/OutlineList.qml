// The table of contents as text, for the page sidebar: indented by level, the current section highlighted; tap to
// go there, the arrows collapse and expand.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ListView {
    id: list
    objectName: "outlineList"
    clip: true
    model: app.outline
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: app.outline.currentRow
    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
    ScrollBar.vertical: ScrollBar {}
    TouchpadMomentum { flickable: list }

    delegate: ItemDelegate {
        id: entry
        required property int index
        required property string title
        required property int level
        required property int page
        required property bool hasChildren
        required property bool expanded
        width: ListView.view.width
        leftPadding: 4 + Math.min(level, 6) * 12
        rightPadding: 6
        topPadding: 6
        bottomPadding: 6
        highlighted: index === app.outline.currentRow
        onClicked: if (page >= 0) app.jumpToPage(page)
        contentItem: RowLayout {
            spacing: 2
            ToolButton {
                visible: entry.hasChildren
                implicitWidth: 28; implicitHeight: 28
                icon.source: app.iconUrl(entry.expanded ? "xqt-chevron-down-small" : "xqt-chevron-right")
                icon.width: 16; icon.height: 16
                display: AbstractButton.IconOnly
                onClicked: app.outline.toggle(entry.index)
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: entry.hasChildren ? 0 : 30
                text: entry.title
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: entry.level === 0 ? 14 : 13
                font.weight: entry.level === 0 ? Font.DemiBold : Font.Normal
                color: entry.page >= 0 ? "#202124" : "#9aa0a6"
            }
            Label {
                text: entry.page >= 0 ? entry.page + 1 : ""
                font.pixelSize: 12
                color: "#6b6f75"
            }
        }
    }
}
