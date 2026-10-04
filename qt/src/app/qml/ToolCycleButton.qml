// One button for tools that do almost the same (qt/docs/adaptive-layout.md, "Cycling buttons"; ToolGroups.qml):
// its icon is the variant in use (or the one last used), the dots below say how many there are and which one it is.
// A tap on it while its tool is in use takes the next variant, else its tool with the variant last used; a long
// press (or a right click) lists all variants with their names, to pick one.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "Popups.js" as Popups

IconButton {
    id: button
    /// The group of ToolGroups: "pen", "select", "shape", "geometry", "eraser"
    property string group
    /// Where the logic is (Main.qml's `toolGroups`)
    property var groups: win.toolGroups
    readonly property var cycleVariants: groups.cycle(group)
    readonly property string currentKey: groups.current(group)
    readonly property var currentVariant: groups.variant(group, currentKey)
    readonly property int currentIndex: {
        for (let i = 0; i < cycleVariants.length; ++i) if (cycleVariants[i].key === currentKey) return i
        return -1
    }
    readonly property bool active: groups.isActive(group)
    iconName: currentVariant.icon
    label: currentVariant.name
    tip: qsTr("%1 (tap again: the next one; hold: all of them)").arg(currentVariant.name)
    checked: active
    ownHold: true
    onClicked: groups.tap(group)
    onPressAndHold: openVariants()
    function openVariants(pos) { Popups.openAt(variantMenu, pos) }
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
        onTapped: function(point) { button.openVariants(point.position) }
    }

    // How many variants, and which one: small dots under the icon
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(2, (parent.height - button.icon.height) / 2 - 8)
        spacing: 3
        visible: button.cycleVariants.length > 1
        Repeater {
            model: button.cycleVariants.length
            delegate: Rectangle {
                required property int index
                width: 4
                height: 4
                radius: 2
                color: index === button.currentIndex ? (button.checked ? Material.accentColor : "#505050") : "#b4b8bd"
            }
        }
    }

    // All variants, with the group's name on top (its name for a finger held on it)
    AdaptiveMenu {
        id: variantMenu
        objectName: button.objectName + "Variants"
        title: button.groups.name(button.group)
        titleShown: true
        Repeater {
            model: button.groups.variants(button.group)
            delegate: AdaptiveMenuItem {
                required property var modelData
                objectName: "variant_" + modelData.key
                text: modelData.name
                icon.source: app.iconUrl(modelData.icon)
                checkable: true
                checked: modelData.curtain === true ? app.curtain === modelData.key
                                                    : button.active && button.currentKey === modelData.key
                onTriggered: button.groups.activate(button.group, modelData.key)
            }
        }
        // The setsquare and the compass leave the page again
        AdaptiveMenuItem {
            objectName: "geometryPutAwayItem"
            offered: button.group === "geometry" && button.active
            text: qsTr("Take it off the page")
            icon.source: app.iconUrl("xqt-close")
            onTriggered: app.toggleGeometryTool("")
        }
        // The pen's options: its line style and filling (the pen draws the shapes too)
        PenStyleOptions {
            offered: (button.group === "pen" || button.group === "shape") && (app.hasLineStyle || app.hasFill)
        }
    }
}
