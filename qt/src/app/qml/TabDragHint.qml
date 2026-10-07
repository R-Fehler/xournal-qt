// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// While a tab is dragged off the strip: what happens when it is let go
Rectangle {
    objectName: "tabDragHint"
    visible: tabStrip.dragIndex >= 0
    z: 60
    anchors.horizontalCenter: parent.horizontalCenter
    y: 12
    width: hintRow.implicitWidth + 28
    height: 44
    radius: 22
    readonly property bool willMove: tabStrip.dragDistance > tabStrip.undockDistance
    color: willMove ? Material.accentColor : "#e8eaed"
    border.width: 1
    border.color: willMove ? Material.accentColor : "#c9ccd1"
    opacity: 0.96
    RowLayout {
        id: hintRow
        anchors.centerIn: parent
        spacing: 8
        Label {
            text: app.secondaryWindow ? (parent.parent.willMove ? qsTr("Let go: back to the main window")
                                                                : qsTr("Drag down: back to the main window"))
                                      : (parent.parent.willMove ? qsTr("Let go: a window of its own")
                                                                : qsTr("Drag down: a window of its own"))
            color: parent.parent.willMove ? "#ffffff" : "#3c4043"
            font.weight: Font.DemiBold
        }
    }
}
