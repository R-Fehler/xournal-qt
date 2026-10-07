// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

// Page and zoom status, floating over the canvas.
Pane {
    id: viewPill
    objectName: "viewPill"
    readonly property alias layoutMenu: layoutMenu
    readonly property alias fitMenu: fitMenu
    // also in full screen; presenting only the page number, for a moment (presentPageIndicator); not in Zen
    visible: !pageGrid.visible && !contentsOverview.visible && !app.presenting && !win.hudHidden && !win.phoneChrome
    /// The compact pill, in a canvas under 520 px wide (a phone, a half beside the reference or the source): undo,
    /// redo (while the tool bar is not shown: win.undoInToolBar), the page number (a tap: all pages), the contents
    /// and the zoom; the page layout is in ⋮ → View then
    readonly property bool compact: canvas.width < 520
    /// Narrower than the compact pill (a very small window): no redo (Ctrl+Y) and no separators
    readonly property bool tight: canvas.width < 360
    /// The page layout button: not in a phone's portrait nor in the compact pill (it is in ⋮ → View there)
    readonly property bool layoutShown: ["phonePortrait", "tiny"].indexOf(win.adaptive.layoutClass) < 0 && !compact
                                        && !win.phoneChrome
    // At the canvas's lower right corner, always inside the canvas (8 px from its edges where 28 is too much), and
    // above the reference's pill where the two would meet
    readonly property rect refPill: Qt.rect(referenceSplit.x + referenceSplit.pillRect.x,
                                            referenceSplit.y + referenceSplit.pillRect.y,
                                            referenceSplit.pillRect.width, referenceSplit.pillRect.height)
    x: Math.max(win.canvasControlsLeft + 8, win.canvasControlsRight - width
                - (win.canvasControlsRight - win.canvasControlsLeft - width >= 56 ? 28 : 8))
    /// (above the toolbox floating at the bottom edge where the two would meet: full screen, on a phone held
    /// upright too)
    readonly property real lowY: {
        const y = win.canvasControlsBottom - 24 - height
        const t = toolboxPane
        const meetsToolbox = win.toolboxFloating && t.edge === "bottom" && x < t.x + t.width && x + width > t.x
        return meetsToolbox ? Math.min(y, t.y - 12 - height) : y
    }
    readonly property bool meetsReference: refPill.width > 0 && x < refPill.x + refPill.width && x + width > refPill.x
                                           && lowY < refPill.y + refPill.height && lowY + height > refPill.y
    y: meetsReference ? refPill.y - height - 12 : lowY
    padding: 2
    leftPadding: 10
    rightPadding: 4
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f2fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    RowLayout {
        spacing: 0
        // Undo and redo while the tool bar is not shown (it leads with them otherwise: one place at a time)
        IconButton {
            objectName: "undoButton"
            visible: !win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar
            iconName: "xopp-edit-undo"
            label: qsTr("Undo")
            tip: win.withKeys(qsTr("Undo"), "undo")
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            enabled: app.canUndo
            onClicked: app.undo()
        }
        IconButton {
            objectName: "redoButton"
            visible: !win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar && !viewPill.tight
            iconName: "xopp-edit-redo"
            label: qsTr("Redo")
            tip: win.withKeys(qsTr("Redo"), "redo")
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            enabled: app.canRedo
            onClicked: app.redo()
        }
        ToolSeparator { visible: !win.undoInToolBar && !win.toolboxShown && !win.undoInFormatBar && !viewPill.tight }
        IconButton {
            objectName: "layoutButton"
            visible: viewPill.layoutShown
            iconName: app.pairedPages ? "xqt-book-open" : "xqt-page-single"
            label: qsTr("Page layout")
            tip: app.pairedPages ? qsTr("Two pages side by side - tap: one page (hold: the page layout)")
                                 : qsTr("One page - tap: two side by side (hold: the page layout)")
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            ownHold: true
            // A tap switches between one page and two side by side; the rest is in the menu (press and hold)
            onClicked: {
                if (app.pairedPages) {
                    app.pairedPages = false
                    app.viewColumns = 1
                } else {
                    app.viewColumns = 2
                    app.pairsOffset = 0
                    app.pairedPages = true
                }
            }
            onPressAndHold: Popups.openAt(layoutMenu)
            TapHandler {  // right click does what press and hold does
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { Popups.openAt(layoutMenu, point.position) }
            }
            AdaptiveMenu {
                id: layoutMenu
                objectName: "layoutMenu"
                title: qsTr("Page layout")
                titleShown: true
                // A text file (.md, .txt): A4 pages, or one continuous page that grows with the text
                AdaptiveMenuItem {
                    objectName: "textPagesItem"
                    offered: win.textDoc && app.textEditable
                    text: qsTr("Text on pages")
                    checkable: true
                    checked: !app.textContinuous
                    onTriggered: app.textContinuous = false
                }
                AdaptiveMenuItem {
                    objectName: "textContinuousItem"
                    offered: win.textDoc && app.textEditable
                    text: qsTr("Text on one continuous page")
                    checkable: true
                    checked: app.textContinuous
                    onTriggered: app.textContinuous = true
                }
                MenuSeparator { property bool offered: win.textDoc && app.textEditable; visible: offered; height: offered ? implicitHeight : 0 }
                AdaptiveMenuItem {
                    objectName: "onePageItem"
                    text: app.horizontalScrolling ? qsTr("Pages in one row") : qsTr("One page per row")
                    checkable: true
                    checked: !app.pairedPages && (app.horizontalScrolling ? app.viewRows === 1 : app.viewColumns === 1)
                    onTriggered: {
                        app.pairedPages = false
                        if (app.horizontalScrolling) app.viewRows = 1
                        else app.viewColumns = 1
                    }
                }
                AdaptiveMenuItem {
                    text: qsTr("Two pages side by side")
                    checkable: true
                    checked: app.pairedPages && app.pairsOffset === 0
                    onTriggered: { app.viewColumns = 2; app.pairsOffset = 0; app.pairedPages = true }
                }
                AdaptiveMenuItem {
                    text: qsTr("Book (cover page alone)")
                    checkable: true
                    checked: app.pairedPages && app.pairsOffset === 1
                    onTriggered: { app.viewColumns = 2; app.pairsOffset = 1; app.pairedPages = true }
                }
                MenuSeparator {}
                // N columns (scrolling sideways: N rows)
                RowLayout {
                    width: parent ? parent.width : implicitWidth
                    readonly property bool rows: app.horizontalScrolling
                    readonly property int count: rows ? app.viewRows : app.viewColumns
                    function setCount(n) {
                        if (rows) { app.viewRows = n; return }
                        app.pairedPages = false
                        app.viewColumns = n
                    }
                    Label { text: parent.rows ? qsTr("Rows") : qsTr("Columns"); Layout.leftMargin: 16; Layout.fillWidth: true }
                    ToolButton {
                        objectName: "fewerColumnsButton"
                        text: "−"; font.pixelSize: 20
                        enabled: parent.count > 1
                        onClicked: parent.setCount(parent.count - 1)
                    }
                    Label {
                        objectName: "columnsLabel"
                        text: parent.count
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        Layout.minimumWidth: 20
                    }
                    ToolButton {
                        objectName: "moreColumnsButton"
                        text: "+"; font.pixelSize: 20
                        enabled: parent.count < 8
                        onClicked: parent.setCount(parent.count + 1)
                    }
                }
                MenuSeparator {}
                AdaptiveMenuItem {
                    objectName: "sidewaysItem"
                    text: qsTr("Scroll sideways")
                    checkable: true
                    checked: app.horizontalScrolling
                    onTriggered: app.horizontalScrolling = !app.horizontalScrolling
                }
                AdaptiveMenuItem {
                    objectName: "snapPagesItem"
                    text: qsTr("Stop on whole pages")
                    enabled: app.horizontalScrolling || win.reading  // (up and down: while reading)
                    checkable: true
                    checked: app.snapPages
                    onTriggered: app.snapPages = !app.snapPages
                }
            }
        }
        ToolSeparator { visible: viewPill.layoutShown }
        IconButton {
            objectName: "pageGridButton"
            visible: !viewPill.compact  // (the compact pill: its page number opens them)
            iconName: "xqt-pages-grid"
            label: qsTr("All pages")
            tip: qsTr("All pages") + win.keyNote("pageGrid")
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            onClicked: pageGrid.open()
        }
        // The table of contents with the pages of each chapter (ContentsOverview)
        IconButton {
            objectName: "contentsButton"
            iconName: "xqt-toc"
            label: qsTr("Contents")
            tip: qsTr("Contents with the pages of each chapter") + win.keyNote("contents")
            implicitWidth: 40; implicitHeight: 40
            icon.width: 22; icon.height: 22
            checked: contentsOverview.visible
            onClicked: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open()
        }
        // Scrolling sideways: the previous and the next page on either side of the page number
        IconButton {
            objectName: "previousPageButton"
            visible: app.horizontalScrolling && !viewPill.compact  // (compact: a swipe turns the page)
            iconName: "xqt-chevron-left"
            tip: qsTr("Previous page (←, Page Up)")
            implicitWidth: 36; implicitHeight: 40
            icon.width: 20; icon.height: 20
            enabled: app.pageNumber > 1
            onClicked: app.previousPage()
        }
        Label {
            objectName: "pageNumberLabel"
            visible: !viewPill.compact
            text: app.pageNumber + " / " + app.pageCount
            color: "#505050"
            Layout.leftMargin: 2
            Layout.rightMargin: app.horizontalScrolling ? 0 : 4
        }
        // The compact pill: the page number opens all pages (the grid button's place)
        ToolButton {
            objectName: "pageNumberButton"
            visible: viewPill.compact
            text: app.pageNumber + " / " + app.pageCount
            font.pixelSize: 14
            implicitHeight: 40
            leftPadding: 8
            rightPadding: 8
            focusPolicy: Qt.NoFocus
            Material.foreground: "#505050"
            Accessible.name: qsTr("All pages")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("All pages") + win.keyNote("pageGrid")
            ToolTip.delay: 600
            onClicked: pageGrid.open()
        }
        // Only on a page with sticky notes: hide them all (to see what they cover) and show them again
        IconButton {
            objectName: "pageNotesButton"
            visible: app.pageHasNotes && !win.textDoc
            iconName: app.pageNotesHidden ? "xqt-eye-off" : "xqt-eye"
            tip: app.pageNotesHidden ? qsTr("Show the sticky notes of this page") : qsTr("Hide the sticky notes of this page")
            implicitWidth: 36; implicitHeight: 40
            icon.width: 20; icon.height: 20
            onClicked: app.pageNotesHidden = !app.pageNotesHidden
        }
        IconButton {
            objectName: "nextPageButton"
            visible: app.horizontalScrolling && !viewPill.compact
            iconName: "xqt-chevron-right"
            tip: qsTr("Next page (→, Page Down)")
            implicitWidth: 36; implicitHeight: 40
            icon.width: 20; icon.height: 20
            enabled: app.pageNumber < app.pageCount
            onClicked: app.nextPage()
        }
        // The canvas turned (qt/docs/canvas-rotation.md): by how much; a tap turns it upright again
        ToolButton {
            objectName: "rotationChip"
            visible: app.canvasRotation !== 0
            readonly property int degrees: Math.round(app.canvasRotation > 180 ? app.canvasRotation - 360
                                                                               : app.canvasRotation)
            text: "\u21ba " + degrees + "\u00b0"
            font.pixelSize: 13
            implicitHeight: 40
            leftPadding: 8
            rightPadding: 8
            focusPolicy: Qt.NoFocus
            Material.foreground: "#505050"
            Accessible.name: qsTr("Turn the canvas upright")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Turned by %1\u00b0: tap to turn it upright (two taps on the page or a fit do too)").arg(degrees)
            ToolTip.delay: 600
            onClicked: app.resetCanvasRotation()
        }
        ToolSeparator { visible: !viewPill.tight }
        // The zoom, small. A tap: the fits (after the double-tap time, so a double tap does not flash the menu);
        // a double tap or a long press: the whole page. Pinch, Ctrl+wheel, Ctrl+plus / minus / 0 and the middle
        // button zoom as before.
        ToolButton {
            id: zoomButton
            objectName: "zoomButton"
            text: app.zoomPercent + " %"
            font.pixelSize: 13
            implicitWidth: Math.max(48, implicitContentWidth + 16)
            implicitHeight: 40
            focusPolicy: Qt.NoFocus
            function fitWhole() {
                menuTimer.stop()
                app.fitPage()
            }
            Timer {
                id: menuTimer
                interval: Qt.styleHints.mouseDoubleClickInterval
                onTriggered: Popups.openAt(fitMenu)
            }
            TapHandler {
                id: zoomTaps
                objectName: "zoomTaps"
                acceptedButtons: Qt.LeftButton
                onTapped: function(point, button) {
                    if (tapCount >= 2) zoomButton.fitWhole()
                    else menuTimer.restart()
                }
                onLongPressed: zoomButton.fitWhole()
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                onTapped: function(point) { menuTimer.stop(); Popups.openAt(fitMenu, point.position) }
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Zoom: tap for the fits; double tap or hold: the whole page")
            ToolTip.delay: 600
            AdaptiveMenu {
                id: fitMenu
                objectName: "fitMenu"
                title: qsTr("Zoom")
                /// A fit chosen in the page grid of the phone chrome: back to the page, to see it
                function done() { if (pageGrid.visible && pageGrid.phoneTools) pageGrid.close() }
                AdaptiveMenuItem { objectName: "fitWidthItem"; text: qsTr("Fit the width") + win.keyNote("fitWidth"); icon.source: app.iconUrl("xqt-fit-width"); onTriggered: { app.fitWidth(); fitMenu.done() } }
                AdaptiveMenuItem {
                    objectName: "realSizeItem"
                    text: qsTr("Real size, 100 %") + win.keyNote("realSize")
                    onTriggered: { app.zoomToRealSize(); fitMenu.done() }
                }
                AdaptiveMenuItem { objectName: "fitHeightItem"; text: qsTr("Fit the height"); onTriggered: { app.fitHeight(); fitMenu.done() } }
                AdaptiveMenuItem {
                    objectName: "fitPageItem"
                    text: app.currentPageDiffers ? qsTr("Fit this page (its size differs)") : qsTr("Fit the whole page (double tap)")
                    onTriggered: { app.fitPage(); fitMenu.done() }
                }
            }
        }
    }
}
