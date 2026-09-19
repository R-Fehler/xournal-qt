// A selected page preview: tinted, with a check mark.
import QtQuick
import QtQuick.Controls.Material

Item {
    anchors.fill: parent
    Rectangle {
        anchors.fill: parent
        color: Material.accentColor
        opacity: 0.18
    }
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        width: 24; height: 24; radius: 12
        color: Material.accentColor
        Text {
            anchors.centerIn: parent
            text: "✓"
            color: "white"
            font.pixelSize: 15
            font.bold: true
        }
    }
}
