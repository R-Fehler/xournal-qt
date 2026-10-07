// A touch-sized tool bar button showing one of the upstream Xournal++ (Lucide) icons.
// Its name without hover (qt/docs/features/adaptive-layout.md, "Labels without hover"): the mouse and the pen's hover
// show the tip after a moment; a finger held on it shows its `label` above the finger while held, and letting go then
// does not press it. A button with a long press of its own (`ownHold`: a menu of its variants, a dialog) keeps that;
// the popup it opens shows the button's name. A mouse or pen held long on a plain button still presses it on release.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ToolButton {
    id: control
    // A tap or click does not take the focus (the keys stay with the page, and the text being written keeps the
    // on-screen keyboard); Tab still reaches it
    focusPolicy: Qt.TabFocus
    property string iconName: ""
    /// What the tool tip says (may name the shortcut and what a long press does)
    property string tip: ""
    /// Its short name: shown while a finger is held on it, and beside it in the "more tools" overflow ("": the tip)
    property string label: ""
    readonly property string name: label !== "" ? label : tip
    /// The button does something of its own on a long press (then no label is shown for a held finger)
    property bool ownHold: false
    /// A finger is held on it: its name shows above the finger
    readonly property bool labelShown: heldLabel
    property bool heldLabel: false
    property bool heldPointer: false
    implicitWidth: 48
    implicitHeight: 48
    checkable: false
    icon.source: control.iconName !== "" ? app.iconUrl(control.iconName) : ""
    icon.width: 26
    icon.height: 26
    icon.color: enabled ? (checked ? Material.accentColor : "#303030") : "#b0b0b0"
    display: AbstractButton.IconOnly
    ToolTip.visible: !heldLabel && (hovered || penHover.hovered) && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 600
    // The name above a finger held on it (at once, while held; made only then)
    Loader {
        active: control.heldLabel && control.name !== ""
        sourceComponent: ToolTip {
            objectName: "heldLabel"
            visible: true
            text: control.name
            delay: 0
            x: (control.width - width) / 2
            y: -height - 12
        }
    }
    background: Rectangle {
        radius: 10
        color: control.checked ? "#e0e3f5" : (control.pressed ? "#e8e8e8" : "transparent")
    }
    // The pen's hover counts as hover (the controls' `hovered` follows the mouse)
    HoverHandler { id: penHover; acceptedDevices: PointerDevice.Stylus }
    // Which device pressed it: a finger held shows the name; a mouse or pen held long is still a press
    PointHandler { id: finger; acceptedDevices: PointerDevice.TouchScreen }
    onPressAndHold: {
        if (control.ownHold) return
        if (finger.active) control.heldLabel = true
        else control.heldPointer = true
    }
    onReleased: {
        if (control.heldLabel) {
            control.heldLabel = false  // (only the name was asked for)
        } else if (control.heldPointer) {
            control.heldPointer = false
            control.clicked()
        }
    }
    onCanceled: { control.heldLabel = false; control.heldPointer = false }
}
