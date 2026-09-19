// Number of search hits on a page (hidden without hits).
import QtQuick
import QtQuick.Controls

Rectangle {
    property int count: 0
    visible: count > 0
    width: label.implicitWidth + 12
    height: 20
    radius: 10
    color: "#ffd200"
    Label {
        id: label
        anchors.centerIn: parent
        text: parent.count
        font.pixelSize: 12
        font.weight: Font.DemiBold
        color: "#5a3d00"
    }
}
