// Toggle: show only the pages with search hits (sidebar, page grid) — to skim long documents.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AbstractButton {
    id: chip
    objectName: "searchFilterChip"
    checkable: true
    checked: app.filteredPages.onlySearchHits
    onToggled: app.filteredPages.onlySearchHits = checked
    implicitHeight: 36
    implicitWidth: row.implicitWidth + 20
    padding: 0
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Show only the pages with search hits")
    ToolTip.delay: 600

    background: Rectangle {
        radius: height / 2
        color: chip.checked ? "#fff3c4" : "#ffffff"
        border.width: 1
        border.color: chip.checked ? "#f9a825" : "#c9ccd1"
    }
    contentItem: Item {
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Image { source: app.iconUrl("xqt-filter"); sourceSize.width: 16; sourceSize.height: 16 }
            Label {
                text: app.searchRunning && app.searchHitPageCount === 0 ? qsTr("Searching…")
                    : app.searchHitPageCount === 1 ? qsTr("1 page with hits")
                    : qsTr("%1 pages with hits").arg(app.searchHitPageCount)
                font.pixelSize: 13
                font.weight: chip.checked ? Font.DemiBold : Font.Normal
                color: "#303030"
            }
        }
    }
}
