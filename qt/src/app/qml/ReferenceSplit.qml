// Reference mode: the canvas area of the window, split in two when the current tab shows another document beside
// its own (app.reference). The main canvas (Main.qml's `canvas`) takes mainX / mainY / mainWidth / mainHeight; the
// reference is a plain canvas for reading on the other side (or for writing too, with the edit switch of its pill),
// behind a divider that can be dragged, with a small pill of its own: the page (a tap: go to a page), edit, fit width,
// swap sides, swap roles, close. The main document has a thin frame, so it is always clear which side is the notes.
// Side by side where the canvas area is landscape, top and bottom where it is portrait (h > w: a tablet or a phone
// held upright); the divider keeps its ratio when that flips (qt/docs/reference-view.md). In a narrow half (< 480 px)
// the pill shows only the page and a ⋮ with the rest.
// The reference may be the tab's own document (qt/self-reference): a second view of it with a page and a zoom of its
// own.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import XournalQt.Canvas
import "Popups.js" as Popups
import "DevicePixels.js" as DevicePixels

Item {
    id: split
    objectName: "referenceSplit"
    /// (the area itself takes no input: DocumentCanvasItem looks through it, to the main canvas under it)
    property bool inputTransparent: true
    // (presenting shows the notes alone; the reference comes back afterwards)
    readonly property bool active: app.reference.active && !app.homeVisible && !app.presenting
    /// The reference first: at the left, or on top when the halves are stacked (app.reference.onLeft)
    readonly property bool onLeft: app.reference.onLeft
    /// Top and bottom: the area is portrait. It flips 16 px past square, so a window dragged across does not flicker;
    /// not while the divider is dragged.
    /// (a value, not a binding: orient() sets it as the size changes)
    property bool vertical: false
    /// Room at the split's bottom for the navigation bar and the soft keyboard (Main.qml): the reference's pill above it
    property real bottomInset: 0
    Component.onCompleted: orient()
    function orient() {
        if (dividerDrag.active) return
        if (height > width + 16) vertical = true
        else if (width > height + 16) vertical = false
    }
    onWidthChanged: orient()
    onHeightChanged: orient()
    /// The gap between the two canvases; the divider's handle is wider (touch)
    readonly property real gap: 8
    /// While the divider is dragged: its share of the main document (written to the setting when let go)
    property real liveRatio: app.reference.ratio
    readonly property real ratio: dividerDrag.active ? liveRatio : app.reference.ratio
    /// Along the split: the width side by side, the height stacked
    readonly property real span: vertical ? height : width
    readonly property real mainSize: active ? Math.round((span - gap) * ratio) : span
    readonly property real referenceSize: active ? span - gap - mainSize : 0
    readonly property real mainOffset: active && onLeft ? referenceSize + gap : 0
    readonly property real referenceOffset: onLeft ? 0 : mainSize + gap
    readonly property real mainX: vertical ? 0 : mainOffset
    readonly property real mainY: vertical ? mainOffset : 0
    readonly property real mainWidth: vertical ? width : mainSize
    readonly property real mainHeight: vertical ? mainSize : height
    readonly property real referenceX: vertical ? 0 : referenceOffset
    readonly property real referenceY: vertical ? referenceOffset : 0
    readonly property real referenceWidth: !active ? 0 : vertical ? width : referenceSize
    readonly property real referenceHeight: !active ? 0 : vertical ? referenceSize : height
    readonly property alias referenceCanvas: referenceCanvas
    /// Where the reference's pill is (in this item; empty without a reference): the main view pill keeps clear of it
    readonly property rect pillRect: active ? Qt.rect(referenceScope.x + referencePill.x, referenceScope.y + referencePill.y,
                                                      referencePill.width, referencePill.height)
                                            : Qt.rect(0, 0, 0, 0)

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
        readonly property real at: split.onLeft ? split.referenceSize : split.mainSize
        x: split.vertical ? 0 : at
        y: split.vertical ? at : 0
        width: split.vertical ? split.width : split.gap
        height: split.vertical ? split.gap : split.height
        color: "#4a4d51"
        property bool inputTransparent: true
    }

    FocusScope {
        id: referenceScope
        objectName: "referenceScope"
        x: split.referenceX
        y: split.referenceY
        width: split.referenceWidth
        height: split.referenceHeight
        visible: split.active

        DocumentCanvas {
            id: referenceCanvas
            objectName: "referenceCanvas"
            anchors.fill: parent
            clip: true
            focus: true
            // For reading, unless the edit switch of its pill is on (per tab)
            readingOnly: !app.reference.editing
            rotatable: false  // (only the notes turn: qt/docs/canvas-rotation.md)
            view: split.active ? app.reference.view : null
        }

        // The same scroll bars, knobs and pills as the notes have, for the reference (app.reference acts on it; for
        // reading only they offer copying, nothing that changes it)
        LinkStatusLine {
            canvasItem: referenceCanvas
            namePrefix: "reference"
        }
        CanvasScrollBars {
            canvasItem: referenceCanvas
            namePrefix: "reference"
            hidden: referenceGrid.visible
            // (above the navigation bar where the reference reaches the split's bottom)
            bottomInset: Math.max(0, split.bottomInset - (split.height - referenceScope.y - referenceScope.height))
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
            bottomMargin: 84  // (above the reference's own pill)
        }
        // The selected sticky note of the reference: the same pill as on the notes (for reading only: copy, deselect)
        NotePill {
            objectName: "referenceNotePill"
            canvasItem: referenceCanvas
            target: app.reference
            namePrefix: "reference"
            hidden: referenceGrid.visible
            onImageRequested: referenceImageDialog.open()
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
                        border.width: DevicePixels.whole(refCell.current ? 3 : 1, Screen.devicePixelRatio)
                        border.color: refCell.current ? Material.accentColor : "#c9ccd1"
                        PagePicture {
                            anchors.fill: parent
                            anchors.margins: refFrame.border.width
                            sketch: refCell.sketch
                            thumbnail: refCell.thumbnail
                            sourceWidth: Math.ceil(Math.ceil(refFrame.width * Screen.devicePixelRatio / 128) * 128 / Screen.devicePixelRatio)
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
            // (at the split's bottom: above the navigation bar; in the upper half of a split top and bottom, no room needed)
            anchors.bottomMargin: 24 + Math.max(0, split.bottomInset - (split.height - referenceScope.y - referenceScope.height))
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
            function toggleGrid() { focusReference(); app.reference.pagesShown = !app.reference.pagesShown }
            function toggleEditing() { focusReference(); app.reference.editing = !app.reference.editing }
            /// Its half is narrow (< 480 px: a phone, a phone held sideways, a small window): the page and a ⋮ with the
            /// rest. (The full pill is about 400 px; it keeps its buttons in a laptop's half beside the sidebar, 530 px.)
            readonly property bool narrow: referenceScope.width < 480
            readonly property string swapSidesText: split.vertical ? qsTr("Swap top and bottom") : qsTr("Swap sides")
            readonly property string swapRolesText: app.reference.self ? qsTr("Swap places (each side keeps its zoom)")
                                                                       : qsTr("Write in this document (the other one becomes the reference)")
            readonly property string popOutText: app.reference.self ? qsTr("Go there in the tab (the view beside closes)")
                                                                    : qsTr("Show as a tab")
            readonly property string closeText: app.reference.self ? qsTr("Close the view beside")
                                                                   : qsTr("Close the reference (its tab stays open)")
            RowLayout {
                spacing: 0
                ToolButton {
                    id: referencePageButton
                    objectName: "referencePageButton"
                    text: app.reference.pageNumber + " / " + app.reference.pageCount
                    focusPolicy: Qt.NoFocus
                    implicitHeight: 40
                    onClicked: { referencePill.focusReference(); referencePagePopup.open() }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Go to page…")
                    ToolTip.delay: 600
                    // Above the pill; in the phone classes a bottom sheet (the window's, above the soft keyboard)
                    Popup {
                        id: referencePagePopup
                        objectName: "referencePagePopup"
                        readonly property bool asSheet: typeof win !== "undefined" && win !== null && win.phoneLayout === true
                        parent: asSheet ? Overlay.overlay : referencePageButton
                        modal: asSheet
                        dim: asSheet
                        x: asSheet ? win.sheetX : 0
                        y: asSheet ? win.sheetBottom - height : -height - 8
                        width: asSheet ? win.sheetWidth : implicitWidth
                        padding: 8
                        leftPadding: asSheet ? 20 : 8
                        rightPadding: asSheet ? 20 : 8
                        topPadding: asSheet ? 16 : 8
                        bottomPadding: asSheet ? 16 + win.sheetBottomPadding : 8
                        background: Rectangle {
                            color: "#ffffff"
                            radius: referencePagePopup.asSheet ? 16 : 4
                            border.width: referencePagePopup.asSheet ? 0 : 1
                            border.color: "#d5d8dc"
                            Rectangle {  // (a sheet: square at the bottom)
                                visible: referencePagePopup.asSheet
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: parent.radius
                                color: parent.color
                            }
                        }
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
                // The rest: buttons in a wide half; in a narrow one (a phone, a small window) the ⋮ below holds them, so
                // the pill stays small
                IconButton {
                    objectName: "referenceGridButton"
                    visible: !referencePill.narrow
                    iconName: "xqt-pages-grid"
                    tip: qsTr("All pages of the reference")
                    checked: app.reference.pagesShown
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: referencePill.toggleGrid()
                }
                ToolSeparator { visible: !referencePill.narrow }
                // Write in the reference too (with the tool in hand; its own undo), or only read it
                IconButton {
                    objectName: "referenceEditButton"
                    visible: !referencePill.narrow
                    iconName: "xopp-tool-pencil"
                    tip: app.reference.editing ? qsTr("Writing in the reference: tap for reading only")
                                               : qsTr("Write in the reference")
                    checked: app.reference.editing
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: referencePill.toggleEditing()
                }
                IconButton {
                    objectName: "referenceCopyButton"
                    visible: app.reference.hasSelection && !referencePill.narrow
                    iconName: "xopp-edit-copy"
                    tip: qsTr("Copy (to paste into the notes)")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.copy() }
                }
                IconButton {
                    objectName: "referenceFitWidthButton"
                    visible: !referencePill.narrow
                    iconName: "xqt-fit-width"
                    tip: qsTr("Fit the width")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.fitWidth() }
                }
                IconButton {
                    objectName: "referenceSwapSidesButton"
                    visible: !referencePill.narrow
                    iconName: "xqt-swap-sides"
                    tip: referencePill.swapSidesText
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); app.reference.swapSides() }
                }
                IconButton {
                    objectName: "referenceSwapRolesButton"
                    visible: !referencePill.narrow
                    iconName: "xqt-swap-roles"
                    tip: referencePill.swapRolesText
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: app.reference.swapRoles()
                }
                // Its tab right after the notes' and shown alone: Ctrl+Tab / Ctrl+Shift+Tab go between the two
                IconButton {
                    objectName: "referencePopOutButton"
                    visible: !referencePill.narrow
                    iconName: "xqt-pop-out"
                    tip: referencePill.popOutText
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 22; icon.height: 22
                    focusPolicy: Qt.NoFocus
                    onClicked: app.reference.popOut()
                }
                IconButton {
                    objectName: "referenceCloseButton"
                    visible: !referencePill.narrow
                    iconName: "xqt-close"
                    tip: referencePill.closeText
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 20; icon.height: 20
                    focusPolicy: Qt.NoFocus
                    onClicked: app.reference.close()
                }
                // A narrow half: the page and this ⋮ only
                IconButton {
                    id: referenceMoreButton
                    objectName: "referenceMoreButton"
                    visible: referencePill.narrow
                    iconName: "xqt-more"
                    label: qsTr("The reference")
                    tip: qsTr("The reference: pages, writing, fit, sides, close")
                    implicitWidth: 40; implicitHeight: 40
                    icon.width: 20; icon.height: 20
                    focusPolicy: Qt.NoFocus
                    onClicked: { referencePill.focusReference(); Popups.openAt(referenceMenu) }
                    AdaptiveMenu {
                        id: referenceMenu
                        objectName: "referenceMenu"
                        title: app.reference.self ? qsTr("The view beside") : qsTr("The reference")
                        titleShown: true
                        AdaptiveMenuItem {
                            objectName: "referenceGridItem"
                            text: qsTr("All pages of the reference")
                            icon.source: app.iconUrl("xqt-pages-grid")
                            checkable: true
                            checked: app.reference.pagesShown
                            onTriggered: referencePill.toggleGrid()
                        }
                        AdaptiveMenuItem {
                            objectName: "referenceEditItem"
                            text: qsTr("Write in the reference")
                            icon.source: app.iconUrl("xopp-tool-pencil")
                            checkable: true
                            checked: app.reference.editing
                            onTriggered: referencePill.toggleEditing()
                        }
                        AdaptiveMenuItem {
                            objectName: "referenceCopyItem"
                            offered: app.reference.hasSelection
                            text: qsTr("Copy (to paste into the notes)")
                            icon.source: app.iconUrl("xopp-edit-copy")
                            onTriggered: app.reference.copy()
                        }
                        AdaptiveMenuItem {
                            objectName: "referenceFitWidthItem"
                            text: qsTr("Fit the width")
                            icon.source: app.iconUrl("xqt-fit-width")
                            onTriggered: app.reference.fitWidth()
                        }
                        AdaptiveMenuItem {
                            objectName: "referenceSwapSidesItem"
                            text: referencePill.swapSidesText
                            icon.source: app.iconUrl("xqt-swap-sides")
                            onTriggered: app.reference.swapSides()
                        }
                        AdaptiveMenuItem {
                            objectName: "referenceSwapRolesItem"
                            text: referencePill.swapRolesText
                            icon.source: app.iconUrl("xqt-swap-roles")
                            onTriggered: app.reference.swapRoles()
                        }
                        AdaptiveMenuItem {
                            objectName: "referencePopOutItem"
                            text: referencePill.popOutText
                            icon.source: app.iconUrl("xqt-pop-out")
                            onTriggered: app.reference.popOut()
                        }
                        AdaptiveMenuItem {
                            objectName: "referenceCloseItem"
                            text: referencePill.closeText
                            icon.source: app.iconUrl("xqt-close")
                            onTriggered: app.reference.close()
                        }
                    }
                }
            }
        }
    }

    // The divider: dragged anywhere along it, or by its grip (touch sized) in the middle. Across the area when the
    // halves are stacked.
    Item {
        id: divider
        objectName: "referenceDivider"
        visible: split.active
        readonly property real grab: 20  // the strip that takes a drag (a little into both canvases)
        readonly property real at: (split.onLeft ? split.referenceSize : split.mainSize) + split.gap / 2 - grab / 2
        x: split.vertical ? 0 : at
        y: split.vertical ? at : 0
        width: split.vertical ? split.width : grab
        height: split.vertical ? grab : split.height
        z: 2
        Rectangle {
            id: grip
            objectName: "referenceDividerGrip"
            anchors.centerIn: parent
            width: split.vertical ? 72 : 16
            height: split.vertical ? 16 : 72
            radius: 8
            color: dividerDrag.active ? Material.accentColor : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            Grid {
                anchors.centerIn: parent
                spacing: 4
                columns: split.vertical ? 3 : 1
                Repeater {
                    model: 3
                    Rectangle {
                        width: split.vertical ? 2 : 6
                        height: split.vertical ? 6 : 2
                        radius: 1
                        color: dividerDrag.active ? "#ffffff" : "#5f6368"
                    }
                }
            }
        }
        HoverHandler { cursorShape: split.vertical ? Qt.SplitVCursor : Qt.SplitHCursor }
        DragHandler {
            id: dividerDrag
            target: null
            xAxis.enabled: !split.vertical
            yAxis.enabled: split.vertical
            property real start: 0
            onActiveChanged: {
                if (active) {
                    start = divider.at + divider.grab / 2
                    split.liveRatio = app.reference.ratio
                } else {
                    app.reference.ratio = split.liveRatio
                    split.orient()
                }
            }
            onTranslationChanged: {
                if (!active) return
                const at = start + (split.vertical ? translation.y : translation.x) - split.gap / 2  // where the gap begins
                const main = split.onLeft ? split.span - split.gap - at : at
                split.liveRatio = Math.max(0.2, Math.min(0.8, main / Math.max(1, split.span - split.gap)))
            }
        }
    }

    // The frame of the main document: which side is written in
    Rectangle {
        objectName: "mainFrame"
        visible: split.active
        x: split.mainX
        y: split.mainY
        width: split.mainWidth
        height: split.mainHeight
        color: "transparent"
        border.width: 2
        border.color: Material.accentColor
        opacity: 0.8
        property bool inputTransparent: true
    }
}
