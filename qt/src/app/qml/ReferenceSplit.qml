// Reference mode: the canvas area of the window, split in two when the current tab shows another document beside
// its own (app.reference). The main canvas (Main.qml's `canvas`) takes mainX / mainWidth; the reference is a plain
// canvas for reading on the other side, behind a divider that can be dragged, with a small pill of its own: the page
// (a tap: go to a page), fit width, swap sides, swap roles, close. The main document has a thin frame, so it is
// always clear which side is written in.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import XournalQt.Canvas

Item {
    id: split
    objectName: "referenceSplit"
    /// (the area itself takes no input: DocumentCanvasItem looks through it, to the main canvas under it)
    property bool inputTransparent: true
    readonly property bool active: app.reference.active && !app.homeVisible
    readonly property bool onLeft: app.reference.onLeft
    /// The gap between the two canvases; the divider's handle is wider (touch)
    readonly property real gap: 8
    /// While the divider is dragged: its share of the main document (written to the setting when let go)
    property real liveRatio: app.reference.ratio
    readonly property real ratio: dividerDrag.active ? liveRatio : app.reference.ratio
    readonly property real mainWidth: active ? Math.round((width - gap) * ratio) : width
    readonly property real referenceWidth: active ? width - gap - mainWidth : 0
    readonly property real mainX: active && onLeft ? referenceWidth + gap : 0
    readonly property real referenceX: onLeft ? 0 : mainWidth + gap
    readonly property alias referenceCanvas: referenceCanvas

    // The keys act on the reference while it (or its pill) has the focus (AppController)
    Binding {
        target: app.reference
        property: "focused"
        value: referenceScope.activeFocus
    }

    Rectangle {  // the gap: the window's background, as around the pages
        visible: split.active
        x: split.onLeft ? split.referenceWidth : split.mainWidth
        width: split.gap
        height: split.height
        color: "#4a4d51"
        property bool inputTransparent: true
    }

    FocusScope {
        id: referenceScope
        objectName: "referenceScope"
        x: split.referenceX
        width: split.referenceWidth
        height: split.height
        visible: split.active

        DocumentCanvas {
            id: referenceCanvas
            objectName: "referenceCanvas"
            anchors.fill: parent
            clip: true
            focus: true
            readingOnly: true
            view: split.active ? app.reference.view : null
        }

        // A tapped link: open it / go to the page (not at once: a tap can be a mistake)
        Popup {
            id: referenceLinkPopup
            objectName: "referenceLinkPopup"
            property string uri
            property int page: -1
            padding: 6
            Connections {
                target: app.reference
                function onLinkTapped(uri, page, rect) {
                    referenceLinkPopup.uri = uri
                    referenceLinkPopup.page = page
                    referenceLinkPopup.x = Math.max(8, Math.min(rect.x, referenceScope.width - referenceLinkPopup.width - 8))
                    referenceLinkPopup.y = rect.y + rect.height + 60 > referenceScope.height ? rect.y - 60
                                                                                            : rect.y + rect.height + 6
                    referenceLinkPopup.open()
                }
            }
            RowLayout {
                spacing: 4
                Image { source: app.iconUrl("xqt-link"); sourceSize.width: 18; sourceSize.height: 18; Layout.leftMargin: 6 }
                Label {
                    visible: referenceLinkPopup.uri !== ""
                    text: referenceLinkPopup.uri
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: Math.max(80, referenceScope.width - 180)
                }
                Button {
                    objectName: "referenceLinkButton"
                    flat: true
                    text: referenceLinkPopup.uri !== "" ? qsTr("Open")
                          : referenceLinkPopup.page >= 0 ? qsTr("Go to page %1").arg(referenceLinkPopup.page + 1)
                                                         : qsTr("Page not in this document")
                    enabled: referenceLinkPopup.uri !== "" || referenceLinkPopup.page >= 0
                    onClicked: {
                        app.reference.followLink(referenceLinkPopup.uri, referenceLinkPopup.page)
                        referenceLinkPopup.close()
                    }
                }
            }
        }

        // The pill of the reference: small, at its bottom
        Pane {
            id: referencePill
            objectName: "referencePill"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            anchors.horizontalCenter: parent.horizontalCenter
            padding: 2
            leftPadding: 6
            rightPadding: 4
            Material.foreground: "#303030"
            background: Rectangle {
                radius: height / 2
                color: "#f2fafafa"
                border.width: 1
                border.color: "#40000000"
            }
            // A tap on the pill gives the reference the keys (copy, zoom, back and forth)
            function focusReference() { referenceCanvas.forceActiveFocus(Qt.MouseFocusReason) }
            RowLayout {
                spacing: 0
                ToolButton {
                    objectName: "referencePageButton"
                    text: app.reference.pageNumber + " / " + app.reference.pageCount
                    focusPolicy: Qt.NoFocus
                    implicitHeight: 40
                    onClicked: { referencePill.focusReference(); referencePagePopup.open() }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Go to page…")
                    ToolTip.delay: 600
                    Popup {
                        id: referencePagePopup
                        objectName: "referencePagePopup"
                        y: -height - 8
                        padding: 8
                        onOpened: { pageField.text = ""; pageField.forceActiveFocus() }
                        onClosed: referencePill.focusReference()
                        RowLayout {
                            Label { text: qsTr("Page") }
                            TextField {
                                id: pageField
                                objectName: "referencePageField"
                                implicitWidth: 72
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: Math.max(1, app.reference.pageCount) }
                                placeholderText: app.reference.pageNumber
                                onAccepted: {
                                    app.reference.goToPage(parseInt(text) - 1)
                                    referencePagePopup.close()
                                }
                            }
                            Label { text: "/ " + app.reference.pageCount; color: "#6b6f75" }
                        }
                    }
                }
                ToolSeparator {}
                IconButton {
                    objectName: "referenceCopyButton"
                    visible: app.reference.hasSelection
                    iconName: "xopp-edit-copy"
                    tip: qsTr("Copy (to paste into the notes)")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.copy() }
                }
                IconButton {
                    objectName: "referenceFitWidthButton"
                    iconName: "xqt-fit-width"
                    tip: qsTr("Fit the width")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.fitWidth() }
                }
                IconButton {
                    objectName: "referenceSwapSidesButton"
                    iconName: "xqt-swap-sides"
                    tip: qsTr("Swap sides")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.swapSides() }
                }
                IconButton {
                    objectName: "referenceSwapRolesButton"
                    iconName: "xqt-swap-roles"
                    tip: qsTr("Write in this document (the other one becomes the reference)")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: app.reference.swapRoles()
                }
                IconButton {
                    objectName: "referenceCloseButton"
                    iconName: "xqt-close"
                    tip: qsTr("Close the reference (its tab stays open)")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 20; icon.height: 20
                    focusPolicy: Qt.NoFocus
                    onClicked: app.reference.close()
                }
            }
        }
    }

    // The divider: dragged anywhere along it, or by its grip (touch sized) in the middle
    Item {
        id: divider
        objectName: "referenceDivider"
        visible: split.active
        readonly property real grab: 20  // the strip that takes a drag (a little into both canvases)
        x: (split.onLeft ? split.referenceWidth : split.mainWidth) + split.gap / 2 - grab / 2
        width: grab
        height: split.height
        z: 2
        Rectangle {
            id: grip
            objectName: "referenceDividerGrip"
            anchors.centerIn: parent
            width: 16
            height: 72
            radius: 8
            color: dividerDrag.active ? Material.accentColor : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            Column {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 3
                    Rectangle { width: 6; height: 2; radius: 1; color: dividerDrag.active ? "#ffffff" : "#5f6368" }
                }
            }
        }
        HoverHandler { cursorShape: Qt.SplitHCursor }
        DragHandler {
            id: dividerDrag
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            property real startX: 0
            onActiveChanged: {
                if (active) {
                    startX = divider.x + divider.width / 2
                    split.liveRatio = app.reference.ratio
                } else {
                    app.reference.ratio = split.liveRatio
                }
            }
            onTranslationChanged: {
                if (!active) return
                const at = startX + translation.x - split.gap / 2  // where the gap begins
                const main = split.onLeft ? split.width - split.gap - at : at
                split.liveRatio = Math.max(0.2, Math.min(0.8, main / Math.max(1, split.width - split.gap)))
            }
        }
    }

    // The frame of the main document: which side is written in
    Rectangle {
        objectName: "mainFrame"
        visible: split.active
        x: split.mainX
        width: split.mainWidth
        height: split.height
        color: "transparent"
        border.width: 2
        border.color: Material.accentColor
        opacity: 0.8
        property bool inputTransparent: true
    }
}
