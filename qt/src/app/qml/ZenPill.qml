// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The dot's pill, over the page beside the dot (the page does not move): Show controls (leaves Zen), Read only,
// the page number (a tap: all pages, to go to one), fit the width / the whole page
Pane {
    id: zenPill
    objectName: "zenPill"
    property bool opened: false
    visible: opened && zenDot.visible
    onVisibleChanged: if (!visible) opened = false
    z: 92
    x: Math.min(zenDot.x + zenDot.width - 6, win.layout.canvasControlsRight - width - 8)
    y: Math.max(win.layout.canvasControlsTop + 8, zenDot.y + zenDot.height - height - 4)
    padding: 4
    Material.foreground: "#303030"
    background: Rectangle {
        radius: 14
        color: "#f7fafafa"
        border.width: 1
        border.color: "#33000000"
    }
    ColumnLayout {
        spacing: 0
        ToolButton {
            objectName: "zenShowControls"
            Layout.fillWidth: true
            implicitHeight: Math.max(44, win.adaptive.minTarget)
            text: qsTr("Show controls")
            icon.source: app.iconUrl("xqt-eye")
            icon.width: 22
            icon.height: 22
            display: AbstractButton.TextBesideIcon
            focusPolicy: Qt.NoFocus
            onClicked: {
                zenPill.opened = false
                win.modes.readStarted = false  // (Read broken up: read only and full screen stay)
                win.modes.setZen(false)
            }
        }
        Switch {
            id: zenReadOnly
            objectName: "zenReadOnly"
            visible: win.modes.readOnlyOffered
            Layout.fillWidth: true
            implicitHeight: Math.max(44, win.adaptive.minTarget)
            text: qsTr("Read only")
            focusPolicy: Qt.NoFocus
            checked: win.modes.readOnlyOn
            onToggled: {
                win.modes.readOnly = checked
                checked = Qt.binding(function() { return win.modes.readOnlyOn })
            }
        }
        RowLayout {
            spacing: 2
            ToolButton {
                objectName: "zenPage"
                Layout.fillWidth: true
                implicitHeight: Math.max(44, win.adaptive.minTarget)
                text: app.pageNumber + " / " + app.pageCount
                font.pixelSize: 14
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Go to a page")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Go to a page (all pages)")
                ToolTip.delay: 600
                onClicked: {
                    zenPill.opened = false
                    pageGrid.open()
                }
            }
            IconButton {
                objectName: "zenFitWidth"
                iconName: "xqt-fit-width"
                implicitWidth: Math.max(44, win.adaptive.minTarget)
                implicitHeight: implicitWidth
                tip: qsTr("Fit the width")
                onClicked: {
                    zenPill.opened = false
                    app.keyTarget.fitWidth()
                }
            }
            IconButton {
                objectName: "zenFitPage"
                iconName: "xqt-page-single"
                implicitWidth: Math.max(44, win.adaptive.minTarget)
                implicitHeight: implicitWidth
                tip: qsTr("The whole page")
                onClicked: {
                    zenPill.opened = false
                    app.fitPage()
                }
            }
        }
    }
}
