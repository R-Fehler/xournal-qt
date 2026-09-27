// A dialog that fits every window (qt/docs/adaptive-layout.md, "Dialogs and sheets"). What is declared inside it is
// its body: it scrolls when the window is too short, while the title and the buttons stay in place. Where it goes
// follows the window's size class (win.adaptive):
//   - a desktop or a tablet: in the middle, at most the window's height less 48 px (above the soft keyboard);
//   - phone portrait: a form ("form") takes the whole screen, with × at the left and the confirm button at the top
//     right, as Android's full-screen dialogs; a question ("question") comes up from the bottom; a short confirmation
//     ("card") stays in the middle;
//   - phone landscape and short windows: forms and questions take the whole screen; cards stay in the middle.
// `fillBody` gives the body the whole height instead (a dialog with a list of its own that scrolls). Esc and Android's
// back key close it (reject), as Cancel does.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Dialog {
    id: dlg
    /// What is declared inside: the body
    default property alias body: bodyBox.data
    /// "form", "question" or "card" (above)
    property string kind: "form"
    /// The width in the middle of a desktop or tablet window (less when the window is narrower)
    property real preferredWidth: 480
    /// The body takes the height there is (its own list scrolls), up to this in the middle of a window
    property bool fillBody: false
    property real preferredHeight: 640
    /// A × in the title row also in the middle of a window (sheets without a Cancel button)
    property bool closeButton: false

    readonly property var win: ApplicationWindow.window
    readonly property var adaptive: win && win.adaptive ? win.adaptive : null
    /// The class the layout follows ("Adapt the layout" off: the desktop's)
    readonly property string sizeClass: adaptive ? adaptive.layoutClass : "desktopWide"
    readonly property real minTarget: adaptive ? adaptive.minTarget : 40
    /// The window's safe area and the soft keyboard (Main.qml: win.safeInsets, win.keyboardTop)
    readonly property bool inWindow: win !== null && win !== undefined && win.safeInsets !== undefined
    /// The top of the soft keyboard while it is open (Android reports it in the screen's pixels)
    readonly property real keyboardTop: {
        if (inWindow) return win.keyboardOpen ? win.keyboardTop : Infinity
        const r = Qt.inputMethod.keyboardRectangle
        return Qt.inputMethod.visible && r.height > 0 ? r.y / (Qt.platform.os === "android" ? Screen.devicePixelRatio : 1)
                                                      : Infinity
    }
    /// (the status bar over the window's top, the navigation bar at its bottom, a cut-out at a side)
    readonly property real safeTop: inWindow ? win.safeTop : 0
    readonly property real safeBottom: inWindow ? win.safeBottom : 0
    readonly property real safeLeft: inWindow ? win.safeLeft : 0
    readonly property real safeRight: inWindow ? win.safeRight : 0
    readonly property real roomTop: safeTop
    /// Above the soft keyboard while it is open, else above the navigation bar
    readonly property real roomBottom: parent ? Math.min(parent.height - safeBottom, keyboardTop) : 600
    /// The width inside the safe area
    readonly property real parentWidth: parent ? parent.width - safeLeft - safeRight : 800
    readonly property real margin: 24

    /// "centered", "bottom" or "fullScreen"
    readonly property string placement: {
        if (kind === "card") return "centered"
        const c = sizeClass
        const portrait = c === "phonePortrait" || (c === "tiny" && parentWidth < roomBottom)
        if (portrait) return kind === "question" ? "bottom" : "fullScreen"
        if (c === "phoneShort" || c === "tiny") return "fullScreen"
        return "centered"
    }
    readonly property bool fullScreen: placement === "fullScreen"

    // --- the footer's buttons: the confirm button moves to the top right of a full-screen sheet ---
    readonly property var footerButtons: {
        const f = dlg.footer
        const list = []
        if (!f || f.itemAt === undefined) return list
        for (let i = 0; i < f.count; ++i) {
            const b = f.itemAt(i)
            if (b) list.push(b)
        }
        return list
    }
    /// The button that confirms (OK, Save, Insert …), if there is one
    readonly property var acceptButton: {
        for (const b of footerButtons) {
            const role = b.DialogButtonBox.buttonRole
            if (b.visible && (role === DialogButtonBox.AcceptRole || role === DialogButtonBox.YesRole)) return b
        }
        return null
    }
    /// Full screen, and the footer has nothing but the confirm button and Cancel (× does that): the confirm button is
    /// shown at the top right instead, and the footer is hidden
    readonly property bool footerMoved: fullScreen && footerButtons.every(function(b) {
        const role = b.DialogButtonBox.buttonRole
        return !b.visible || b === acceptButton || role === DialogButtonBox.RejectRole
    })
    /// The footer's buttons do not fit in one row (a phone, long labels): they are shown one below the other instead,
    /// the confirm button on top
    readonly property bool footerStacked: {
        if (footerMoved || !footer || footerButtons.length < 2) return false
        let w = footer.leftPadding + footer.rightPadding
        let shown = 0
        for (const b of footerButtons) {
            if (!b.visible) continue
            w += b.implicitWidth
            ++shown
        }
        return shown > 1 && w + (shown - 1) * footer.spacing > width
    }
    /// The footer's buttons in the order of the stack: the confirm button, the others, Cancel
    readonly property var stackedButtons: {
        const rejects = footerButtons.filter(function(b) { return b.DialogButtonBox.buttonRole === DialogButtonBox.RejectRole })
        const others = footerButtons.filter(function(b) { return b !== acceptButton && rejects.indexOf(b) < 0 })
        return (acceptButton ? [acceptButton] : []).concat(others, rejects)
    }
    /// The button that confirms where it is shown now (for the tests: it is inside the window)
    readonly property Item confirmItem: footerMoved ? (acceptButton ? sheetConfirm : null)
                                      : footerStacked && acceptButton ? stackRepeater.itemAt(stackedButtons.indexOf(acceptButton))
                                                                      : acceptButton
    /// The scrolling body
    readonly property alias bodyFlickable: flick

    parent: Overlay.overlay
    modal: true
    focus: true  // (Esc and the back key reach it)
    Material.roundedScale: fullScreen ? Material.NotRounded : Material.dialogRoundedScale
    Material.elevation: fullScreen ? 0 : 6  // (nothing to lift it from)
    // (a phone's width is precious: less padding at the sides of a full-screen sheet)
    leftPadding: fullScreen ? 16 : 24
    rightPadding: fullScreen ? 16 : 24

    width: fullScreen ? parentWidth
                      : placement === "bottom" ? Math.min(parentWidth, 640)
                                               : Math.min(preferredWidth, parentWidth - 32)
    height: {
        const room = roomBottom - roomTop
        if (fullScreen) return room
        const most = placement === "bottom" ? room - 32 : room - 2 * margin
        return Math.max(0, Math.min(fillBody ? preferredHeight : implicitHeight, most))
    }
    x: safeLeft + Math.round((parentWidth - width) / 2)
    y: fullScreen ? roomTop
                  : placement === "bottom" ? roomBottom - height
                                           : Math.round(Math.max(roomTop + margin,
                                                                 Math.min(((parent ? parent.height : 600) - height) / 2,
                                                                          roomBottom - height - margin)))

    /// Scrolls the body so that this item (a field that got the keys) is in view
    function ensureVisible(item) {
        if (!item || fillBody || flick.contentHeight <= flick.height) return
        let p = item
        while (p && p !== bodyBox) p = p.parent
        if (!p) return
        const r = item.mapToItem(bodyBox, 0, 0, item.width, item.height)
        if (r.y < flick.contentY)
            flick.contentY = Math.max(0, r.y - 8)
        else if (r.y + r.height > flick.contentY + flick.height)
            flick.contentY = Math.min(r.y + r.height + 8 - flick.height, flick.contentHeight - flick.height)
    }
    onOpened: flick.contentY = 0

    header: Item {
        implicitHeight: !visible ? 0 : dlg.fullScreen ? Math.max(56, dlg.minTarget + 8)
                                                      : titleRow.implicitHeight + titleRow.anchors.topMargin
        visible: dlg.fullScreen || dlg.title !== "" || dlg.closeButton
        RowLayout {
            id: titleRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: dlg.fullScreen ? undefined : parent.top
            anchors.verticalCenter: dlg.fullScreen ? parent.verticalCenter : undefined
            anchors.topMargin: dlg.fullScreen ? 0 : (dlg.closeButton ? 12 : 24)
            anchors.leftMargin: dlg.fullScreen ? 4 : 24
            anchors.rightMargin: dlg.fullScreen || dlg.closeButton ? 8 : 24
            spacing: 4
            IconButton {
                objectName: "dialogCloseButton"
                visible: dlg.fullScreen
                iconName: "xqt-close"
                tip: qsTr("Close")
                implicitWidth: Math.max(48, dlg.minTarget)
                implicitHeight: Math.max(48, dlg.minTarget)
                onClicked: dlg.reject()
            }
            Label {
                objectName: "dialogTitle"
                Layout.fillWidth: true
                Layout.leftMargin: dlg.fullScreen ? 8 : 0
                text: dlg.title
                elide: Label.ElideRight
                font.pixelSize: dlg.fullScreen ? 20 : Material.dialogTitleFontPixelSize
                font.weight: dlg.fullScreen ? Font.DemiBold : Font.Normal
            }
            Button {
                id: sheetConfirm
                objectName: "dialogConfirmButton"
                visible: dlg.footerMoved && dlg.acceptButton !== null
                text: dlg.acceptButton ? dlg.acceptButton.text : ""
                enabled: dlg.acceptButton ? dlg.acceptButton.enabled : false
                highlighted: true
                flat: true
                implicitHeight: Math.max(48, dlg.minTarget)
                onClicked: if (dlg.acceptButton) dlg.acceptButton.clicked()
            }
            IconButton {
                objectName: "dialogCornerClose"
                visible: dlg.closeButton && !dlg.fullScreen
                iconName: "xqt-close"
                tip: qsTr("Close (Esc)")
                onClicked: dlg.reject()
            }
        }
        Rectangle {  // a line under the title once the body is scrolled under it
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#e0e0e0"
            visible: flick.contentY > 1
        }
    }

    contentItem: Item {
        implicitWidth: dlg.preferredWidth - dlg.leftPadding - dlg.rightPadding
        implicitHeight: (dlg.fillBody ? 0 : bodyBox.implicitHeight) + (stack.visible ? stack.implicitHeight + 12 : 0)
        Flickable {
            id: flick
            objectName: "dialogBody"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: stack.visible ? stack.top : parent.bottom
            anchors.bottomMargin: stack.visible ? 12 : 0
            contentWidth: width
            contentHeight: dlg.fillBody ? height : bodyBox.height
            interactive: !dlg.fillBody && contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: bodyBar
            Item {
                id: bodyBox
                width: flick.width
                height: dlg.fillBody ? flick.height : implicitHeight
                implicitHeight: childrenRect.y + childrenRect.height
            }
            // The footer goes when its buttons moved to the title row (and comes back with the middle of a window).
            // (Not by `visible` or `enabled`: its buttons would follow, and whether they are shown and enabled is what
            // the title row's button follows. With no height it lies below the sheet, at the window's bottom edge.)
            Binding {
                target: dlg.footer
                when: (dlg.footerMoved || dlg.footerStacked) && dlg.footer !== null
                property: "implicitHeight"
                value: 0
            }
            Binding {
                target: dlg.footer
                when: (dlg.footerMoved || dlg.footerStacked) && dlg.footer !== null
                property: "opacity"
                value: 0
            }
            // Android's back key closes it as Esc does (Qt closes a popup on it only while the popup has the keys)
            Shortcut {
                sequences: ["Back"]
                enabled: dlg.opened && (dlg.closePolicy & Popup.CloseOnEscape)
                onActivated: dlg.reject()
            }
            // A field that gets the keys is scrolled into view, and again when the soft keyboard makes room smaller
            Connections {
                target: dlg.win
                enabled: dlg.opened
                function onActiveFocusItemChanged() { dlg.ensureVisible(dlg.win.activeFocusItem) }
            }
            Connections {
                target: dlg
                function onHeightChanged() {
                    if (dlg.opened && dlg.win) Qt.callLater(dlg.ensureVisible, dlg.win.activeFocusItem)
                }
            }
        }
        // The footer's buttons one below the other (footerStacked), each doing what its button in the footer does
        ColumnLayout {
            id: stack
            objectName: "dialogButtonStack"
            visible: dlg.footerStacked
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 0
            Repeater {
                id: stackRepeater
                model: dlg.footerStacked ? dlg.stackedButtons : []
                delegate: Button {
                    required property var modelData
                    objectName: modelData.objectName !== "" ? modelData.objectName + "Stacked" : ""
                    Layout.fillWidth: true
                    implicitHeight: Math.max(48, dlg.minTarget)
                    visible: modelData.visible
                    enabled: modelData.enabled
                    text: modelData.text
                    flat: modelData !== dlg.acceptButton
                    highlighted: modelData === dlg.acceptButton
                    onClicked: modelData.clicked()
                }
            }
        }
        // (in the padding at the right, not over the body)
        ScrollBar {
            id: bodyBar
            orientation: Qt.Vertical
            x: parent.width + Math.max(0, (dlg.rightPadding - width) / 2)
            y: 0
            height: flick.height
            policy: flick.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
    }
}
