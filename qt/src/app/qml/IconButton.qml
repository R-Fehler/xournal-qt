// A touch-sized tool bar button showing one of the upstream Xournal++ (Lucide) icons.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ToolButton {
    id: control
    property string iconName: ""
    property string tip: ""
    implicitWidth: 48
    implicitHeight: 48
    checkable: false
    icon.source: app.iconUrl(control.iconName)
    icon.width: 26
    icon.height: 26
    icon.color: enabled ? (checked ? Material.accentColor : "#303030") : "#b0b0b0"
    display: AbstractButton.IconOnly
    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 600
    background: Rectangle {
        radius: 10
        color: control.checked ? "#e0e3f5" : (control.pressed ? "#e8e8e8" : "transparent")
    }
}
