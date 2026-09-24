// Reference mode: the canvas area of the window, split in two when the current tab shows another document beside
// its own (app.reference). The main canvas (Main.qml's `canvas`) takes mainX / mainWidth; the reference is a plain
// canvas for reading on the other side (or for writing too, with the edit switch of its pill), behind a divider that
// can be dragged, with a small pill of its own: the page (a tap: go to a page), edit, fit width, swap sides, swap
// roles, close. The main document has a thin frame, so it is always clear which side is the notes.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import XournalQt.Canvas

Item {
    id: split
    objectName: "referenceSplit"
    /// (the area itself takes no input: DocumentCanvasItem looks through it, to the main canvas under it)
    property bool inputTransparent: true
    // (presenting shows the notes alone; the reference comes back afterwards)
    readonly property bool active: app.reference.active && !app.homeVisible && !app.presenting
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
    /// "Go to page…" of the reference (its pill, its context pill)
    function openPagePopup() {
        referenceCanvas.forceActiveFocus(Qt.MouseFocusReason)
        referencePagePopup.open()
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
            // For reading, unless the edit switch of its pill is on (per tab)
            readingOnly: !app.reference.editing
            view: split.active ? app.reference.view : null
        }

        // The same scroll bars, knobs and pills as the notes have, for the reference (app.reference acts on it; for
        // reading only they offer copying, nothing that changes it)
        CanvasScrollBars {
            canvasItem: referenceCanvas
            namePrefix: "reference"
            hidden: referenceGrid.visible
        }
        PdfTextHandles {
            canvasItem: referenceCanvas
            target: app.reference
            namePrefix: "reference"
        }
        PdfTextPill {
            id: referencePdfTextPill
            objectName: "referencePdfTextBar"
            canvasItem: referenceCanvas
            target: app.reference
            namePrefix: "reference"
            hidden: referenceGrid.visible
        }
        SelectionPill {
            objectName: "referenceSelectionBar"
            canvasItem: referenceCanvas
            target: app.reference
            namePrefix: "reference"
            hidden: referenceGrid.visible
            anchors.bottomMargin: 84  // (above the reference's own pill)
        }
        ContextPill {
            id: referenceContextPill
            canvasItem: referenceCanvas
            target: app.reference
            namePrefix: "reference"
            onImageRequested: referenceImageDialog.open()
            onGoToPageRequested: split.openPagePopup()
        }
        FileDialog {
            id: referenceImageDialog
            title: qsTr("Insert image")
            nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"), qsTr("All files (*)")]
            onAccepted: app.reference.insertImage(selectedFile)
        }
        // A long press or right click on the reference: on PDF text the word is selected (its pill offers paste
        // too), elsewhere what can be done there - as on the notes
        Connections {
            target: app.reference
            function onContextRequested(viewPos) {
                if (app.reference.selectPdfTextAt(viewPos.x, viewPos.y)) {
                    referencePdfTextPill.offerPaste(viewPos)
                    return
                }
                referenceContextPill.openAt(viewPos, app.reference.pdfTextIsSelected)
            }
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

        // The pages of the reference, in its half (the page sidebar keeps showing the notes): a tap goes there
        Rectangle {
            id: referenceGrid
            objectName: "referenceGrid"
            anchors.fill: parent
            visible: app.reference.pagesShown && split.active
            color: "#eef0f3"
            onVisibleChanged: if (visible) Qt.callLater(function() {
                referenceGridView.positionViewAtIndex(Math.max(0, app.reference.pageNumber - 1), GridView.Center)
            })
            GridView {
                id: referenceGridView
                objectName: "referenceGridView"
                anchors.fill: parent
                anchors.margins: 8
                anchors.bottomMargin: 76  // (the pill)
                clip: true
                model: app.reference.pages
                readonly property int columns: Math.max(1, Math.round(width / 170))
                readonly property real aspect: Math.min(2, Math.max(0.4, app.reference.pages.typicalAspect))
                cellWidth: Math.floor(width / columns)
                cellHeight: Math.round((cellWidth - 16) * aspect) + 32
                boundsBehavior: Flickable.StopAtBounds
                delegate: Item {
                    id: refCell
                    objectName: "referenceGridPage"
                    required property int pageIndex
                    required property string thumbnail
                    required property string sketch
                    required property bool current
                    width: referenceGridView.cellWidth
                    height: referenceGridView.cellHeight
                    Rectangle {
                        id: refFrame
                        x: 8
                        y: 6
                        width: parent.width - 16
                        height: parent.height - 32
                        color: "#ffffff"
                        border.width: refCell.current ? 3 : 1
                        border.color: refCell.current ? Material.accentColor : "#c9ccd1"
                        PagePicture {
                            anchors.fill: parent
                            anchors.margins: refFrame.border.width
                            sketch: refCell.sketch
                            thumbnail: refCell.thumbnail
                            sourceWidth: Math.ceil(refFrame.width * Screen.devicePixelRatio / 128) * 128
                        }
                    }
                    Label {
                        anchors.top: refFrame.bottom
                        anchors.topMargin: 3
                        anchors.horizontalCenter: refFrame.horizontalCenter
                        text: refCell.pageIndex + 1
                        font.pixelSize: 12
                        font.weight: refCell.current ? Font.DemiBold : Font.Normal
                        color: "#5f6368"
                    }
                    TapHandler {
                        onTapped: {
                            app.reference.goToPage(refCell.pageIndex)
                            app.reference.pagesShown = false
                        }
                    }
                }
                TouchpadMomentum { flickable: referenceGridView }
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
                IconButton {
                    objectName: "referenceGridButton"
                    iconName: "xqt-pages-grid"
                    tip: qsTr("All pages of the reference")
                    checked: app.reference.pagesShown
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.pagesShown = !app.reference.pagesShown }
                }
                ToolSeparator {}
                // Write in the reference too (with the tool in hand; its own undo), or only read it
                IconButton {
                    objectName: "referenceEditButton"
                    iconName: "xopp-tool-pencil"
                    tip: app.reference.editing ? qsTr("Writing in the reference: tap for reading only")
                                               : qsTr("Write in the reference")
                    checked: app.reference.editing
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.editing = !app.reference.editing }
                }
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
