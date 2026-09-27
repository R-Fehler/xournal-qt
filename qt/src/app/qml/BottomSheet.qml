// A bottom sheet of the phone classes (the tools of the phone chrome, the colors, the widths): as wide as the window
// (at most 640 px, centred), at most 85 % of it high (the rest scrolls), above the bottom safe area, with the handle
// of MenuSheet. A drag down on the handle, a tap beside it, Esc and Android's back key close it.
// What is declared inside is its body: it sizes itself (`width: parent.width`), its height is what it needs.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: sheet
    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
    /// On top of the sheet ("": none)
    property string title: ""
    default property alias body: bodyItem.data
    readonly property Flickable flickable: flick
    readonly property real safeTop: typeof win !== "undefined" && win && win.safeTop ? win.safeTop : 0
    readonly property real safeBottom: typeof win !== "undefined" && win && win.safeBottom ? win.safeBottom : 0

    width: parent ? Math.min(parent.width, 640) : 360
    x: parent ? Math.round((parent.width - width) / 2) : 0
    height: parent ? Math.min(implicitHeight, Math.round(parent.height * 0.85), parent.height - safeTop - 8) : implicitHeight
    y: parent ? parent.height - height + handle.offset + slide : 0
    /// Slides it in and out
    property real slide: 0
    enter: Transition { NumberAnimation { property: "slide"; from: sheet.height; to: 0; duration: 180; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "slide"; to: sheet.height; duration: 150; easing.type: Easing.InCubic } }
    onAboutToShow: {
        handle.offset = 0
        flick.contentY = 0
    }

    background: Rectangle {
        radius: 16
        color: "#ffffff"
        Rectangle {  // (square at the bottom: the sheet rests on the window's edge)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        // Android's back key (Qt closes a popup on it only while the popup has the keys)
        Shortcut {
            sequence: "Back"
            enabled: sheet.opened
            onActivated: sheet.close()
        }
        MenuSheetHandle {
            id: handle
            Layout.fillWidth: true
            onDismissed: sheet.close()
        }
        Label {
            objectName: "bottomSheetTitle"
            visible: sheet.title !== ""
            text: sheet.title
            font.pixelSize: 16
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 6
        }
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: contentHeight
            Layout.minimumHeight: Math.min(contentHeight, 48)
            contentWidth: width
            contentHeight: bodyItem.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height + 1
            ScrollBar.vertical: ScrollBar { policy: flick.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            Item {
                id: bodyItem
                width: flick.width - (flick.interactive ? 12 : 0)  // (the scroll bar beside it)
                height: childrenRect.height
            }
        }
        Item { implicitHeight: 8 + sheet.safeBottom }
    }
}
