// One of the user's tools in the toolbox (qt/docs/toolbox.md): its kind as an icon and a sample of its ink below it
// (its color, how wide, dashed or filled), like a pen lying in a sorted box. The entry in hand is lifted towards the
// page, as a pen picked up. A stack (a folded section) shows the entry used last of its section with dots for how many
// it holds.
// A tap picks it up (the toolbox decides what a tap on the one in hand does: its editor); a long press or a right
// click: its menu; the mouse wheel over it: the width (the toolbox handles these through the signals).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "LineStyles.js" as LineStyles

AbstractButton {
    id: button
    /// The entry ({id, type, color, role, width, …})
    property var entry: ({})
    /// Its color now (its role's in the chosen palette, else its own)
    property color inkColor: "#303030"
    /// It is the tool in hand
    property bool inHand: false
    /// Where the page is from the button: the side it lifts towards ("left", "right", "up", "down")
    property string towardsPage: "left"
    /// A folded section: how many entries the stack holds (0: not a stack)
    property int stackCount: 0
    /// Its name (tips, the held finger, accessibility)
    property string name: ""
    property real cell: 48
    /// It is being dragged to another place (qt/docs/toolbox.md, "Carrying"): drawn lifted with a shadow
    property bool dragging: false
    /// An app item ({id, app}: the hand, select, …): the icon of its button (it has no ink)
    property string appIcon: ""

    signal held(point pos)
    signal secondaryClicked(point pos)
    signal wheelStepped(int steps)
    /// Reordering (qt/docs/toolbox.md, "Reordering"): held 400 ms without moving, then moved: the pointer in the
    /// scene while it moves, and where it was let go
    signal dragMoved(point scenePos)
    signal dropped(point scenePos)
    signal dragCanceled()
    /// It can be dragged to another place (a stack's list cannot)
    property bool reorderable: true
    /// Held long enough: lifted, ready to be moved (letting go without moving opens the menu)
    readonly property bool armed: touchArea.armed

    readonly property string type: entry && entry.type ? entry.type : ""
    readonly property real width_: entry && entry.width ? entry.width : 1

    implicitWidth: cell
    implicitHeight: cell
    focusPolicy: Qt.NoFocus  // (the keys stay with the page)
    hoverEnabled: true
    Accessible.name: name
    Accessible.role: Accessible.Button
    ToolTip.visible: (hovered || penHover.hovered) && !down && !lifted && name !== ""
    ToolTip.text: name
    ToolTip.delay: 600

    /// The icon of its kind (and variant)
    function iconOf(e) {
        if (e && e.app !== undefined) return appIcon !== "" ? appIcon : "xqt-tools-more"
        if (!e || !e.type) return "xopp-tool-pencil"
        switch (e.type) {
        case "pen": return "xopp-tool-pencil"
        case "highlighter": return "xopp-tool-highlighter"
        case "eraser": return e.variant === "whiteout" ? "xqt-eraser-whiteout"
                              : e.variant === "deleteStroke" ? "xqt-eraser-stroke" : "xopp-tool-eraser"
        case "shape": return ({ "line": "xopp-draw-line", "rectangle": "xopp-draw-rect", "ellipse": "xopp-draw-ellipse",
                                "arrow": "xopp-draw-arrow", "doubleArrow": "xopp-draw-double-arrow",
                                "drawCoordinateSystem": "xopp-draw-coordinate-system",
                                "strokeRecognizer": "xopp-shape-recognizer" })[e.variant] || "xqt-shapes"
        case "text": return "xqt-text-box"
        case "sticky": return "xqt-sticky-note"
        case "laser": return "xopp-laser-pointer"
        case "snip": return e.variant === "lasso" ? "xqt-snip-lasso" : "xqt-snip-rect"
        }
        return "xopp-tool-pencil"
    }

    // Picked up: lifted towards the page; dragged: lifted more, larger, with a shadow
    /// Held (ready to be carried) or carried: lifted off the rail, larger, with a shadow (not the rail moving)
    readonly property bool lifted: dragging || armed
    readonly property real lift: lifted ? 0 : (inHand ? 5 : 0)
    transform: Translate {
        x: button.towardsPage === "left" ? -button.lift : button.towardsPage === "right" ? button.lift : 0
        y: button.towardsPage === "up" ? -button.lift : button.towardsPage === "down" ? button.lift : 0
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
    scale: lifted ? 1.15 : 1
    // (carried: its place stays faint, the carried one follows the pointer)
    opacity: dragging && enabled ? 0.3 : 1
    Behavior on scale { NumberAnimation { duration: 120 } }

    background: Rectangle {
        radius: 10
        color: button.lifted ? "#ffffff" : button.inHand ? "#e0e3f5" : (button.down ? "#e8e8e8" : "transparent")
        border.width: button.inHand || button.lifted ? 1 : 0
        border.color: button.lifted ? "#9aa0a6" : Qt.rgba(Material.accentColor.r, Material.accentColor.g, Material.accentColor.b, 0.5)
        // (the shadow of a dragged entry)
        Rectangle {
            visible: button.lifted
            z: -1
            anchors.fill: parent
            anchors.margins: -3
            anchors.topMargin: 2
            anchors.bottomMargin: -6
            radius: 13
            color: "#33000000"
        }
    }

    contentItem: Item {
        // A sticky note: the note itself, in its pastel
        Rectangle {
            visible: button.type === "sticky"
            anchors.centerIn: parent
            width: 26; height: 26
            radius: 3
            color: button.inkColor
            border.width: 1
            border.color: "#40000000"
            Rectangle {  // (its folded corner)
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 8; height: 8
                color: Qt.darker(button.inkColor, 1.12)
            }
        }
        Image {
            visible: button.type !== "sticky"
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(parent.height / 2 - height + 4)
            source: app.iconUrl(button.iconOf(button.entry))
            sourceSize.width: 22
            sourceSize.height: 22
        }
        // The ink: a stroke of its color, as wide as it draws (on a log scale), dashed as it draws; a highlighter's
        // translucent; an eraser's a gray dot of its size; a text box's "Aa" in its font and color
        Canvas {
            id: sample
            objectName: "toolSample"
            visible: ["pen", "highlighter", "shape", "eraser", "laser"].indexOf(button.type) >= 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(parent.height / 2 + 6)
            width: 28
            height: 12
            readonly property var key: [button.inkColor, button.width_, button.type, button.entry ? button.entry.lineStyle : "",
                                        button.entry && button.entry.fill ? button.entry.fill.on : false,
                                        button.entry ? button.entry.base : ""]
            onKeyChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const t = button.type
                const w = Math.max(1.5, Math.min(9, 1.2 + Math.log(button.width_ + 1) * 1.6))
                if (t === "eraser") {
                    const r = Math.max(2.5, Math.min(5.5, w * 0.7))
                    ctx.fillStyle = "#c8ccd1"
                    ctx.strokeStyle = "#80868b"
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
                    ctx.fill()
                    ctx.stroke()
                    return
                }
                const highlighter = t === "highlighter" || button.entry.base === "highlighter"
                ctx.globalAlpha = highlighter ? 0.6 : 1
                if (button.entry && button.entry.fill && button.entry.fill.on) {
                    ctx.fillStyle = button.inkColor
                    ctx.globalAlpha = highlighter ? 0.3 : 0.35
                    ctx.fillRect(3, 1, width - 6, height - 2)
                    ctx.globalAlpha = highlighter ? 0.6 : 1
                }
                ctx.strokeStyle = button.inkColor
                ctx.lineWidth = highlighter ? Math.max(w, 6) : w
                const style = button.entry.lineStyle || "plain"
                ctx.lineCap = highlighter ? "butt" : LineStyles.sampleCap(style, "round")
                ctx.setLineDash(LineStyles.sampleDashes(style, ctx.lineWidth))
                ctx.beginPath()
                ctx.moveTo(4 + ctx.lineWidth / 2, height / 2)
                ctx.lineTo(width - 4 - ctx.lineWidth / 2, height / 2)
                ctx.stroke()
                // A very light color shows its edge on the white rail
                if (!highlighter && Qt.colorEqual(button.inkColor, "#ffffff")) {
                    ctx.setLineDash([])
                    ctx.lineWidth = 0.5
                    ctx.strokeStyle = "#9aa0a6"
                    ctx.strokeRect(4, height / 2 - w / 2, width - 8, w)
                }
            }
        }
        Label {
            visible: button.type === "text"
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(parent.height / 2 + 2)
            text: "Aa"
            color: button.inkColor
            font.family: button.entry && button.entry.font ? button.entry.font.family : ""
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        // A snip (a cycling tool): two dots, the one of its shape in color (ToolCycleButton's dots)
        Row {
            objectName: "snipDots"
            visible: button.type === "snip"
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(parent.height / 2 + 9)
            spacing: 3
            Repeater {
                model: ["rect", "lasso"]
                delegate: Rectangle {
                    required property string modelData
                    width: 4; height: 4; radius: 2
                    color: (button.entry.variant || "rect") === modelData
                           ? (button.inHand ? Material.accentColor : "#505050") : "#b4b8bd"
                }
            }
        }
        // A stack: dots for the entries it holds
        Row {
            visible: button.stackCount > 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            spacing: 2
            Repeater {
                model: Math.min(button.stackCount, 6)
                delegate: Rectangle { width: 3; height: 3; radius: 1.5; color: "#80868b" }
            }
        }
    }

    HoverHandler { id: penHover; acceptedDevices: PointerDevice.Stylus }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            const steps = event.angleDelta.y > 0 ? 1 : event.angleDelta.y < 0 ? -1 : 0
            if (steps !== 0) button.wheelStepped(steps)
        }
    }
    // A tap picks it up; a right click: the menu; held (400 ms) and let go without moving: the menu; held and moved:
    // it is carried to another place. A drag at once scrolls the rail (the press goes to the rail's Flickable).
    MouseArea {
        id: touchArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        pressAndHoldInterval: 400
        property bool armed: false
        property bool moving: false
        property point start
        // (held: the rail may not take the drag for scrolling any more)
        preventStealing: armed
        onPressed: function(mouse) {
            armed = false
            moving = false
            start = Qt.point(mouse.x, mouse.y)
            button.down = true
        }
        onPressAndHold: function(mouse) {
            if (mouse.button === Qt.RightButton) return
            armed = true
        }
        onPositionChanged: function(mouse) {
            if (!armed) return
            if (!moving && Math.hypot(mouse.x - start.x, mouse.y - start.y) < 8) return
            if (!button.reorderable) return
            moving = true
            button.dragging = true
            button.dragMoved(mapToItem(null, mouse.x, mouse.y))
        }
        onReleased: function(mouse) {
            button.down = undefined
            if (moving) {
                button.dragging = false
                button.dropped(mapToItem(null, mouse.x, mouse.y))
            } else if (armed) {
                button.held(Qt.point(mouse.x, mouse.y))
            }
            armed = false
            moving = false
        }
        onCanceled: {
            button.down = undefined
            if (moving) button.dragCanceled()
            button.dragging = false
            armed = false
            moving = false
        }
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) button.secondaryClicked(Qt.point(mouse.x, mouse.y))
            else if (!armed) button.clicked()
        }
    }
    // (for the tests and the keyboard: what a long press does)
    onPressAndHold: held(Qt.point(width / 2, height / 2))
}
