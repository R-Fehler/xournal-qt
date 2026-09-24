// xournal-qt: main window (first usable version: one document, pen / highlighter / eraser / hand).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import XournalQt
import XournalQt.Canvas
import "Popups.js" as Popups

ApplicationWindow {
    id: win
    width: 1280
    height: 900
    // Maximized when the app starts (people make it smaller with the tiling of their desktop); the size above is
    // what it gets when it is not maximized, and what the tests use
    visibility: app.startMaximized ? Window.Maximized : Window.Windowed
    title: app.homeVisible ? (app.library.available ? app.library.name + " — Xournal Qt" : "Xournal Qt")
                           : (app.modified ? "• " : "") + app.title + " — Xournal Qt"
    Material.theme: Material.Light
    Material.accent: Material.Indigo
    color: "#5f6368"

    property var afterDiscardCheck: null
    property bool sidebarShown: width >= 900
    property bool quitting: false
    readonly property string toolbarPosition: app.toolbarPosition
    readonly property bool sideToolbar: toolbarPosition === "left" || toolbarPosition === "right"
    /// Full screen: no tab strip, tool bar or page sidebar; a small square shows the current tool, a tap on it offers
    /// the tools (the same ones) and colors. The page / zoom pill stays.
    property bool fullScreenMode: false
    /// The window's state outside full screen (maximized or not), followed all the time rather than read when full
    /// screen starts: the platform may report the state late or in steps, and leaving full screen goes back to it
    property int windowedVisibility: app.startMaximized ? Window.Maximized : Window.Windowed
    property bool leavingFullScreen: false
    property bool remaximized: false
    onVisibilityChanged: {
        if (leavingFullScreen) {
            // The way back may pass through other states (the timer ends this). A compositor may also give back the
            // size from before it was maximized when full screen ends, even after it reported maximized: then ask
            // for maximized once more.
            if (visibility === Window.Windowed && windowedVisibility === Window.Maximized && !remaximized) {
                remaximized = true
                showMaximized()
            }
        } else if (!fullScreenMode && (visibility === Window.Windowed || visibility === Window.Maximized)) {
            windowedVisibility = visibility
        }
    }
    Timer {  // ends the way back from full screen (a compositor may take a moment and several steps)
        id: leavingFullScreenTimer
        interval: 1500
        onTriggered: {
            win.leavingFullScreen = false
            // settled: the state it is in now is the window's state
            if (!win.fullScreenMode && (win.visibility === Window.Windowed || win.visibility === Window.Maximized))
                win.windowedVisibility = win.visibility
        }
    }
    onFullScreenModeChanged: {
        if (fullScreenMode) {
            leavingFullScreenTimer.stop()
            leavingFullScreen = false
            showFullScreen()
        } else {
            quickTools.close()
            leavingFullScreen = true
            remaximized = false
            leavingFullScreenTimer.restart()
            if (windowedVisibility === Window.Maximized) showMaximized()
            else showNormal()
        }
    }
    /// No tool bar: in full screen, or when it was put away - the small tool square takes over
    readonly property bool noToolbar: fullScreenMode || app.toolbarHidden
    readonly property bool verticalTools: sideToolbar || noToolbar
    readonly property int toolColumns: noToolbar ? 6 : 2
    Connections {
        target: app
        function onHomeVisibleChanged() { if (app.homeVisible) win.fullScreenMode = false }
    }

    function withSavedChanges(action) {
        if (!app.modified) {
            action()
            return
        }
        afterDiscardCheck = action
        unsavedDialog.open()
    }
    function openSaveDialog(then) {
        // Upstream Xournal++ suggestion: next to the annotated PDF ("lecture.pdf" -> "lecture.xopp"), else the
        // document's own path, else the default name in the last used folder.
        const suggestion = app.suggestedSaveFile().toString()
        saveDialog.afterSave = then
        if (suggestion !== "") {
            saveDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            saveDialog.selectedFile = suggestion
        }
        saveDialog.open()
    }
    function openExportDialog() {
        const suggestion = app.suggestedExportFile().toString()
        if (suggestion !== "") {
            exportDialog.currentFolder = suggestion.substring(0, suggestion.lastIndexOf("/"))
            exportDialog.selectedFile = suggestion
        }
        exportDialog.open()
    }
    function saveOrAsk(then) {
        if (app.hasFilePath) {
            if (app.save() && then) then()
        } else {
            openSaveDialog(then)
        }
    }

    // Close a tab; unsaved changes are asked about first (with that tab shown).
    function requestCloseTab(index) {
        if (!app.tabModified(index)) {
            app.closeTab(index)
            return
        }
        app.currentTab = index
        withSavedChanges(function() { app.closeTab(app.currentTab) })
    }
    // Close every document; unsaved changes are asked about one by one.
    function closeAllTabs() {
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            app.closeAllTabs()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeAllTabs() })
    }
    // Quitting: go through the tabs with unsaved changes one by one.
    function closeWindow() {
        const pending = app.modifiedTabs()
        if (pending.length === 0) {
            quitting = true
            win.close()
            return
        }
        app.currentTab = pending[0]
        withSavedChanges(function() { app.closeTab(app.currentTab); closeWindow() })
    }

    onClosing: function(close) {
        if (!quitting && app.modifiedTabs().length > 0) {
            close.accepted = false
            closeWindow()
            return
        }
        if (app.secondaryWindow) {
            app.windowClosed()  // its documents (unsaved ones go back to the main window)
        }
    }

    Connections {
        target: app
        function onCloseWindowRequested() {  // its last document moved to another window
            win.quitting = true
            win.close()
        }
        function onRaiseRequested() {
            if (win.visibility === Window.Minimized) win.showNormal()
            win.raise()
            win.requestActivate()
        }
    }

    header: Column {
      TabStrip {
        id: tabStrip
        width: parent.width
        visible: !win.fullScreenMode
        onCloseRequested: function(index) { requestCloseTab(index) }
        onOverviewRequested: tabOverview.open()
        onUndockRequested: function(index) { app.undockTab(index) }
        onDockRequested: function(index) { app.dockTab(index) }
      }
      ToolBar {
        id: topTools
        width: parent.width
        visible: !app.homeVisible && !win.sideToolbar && !win.noToolbar
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: 56
      }
    }

    // The tools: in the header (top), or a column at the left or right side (setting). One set of tools, moved.
    Rectangle {
        id: sideTools
        objectName: "sideTools"
        visible: !app.homeVisible && win.sideToolbar && !win.noToolbar
        width: visible ? 104 : 0
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: win.toolbarPosition === "right" ? parent.width - width : 0
        color: "#ffffff"
        Rectangle {  // the line towards the pages
            width: 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: win.toolbarPosition === "right" ? 0 : parent.width - 1
            color: "#d5d8dc"
        }
    }
    Item {
        id: toolArea
        parent: win.noToolbar ? quickToolsHolder : (win.sideToolbar ? sideTools : topTools)
        anchors.fill: parent
        anchors.margins: win.verticalTools ? 4 : 0
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        Material.foreground: "#303030"
        // Scrolls when the window is too small for all tools (sideways on top, up and down at a side).
        Flickable {
            id: toolFlick
            anchors.fill: parent
            contentWidth: toolRow.width
            contentHeight: toolRow.height
            flickableDirection: win.verticalTools ? Flickable.VerticalFlick : Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: win.verticalTools ? contentHeight > height : contentWidth > width
            clip: true
        GridLayout {
            id: toolRow
            objectName: "toolRow"
            rows: win.verticalTools ? -1 : 1
            columns: win.verticalTools ? win.toolColumns : -1
            height: win.verticalTools ? Math.max(toolFlick.height, implicitHeight) : toolFlick.height
            width: win.verticalTools ? toolFlick.width : Math.max(toolFlick.width, implicitWidth)
            rowSpacing: 2
            columnSpacing: 2

            IconButton { iconName: "xopp-sidebar-page-preview"; tip: qsTr("Pages"); checked: sidebarShown; onClicked: sidebarShown = !sidebarShown }
            IconButton {
                objectName: "contentsButton"
                iconName: "xqt-toc"
                tip: qsTr("Contents with the pages of each chapter (Ctrl+Alt+O)")
                checked: contentsOverview.visible
                onClicked: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open()
            }
            ToolSeparator { orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            IconButton { iconName: "xopp-document-new"; tip: qsTr("New document (new tab)"); onClicked: app.newDocument() }
            IconButton { iconName: "xopp-document-open"; tip: qsTr("Open (in a new tab)"); onClicked: openDialog.open() }
            IconButton { iconName: "xopp-document-save"; tip: qsTr("Save"); onClicked: saveOrAsk(null) }
            ToolSeparator { orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            IconButton { iconName: "xopp-tool-pencil"; tip: qsTr("Pen"); checked: app.tool === "pen"; onClicked: app.selectTool("pen") }
            IconButton { iconName: "xopp-tool-highlighter"; tip: qsTr("Highlighter"); checked: app.tool === "highlighter"; onClicked: app.selectTool("highlighter") }
            // The eraser: a tap takes it; tapped again, held or right-clicked, it offers how it erases
            IconButton {
                objectName: "eraserButton"
                iconName: "xopp-tool-eraser"
                tip: qsTr("Eraser (tap again or hold: how it erases)")
                checked: app.tool === "eraser"
                onClicked: checked ? Popups.openAt(eraserMenu) : app.selectTool("eraser")
                onPressAndHold: Popups.openAt(eraserMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(eraserMenu, point.position) }
                }
                Menu {
                    id: eraserMenu
                    objectName: "eraserMenu"
                    component EraserItem: MenuItem {
                        property string mode
                        checkable: true
                        checked: (app.settings.revision, app.settings.get("eraserMode")) === mode
                        onTriggered: {
                            app.settings.set("eraserMode", mode)
                            app.selectTool("eraser")
                        }
                    }
                    EraserItem { objectName: "eraserStandard"; text: qsTr("Standard (cuts strokes)"); mode: "default" }
                    EraserItem { objectName: "eraserWholeStrokes"; text: qsTr("Whole strokes"); mode: "deleteStroke" }
                    EraserItem { objectName: "eraserWhiteout"; text: qsTr("Whiteout (paints white)"); mode: "whiteout" }
                }
            }
            IconButton { iconName: "xopp-hand"; tip: qsTr("Hand"); checked: app.tool === "hand"; onClicked: app.selectTool("hand") }
            IconButton {
                objectName: "textButton"
                iconName: "xopp-tool-text"
                tip: app.textMarkdown ? qsTr("Markdown text (tap to write; tap a text to edit it; hold for the font)")
                                      : qsTr("Text (tap to write; tap a text to edit it)")
                checked: app.tool === "text"
                onClicked: app.tool === "text" ? fontPopup.open() : app.selectTool("text")
                onPressAndHold: fontPopup.open()
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: fontPopup.open()
                }
                Popup {
                    id: fontPopup
                    x: win.toolbarPosition === "left" ? parent.width : win.toolbarPosition === "right" ? -width : 0
                    y: win.verticalTools ? 0 : parent.height
                    padding: 12
                    ColumnLayout {
                        spacing: 8
                        Label { text: qsTr("Font"); font.weight: Font.DemiBold }
                        ComboBox {
                            id: familyBox
                            Layout.preferredWidth: 260
                            model: fontPopup.opened ? app.fontFamilies() : []
                            currentIndex: model.indexOf(app.fontFamily)
                            onActivated: app.fontFamily = currentText
                        }
                        RowLayout {
                            Label { text: app.textMarkdown ? qsTr("Size of Markdown text") : qsTr("Size"); Layout.fillWidth: true }
                            SpinBox {
                                objectName: "fontSizeBox"
                                from: 4; to: 200
                                value: Math.round(app.textMarkdown ? app.markdownFontSize : app.fontSize)
                                editable: true
                                onValueModified: app.textMarkdown ? app.markdownFontSize = value : app.fontSize = value
                            }
                        }
                        // New text boxes are Markdown: shown formatted
                        Switch {
                            objectName: "textMarkdownSwitch"
                            text: qsTr("Markdown (shown formatted)")
                            checked: app.textMarkdown
                            onToggled: app.textMarkdown = checked
                        }
                        // Markdown: written on the page (formatted while typing, the block with the cursor showing its
                        // Markdown), or its source beside the page
                        Switch {
                            objectName: "markdownInPanelSwitch"
                            text: qsTr("Write Markdown beside the page (its source)")
                            checked: app.markdownInPanel
                            onToggled: app.markdownInPanel = checked
                        }
                    }
                }
            }
            IconButton {
                objectName: "pdfTextButton"
                readonly property var icons: ({ "highlight": "xopp-select-pdf-text-ht", "underline": "xqt-underline",
                                                "strikethrough": "xqt-strikethrough", "select": "xopp-select-pdf-text-area" })
                iconName: icons[app.pdfTextMode] || "xopp-select-pdf-text-ht"
                tip: qsTr("Mark PDF text (drag over the text)")
                checked: app.tool === "selectPdfTextLinear" || app.tool === "selectPdfTextRect"
                onClicked: checked ? Popups.openAt(pdfTextMenu) : app.selectTool("selectPdfTextLinear")
                onPressAndHold: Popups.openAt(pdfTextMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(pdfTextMenu, point.position) }
                }
                Menu {
                    id: pdfTextMenu
                    objectName: "pdfTextMenu"
                    width: 320
                    component ModeItem: MenuItem {
                        property string mode
                        checkable: true
                        checked: app.pdfTextMode === mode
                        onTriggered: {
                            app.pdfTextMode = mode
                            if (app.tool !== "selectPdfTextRect") app.selectTool("selectPdfTextLinear")
                        }
                    }
                    ModeItem { text: qsTr("Highlight"); mode: "highlight" }
                    // The highlight color: three presets
                    RowLayout {
                        objectName: "highlightColors"
                        width: parent ? parent.width : implicitWidth
                        Label { text: qsTr("Highlight color"); Layout.leftMargin: 16; Layout.fillWidth: true; color: "#5f6368" }
                        HighlightColors { Layout.rightMargin: 8 }
                    }
                    ModeItem { text: qsTr("Underline"); mode: "underline" }
                    ModeItem { text: qsTr("Strike through"); mode: "strikethrough" }
                    ModeItem { text: qsTr("Select (then copy or mark)"); mode: "select" }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Select by area (columns, tables)")
                        checkable: true
                        checked: app.tool === "selectPdfTextRect"
                        onTriggered: app.selectTool(checked ? "selectPdfTextRect" : "selectPdfTextLinear")
                    }
                }
            }
            // Writing on the page with the keyboard: the text mode or Markdown (the one used last; hold for both)
            IconButton {
                id: writeButton
                objectName: "textModeButton"
                property bool markdownMode: false
                iconName: markdownMode ? "xqt-markdown" : "xqt-text-mode"
                tip: markdownMode ? qsTr("Markdown: write Markdown on the page, shown formatted (Ctrl+Alt+M). Hold for the text mode")
                                  : qsTr("Text mode: type the page's text like in a word processor (Ctrl+Alt+E). Hold for Markdown")
                checked: textFlowPanel.visible || markdownPanel.visible
                onClicked: {
                    if (textFlowPanel.visible) textFlowPanel.close(true)
                    else if (markdownPanel.visible) markdownPanel.close(true)
                    else if (markdownMode) markdownPanel.open()
                    else textFlowPanel.open()
                }
                onPressAndHold: Popups.openAt(writeMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(writeMenu, point.position) }
                }
                Menu {
                    id: writeMenu
                    objectName: "writeModeMenu"
                    width: 340
                    MenuItem {
                        objectName: "textModeItem"
                        text: qsTr("Text mode (like a word processor)")
                        checkable: true
                        checked: !writeButton.markdownMode
                        onTriggered: textFlowPanel.open()
                    }
                    MenuItem {
                        objectName: "markdownItem"
                        text: qsTr("Markdown (shown formatted)")
                        checkable: true
                        checked: writeButton.markdownMode
                        onTriggered: markdownPanel.open()
                    }
                }
                Connections {
                    target: textFlowPanel
                    function onVisibleChanged() { if (textFlowPanel.visible) writeButton.markdownMode = false }
                }
                Connections {
                    target: markdownPanel
                    function onVisibleChanged() { if (markdownPanel.visible) writeButton.markdownMode = true }
                }
            }
            IconButton { objectName: "imageButton"; iconName: "xopp-tool-image"; tip: qsTr("Insert image"); onClicked: imageDialog.open() }
            IconButton { objectName: "selectRectButton"; iconName: "xopp-select-rect"; tip: qsTr("Select (rectangle)"); checked: app.tool === "selectRect"; onClicked: app.selectTool("selectRect") }
            IconButton { objectName: "lassoButton"; iconName: "xopp-select-lasso"; tip: qsTr("Select (lasso)"); checked: app.tool === "selectRegion"; onClicked: app.selectTool("selectRegion") }
            IconButton {
                objectName: "shapeButton"
                readonly property var icons: ({
                    "line": "xopp-draw-line", "rectangle": "xopp-draw-rect", "ellipse": "xopp-draw-ellipse",
                    "arrow": "xopp-draw-arrow", "doubleArrow": "xopp-draw-double-arrow",
                    "drawCoordinateSystem": "xopp-draw-coordinate-system", "strokeRecognizer": "xopp-shape-recognizer"
                })
                iconName: icons[app.drawingType] || "xqt-shapes"
                tip: qsTr("Shapes")
                checked: (app.tool === "pen" || app.tool === "highlighter") && app.drawingType !== "default"
                onClicked: Popups.openAt(shapeMenu)
                Menu {
                    id: shapeMenu
                    component ShapeItem: MenuItem {
                        property string type
                        checkable: true
                        checked: app.drawingType === type
                        onTriggered: app.drawingType = type
                    }
                    ShapeItem { text: qsTr("Freehand"); type: "default" }
                    ShapeItem { text: qsTr("Recognize shapes (draw, then it straightens)"); type: "strokeRecognizer" }
                    MenuSeparator {}
                    ShapeItem { text: qsTr("Line"); type: "line" }
                    ShapeItem { text: qsTr("Rectangle"); type: "rectangle" }
                    ShapeItem { text: qsTr("Ellipse"); type: "ellipse" }
                    ShapeItem { text: qsTr("Arrow"); type: "arrow" }
                    ShapeItem { text: qsTr("Double arrow"); type: "doubleArrow" }
                    ShapeItem { text: qsTr("Coordinate system"); type: "drawCoordinateSystem" }
                    MenuSeparator {}
                    // On the page itself, not a way of drawing: drag it where it is needed, draw along its edge
                    MenuItem {
                        objectName: "setsquareItem"
                        text: qsTr("Setsquare (draw along its edge)")
                        checkable: true
                        checked: app.geometryTool === "setsquare"
                        onTriggered: app.toggleSetsquare()
                    }
                    MenuItem {
                        objectName: "compassItem"
                        text: qsTr("Compass (draw circles around it)")
                        checkable: true
                        checked: app.geometryTool === "compass"
                        onTriggered: app.toggleCompass()
                    }
                    MenuSeparator {}
                    // Corners of shapes and moved selections jump onto the half-centimetre grid (upstream's tool bar
                    // toggle; also in the settings)
                    MenuItem {
                        objectName: "snapGridItem"
                        text: qsTr("Snap to the grid")
                        checkable: true
                        checked: (app.settings.revision, app.settings.get("snapGrid"))
                        onTriggered: app.settings.set("snapGrid", checked)
                    }
                }
            }
            ToolSeparator { orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            // The preset colors: tap to use; press and hold / right click to remove; + adds one.
            Repeater {
                model: app.toolbarColors
                delegate: AbstractButton {
                    id: swatch
                    required property color modelData
                    required property int index
                    objectName: "colorSwatch"
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setColor(modelData)
                    onPressAndHold: Popups.openAt(swatchMenu)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                        onTapped: function(point) { Popups.openAt(swatchMenu, point.position) }
                    }
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: swatch.modelData
                            border.width: Qt.colorEqual(app.color, swatch.modelData) ? 3 : 1
                            border.color: Qt.colorEqual(app.color, swatch.modelData) ? Material.accentColor : "#9e9e9e"
                        }
                    }
                    Menu {
                        id: swatchMenu
                        MenuItem { text: qsTr("Remove from the tool bar"); onTriggered: app.removeToolbarColor(swatch.index) }
                        MenuItem { text: qsTr("Add a color…"); onTriggered: colorDialog.open() }
                        MenuItem { text: qsTr("Default colors"); onTriggered: app.resetToolbarColors() }
                    }
                }
            }
            AbstractButton {
                objectName: "addColorButton"
                implicitWidth: 40
                implicitHeight: 44
                onClicked: colorDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add a color (press and hold a color to remove it)")
                ToolTip.delay: 600
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 28; height: 28; radius: 14
                        color: "transparent"
                        border.width: 1
                        border.color: "#9e9e9e"
                        Label { anchors.centerIn: parent; text: "+"; font.pixelSize: 18; color: "#5f6368" }
                    }
                }
            }
            ToolSeparator { orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            Repeater {
                model: [ { size: 1, dot: 6 }, { size: 2, dot: 10 }, { size: 3, dot: 15 }, { size: 4, dot: 21 } ]
                delegate: AbstractButton {
                    required property var modelData
                    objectName: "sizeButton" + modelData.size
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setSize(modelData.size)
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 32; height: 32; radius: 6
                            color: app.size === modelData.size ? "#e0e3f5" : "transparent"
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: modelData.dot; height: modelData.dot; radius: modelData.dot / 2
                            color: "#303030"
                        }
                    }
                }
            }
            // The fifth width: the tool's own, adjustable (tap it again or press and hold)
            AbstractButton {
                id: customSizeButton
                objectName: "customSizeButton"
                implicitWidth: 40
                implicitHeight: 44
                enabled: app.customWidth > 0
                opacity: enabled ? 1 : 0.4
                // As big as the width compared to the very thick size
                readonly property real dot: {
                    const ref = app.tool, thick = app.sizeWidth(4)
                    return thick > 0 ? Math.max(3, Math.min(26, Math.sqrt(app.customWidth / thick) * 21)) : 12
                }
                onClicked: app.size === 5 ? customSizePopup.open() : app.setSize(5)
                onPressAndHold: { app.setSize(5); customSizePopup.open() }
                ToolTip.visible: hovered && !customSizePopup.visible
                ToolTip.text: qsTr("Own width (%1 mm): tap again or press and hold to change it").arg(customSizePopup.mmText)
                ToolTip.delay: 600
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 32; height: 32; radius: 6
                        color: app.size === 5 ? "#e0e3f5" : "transparent"
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: customSizeButton.dot; height: width; radius: width / 2
                        color: "transparent"
                        border.width: Math.min(width / 2, 2.5)
                        border.color: "#303030"
                    }
                }
                CustomWidthPopup {
                    id: customSizePopup
                    side: win.fullScreenMode ? "left" : win.toolbarPosition
                }
            }
            Item { Layout.fillWidth: !win.verticalTools; Layout.fillHeight: win.verticalTools; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1 }
            IconButton {
                objectName: "addPageButton"
                iconName: "xopp-page-add"
                tip: qsTr("Add a page after the current one (press and hold: background, size, several pages)")
                onClicked: app.addPageAfterCurrent()
                onPressAndHold: insertPagesDialog.openAt(app.pageNumber)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: insertPagesDialog.openAt(app.pageNumber)
                }
            }
            ToolSeparator { orientation: win.verticalTools ? Qt.Horizontal : Qt.Vertical; Layout.columnSpan: win.verticalTools ? win.toolColumns : 1; Layout.fillWidth: win.verticalTools }
            IconButton { objectName: "searchButton"; iconName: "xqt-search"; tip: qsTr("Search (Ctrl+F)"); checked: searchBar.visible; onClicked: searchBar.visible ? searchBar.closeBar() : searchBar.openBar() }
            // Full screen (F11). Not inside full screen itself: the tools there end with "Leave full screen"
            IconButton {
                objectName: "fullScreenButton"
                visible: !win.fullScreenMode
                iconName: "xopp-fullscreen"
                tip: qsTr("Full screen (F11)")
                onClicked: win.fullScreenMode = true
            }
            IconButton { objectName: "settingsButton"; iconName: "xqt-settings"; tip: qsTr("Settings (Ctrl+,)"); onClicked: settingsPage.open() }
            IconButton {
                objectName: "moreButton"
                iconName: "xqt-more"
                tip: qsTr("More")
                onClicked: Popups.openAt(moreMenu)
                Menu {
                    id: moreMenu
                    MenuItem { text: qsTr("Save as…"); onTriggered: openSaveDialog(null) }
                    MenuItem { text: qsTr("Export as PDF…"); onTriggered: openExportDialog() }
                    MenuItem { objectName: "printItem"; text: qsTr("Print… (Ctrl+P)"); onTriggered: printDialog.open() }
                    MenuItem { text: qsTr("Start a chapter here…"); onTriggered: chapterDialog.openFor(app.pageNumber - 1) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Insert image…"); onTriggered: imageDialog.open() }
                    MenuItem { text: qsTr("Insert pages…"); onTriggered: insertPagesDialog.openAt(app.pageNumber) }
                    MenuItem { text: qsTr("Background of this page…"); onTriggered: backgroundDialog.openFor([app.pageNumber - 1]) }
                    MenuItem { text: qsTr("All pages"); onTriggered: pageGrid.open() }
                    MenuItem { text: qsTr("All open documents"); onTriggered: tabOverview.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Settings"); onTriggered: settingsPage.open() }
                    MenuItem {
                        objectName: "hideToolbarItem"
                        text: app.toolbarHidden ? qsTr("Show the tool bar") : qsTr("Hide the tool bar")
                        onTriggered: app.toolbarHidden = !app.toolbarHidden
                    }
                    MenuItem {
                        objectName: "fullScreenItem"
                        text: win.fullScreenMode ? qsTr("Leave full screen (F11)") : qsTr("Full screen (F11)")
                        onTriggered: win.fullScreenMode = !win.fullScreenMode
                    }
                    Menu {
                        title: qsTr("Tool bar position")
                        component PositionItem: MenuItem {
                            property string position
                            checkable: true
                            checked: app.toolbarPosition === position
                            onTriggered: app.toolbarPosition = position
                        }
                        PositionItem { text: qsTr("Top"); position: "top" }
                        PositionItem { text: qsTr("Left"); position: "left" }
                        PositionItem { text: qsTr("Right"); position: "right" }
                    }
                }
            }
        }
        }
    }

    PageSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: win.toolbarPosition === "left" ? sideTools.right : parent.left
        width: 210
        visible: sidebarShown && !win.fullScreenMode
    }

    DocumentCanvas {
        id: canvas
        objectName: "canvas"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: textFlowPanel.visible ? textFlowPanel.left
                       : markdownPanel.visible ? markdownPanel.left
                       : (win.toolbarPosition === "right" ? sideTools.left : parent.right)
        anchors.left: sidebar.visible ? sidebar.right : (win.toolbarPosition === "left" ? sideTools.right : parent.left)
        clip: true  // zoomed-in pages must not paint over the sidebar
        view: app.view
    }

    // A Markdown file shown read-only for now, an image to write on: what that means (closed for this tab with ×).
    Pane {
        id: shownFileNote
        objectName: "shownFileNote"
        property string closedFor: ""
        visible: app.shownFileNote !== "" && closedFor !== app.title && !pageGrid.visible && !contentsOverview.visible
        // (bottom left: the search bar is at the top, the page and zoom pill at the bottom right)
        anchors.bottom: canvas.bottom
        anchors.left: canvas.left
        anchors.bottomMargin: 24
        anchors.leftMargin: 24
        width: Math.max(160, Math.min(canvas.width - viewPill.width - 80, 560))
        padding: 2
        leftPadding: 14
        background: Rectangle {
            radius: 12
            color: "#f2fff8e1"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            width: parent.width
            spacing: 4
            Label {
                objectName: "shownFileNoteText"
                Layout.fillWidth: true
                text: app.shownFileNote
                wrapMode: Text.Wrap
                color: "#4a3b00"
                font.pixelSize: 13
            }
            ToolButton {
                objectName: "shownFileNoteClose"
                text: "×"
                font.pixelSize: 18
                implicitWidth: 36
                onClicked: shownFileNote.closedFor = app.title
            }
        }
    }

    // Page and zoom status, floating over the canvas.
    Pane {
        id: viewPill
        objectName: "viewPill"
        visible: !pageGrid.visible && !contentsOverview.visible  // also in full screen
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 24
        padding: 2
        leftPadding: 14
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
            IconButton {
                objectName: "undoButton"
                iconName: "xopp-edit-undo"
                tip: qsTr("Undo (Ctrl+Z)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canUndo
                onClicked: app.undo()
            }
            IconButton {
                objectName: "redoButton"
                iconName: "xopp-edit-redo"
                tip: qsTr("Redo (Ctrl+Y)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                enabled: app.canRedo
                onClicked: app.redo()
            }
            ToolSeparator {}
            IconButton {
                objectName: "layoutButton"
                iconName: "xqt-columns"
                tip: qsTr("Page layout")
                implicitWidth: 40; implicitHeight: 40
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
                Menu {
                    id: layoutMenu
                    objectName: "layoutMenu"
                    MenuItem {
                        text: qsTr("One page per row")
                        checkable: true
                        checked: app.viewColumns === 1 && !app.pairedPages
                        onTriggered: { app.pairedPages = false; app.viewColumns = 1 }
                    }
                    MenuItem {
                        text: qsTr("Two pages side by side")
                        checkable: true
                        checked: app.pairedPages && app.pairsOffset === 0
                        onTriggered: { app.viewColumns = 2; app.pairsOffset = 0; app.pairedPages = true }
                    }
                    MenuItem {
                        text: qsTr("Book (cover page alone)")
                        checkable: true
                        checked: app.pairedPages && app.pairsOffset === 1
                        onTriggered: { app.viewColumns = 2; app.pairsOffset = 1; app.pairedPages = true }
                    }
                    MenuSeparator {}
                    // N columns
                    RowLayout {
                        width: parent ? parent.width : implicitWidth
                        Label { text: qsTr("Columns"); Layout.leftMargin: 16; Layout.fillWidth: true }
                        ToolButton {
                            text: "−"; font.pixelSize: 20
                            enabled: app.viewColumns > 1
                            onClicked: { app.pairedPages = false; app.viewColumns = app.viewColumns - 1 }
                        }
                        Label {
                            objectName: "columnsLabel"
                            text: app.viewColumns
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            Layout.minimumWidth: 20
                        }
                        ToolButton {
                            text: "+"; font.pixelSize: 20
                            enabled: app.viewColumns < 8
                            onClicked: { app.pairedPages = false; app.viewColumns = app.viewColumns + 1 }
                        }
                    }
                }
            }
            ToolSeparator {}
            IconButton {
                objectName: "pageGridButton"
                iconName: "xqt-pages-grid"
                tip: qsTr("All pages (Ctrl+Alt+G)")
                implicitWidth: 40; implicitHeight: 40
                icon.width: 22; icon.height: 22
                onClicked: pageGrid.open()
            }
            Label { text: app.pageNumber + " / " + app.pageCount; color: "#505050"; Layout.rightMargin: 6 }
            ToolSeparator {}
            ToolButton { text: "−"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomOut() }
            ToolButton {
                objectName: "zoomButton"
                text: app.zoomPercent + " %"
                implicitWidth: 72
                onClicked: app.fitWidth()
                onPressAndHold: Popups.openAt(fitMenu)
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
                    onTapped: function(point) { Popups.openAt(fitMenu, point.position) }
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit the width (press and hold: more)")
                ToolTip.delay: 600
                Menu {
                    id: fitMenu
                    objectName: "fitMenu"
                    MenuItem { text: qsTr("Fit the width (Ctrl+0)"); onTriggered: app.fitWidth() }
                    MenuItem { text: qsTr("Fit the height"); onTriggered: app.fitHeight() }
                    MenuItem {
                        objectName: "fitPageItem"
                        text: app.currentPageDiffers ? qsTr("Fit this page (its size differs)") : qsTr("Fit the whole page")
                        onTriggered: app.fitPage()
                    }
                }
            }
            ToolButton { text: "+"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomIn() }
        }
    }

    PageGrid {
        id: pageGrid
        objectName: "pageGrid"
        anchors.fill: canvas
    }

    // While a tab is dragged off the strip: what happens when it is let go
    Rectangle {
        objectName: "tabDragHint"
        visible: tabStrip.dragIndex >= 0
        z: 60
        anchors.horizontalCenter: parent.horizontalCenter
        y: 12
        width: hintRow.implicitWidth + 28
        height: 44
        radius: 22
        readonly property bool willMove: tabStrip.dragDistance > tabStrip.undockDistance
        color: willMove ? Material.accentColor : "#e8eaed"
        border.width: 1
        border.color: willMove ? Material.accentColor : "#c9ccd1"
        opacity: 0.96
        RowLayout {
            id: hintRow
            anchors.centerIn: parent
            spacing: 8
            Label {
                text: app.secondaryWindow ? (parent.parent.willMove ? qsTr("Let go: back to the main window")
                                                                    : qsTr("Drag down: back to the main window"))
                                          : (parent.parent.willMove ? qsTr("Let go: a window of its own")
                                                                    : qsTr("Drag down: a window of its own"))
                color: parent.parent.willMove ? "#ffffff" : "#3c4043"
                font.weight: Font.DemiBold
            }
        }
    }
    // Text mode: beside the pages (right), the canvas makes room
    TextFlowPanel {
        id: textFlowPanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: win.toolbarPosition === "right" ? sideTools.left : parent.right
        width: visible ? Math.min(Math.max(360, win.width * 0.38), 600) : 0
    }
    // Markdown box: the same place
    MarkdownPanel {
        id: markdownPanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: win.toolbarPosition === "right" ? sideTools.left : parent.right
        width: visible ? Math.min(Math.max(360, win.width * 0.38), 600) : 0
    }
    Connections {
        target: app
        // The text tool tapped a Markdown box
        function onMarkdownRequested(page) {
            if (textFlowPanel.visible) textFlowPanel.close(true)
            if (!markdownPanel.visible || app.markdownPage !== page || !app.markdownIsPageText) markdownPanel.open(page)
        }
        // A Markdown text box (or a place for a new one)
        function onMarkdownBoxRequested(page, x, y) {
            if (textFlowPanel.visible) textFlowPanel.close(true)
            markdownPanel.openBox(page, x, y)
        }
    }
    ContentsOverview {
        id: contentsOverview
        anchors.fill: canvas
        onVisibleChanged: if (visible && pageGrid.visible) pageGrid.close()
    }
    Connections {
        target: pageGrid
        function onVisibleChanged() { if (pageGrid.visible && contentsOverview.visible) contentsOverview.close() }
    }

    // Actions on the selected elements (select tools).
    Pane {
        id: selectionBar
        objectName: "selectionBar"
        visible: app.hasSelection && !pageGrid.visible
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: 24
        anchors.horizontalCenter: canvas.horizontalCenter
        padding: 2
        leftPadding: 8
        rightPadding: 8
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f7fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            IconButton { iconName: "xopp-edit-copy"; tip: qsTr("Copy (Ctrl+C)"); onClicked: app.copySelection() }
            IconButton { iconName: "xopp-edit-cut"; tip: qsTr("Cut (Ctrl+X)"); onClicked: app.cutSelection() }
            IconButton { iconName: "xopp-edit-paste"; tip: qsTr("Paste (Ctrl+V)"); onClicked: app.pasteElements() }
            IconButton { iconName: "xqt-delete"; tip: qsTr("Delete (Del)"); onClicked: app.deleteSelection() }
            ToolSeparator {}
            IconButton { iconName: "xqt-close"; tip: qsTr("Deselect (Esc)"); onClicked: app.clearSelection() }
        }
    }

    // Selected PDF text: mark or copy it. The pill sits at the text and goes along with it while scrolling; once
    // the text is out of sight it waits at the top edge of the canvas and offers the way back to it.
    Pane {
        id: pdfTextBar
        objectName: "pdfTextBar"
        visible: app.pdfTextIsSelected && !pageGrid.visible && !contentsOverview.visible
        padding: 2
        /// The selected text on the canvas; read again whenever the view moves
        property rect box: Qt.rect(0, 0, 0, 0)
        readonly property bool above: box.y + box.height < 8
        readonly property bool below: box.y > canvas.height - 8
        /// The text is not in view any more
        readonly property bool away: (box.width !== 0 || box.height !== 0) && (above || below)
        function refresh() {
            box = app.pdfSelectionBox()
            if (away) {
                x = canvas.x + (canvas.width - width) / 2
                y = canvas.y + 12
                return
            }
            x = Math.max(canvas.x + 8, Math.min(canvas.x + box.x, canvas.x + canvas.width - width - 8))
            y = canvas.y + box.y - height - 8 < canvas.y ? canvas.y + box.y + box.height + 8
                                                         : canvas.y + box.y - height - 8
        }
        onVisibleChanged: if (visible) refresh(); else pasteOffered = false
        /// Selected by a long press (finger or pen): paste at that place is offered too, as the long press does
        /// everywhere else (only if there is something to paste)
        property bool pasteOffered: false
        property point pasteAt: Qt.point(0, 0)
        function offerPaste(viewPos) {
            pasteAt = viewPos
            pasteOffered = app.canPaste()
            Qt.callLater(refresh)  // (wider now)
        }
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f7fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        Connections {
            target: app
            function onPdfTextSelectionChanged() { pdfTextBar.refresh() }
            function onPdfTextSelected(rect) { pdfTextBar.refresh() }
        }
        Connections {
            target: canvas
            function onViewportChanged() { pdfTextBar.refresh() }
        }
        RowLayout {
            spacing: 0
            // Only while the text is out of sight: back to it
            IconButton {
                objectName: "pdfBackToSelection"
                iconName: pdfTextBar.above ? "xqt-chevron-up" : "xqt-chevron-down"
                tip: qsTr("Back to the selected text")
                visible: pdfTextBar.away
                onClicked: app.showPdfSelection()
            }
            ToolSeparator { visible: pdfTextBar.away }
            IconButton { iconName: "xopp-select-pdf-text-ht"; tip: qsTr("Highlight"); onClicked: app.markPdfText("highlight") }
            HighlightColors { onPicked: app.markPdfText("highlight") }  // a color: highlight in it right away
            ToolSeparator {}
            IconButton { iconName: "xqt-underline"; tip: qsTr("Underline"); onClicked: app.markPdfText("underline") }
            IconButton { iconName: "xqt-strikethrough"; tip: qsTr("Strike through"); onClicked: app.markPdfText("strikethrough") }
            IconButton { iconName: "xopp-edit-copy"; tip: qsTr("Copy text"); onClicked: app.copyPdfText() }
            ToolSeparator { visible: pdfTextBar.pasteOffered }
            IconButton {
                objectName: "pdfTextPaste"
                iconName: "xopp-edit-paste"
                tip: qsTr("Paste here")
                visible: pdfTextBar.pasteOffered
                onClicked: {
                    const at = pdfTextBar.pasteAt
                    app.clearPdfTextSelection()  // it was about pasting, not about the text
                    app.pasteAt(at.x, at.y)
                }
            }
        }
    }

    // Back / forward after jumps (links, page grid, sidebar)
    Pane {
        id: navPill
        objectName: "navPill"
        visible: (app.canGoBack || app.canGoForward) && !pageGrid.visible
        anchors.left: canvas.left
        anchors.bottom: canvas.bottom
        anchors.leftMargin: 20
        anchors.bottomMargin: 24
        padding: 2
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f7fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        RowLayout {
            spacing: 0
            IconButton {
                objectName: "navBack"
                iconName: "xopp-navigate-back"
                tip: qsTr("Back to where you were (Alt+Left)")
                enabled: app.canGoBack
                onClicked: app.navigateBack()
            }
            IconButton {
                objectName: "navForward"
                iconName: "xopp-navigate-forward"
                tip: qsTr("Forward (Alt+Right)")
                enabled: app.canGoForward
                onClicked: app.navigateForward()
            }
            IconButton {
                iconName: "xqt-close"
                tip: qsTr("Forget these places")
                implicitWidth: 36
                onClicked: app.clearNavigation()
            }
        }
    }

    // A tapped PDF link: open it / go to the page (not at once: a tap can be a mistake).
    Popup {
        id: linkPopup
        objectName: "linkPopup"
        property string uri
        property int page: -1
        padding: 6
        Connections {
            target: app
            function onLinkTapped(uri, page, rect) {
                linkPopup.uri = uri
                linkPopup.page = page
                linkPopup.x = Math.max(8, Math.min(canvas.x + rect.x, win.width - linkPopup.width - 8))
                linkPopup.y = canvas.y + rect.y + rect.height + 6
                if (linkPopup.y + 60 > win.height) linkPopup.y = canvas.y + rect.y - 60
                linkPopup.open()
            }
        }
        RowLayout {
            spacing: 4
            Image { source: app.iconUrl("xqt-link"); sourceSize.width: 18; sourceSize.height: 18; Layout.leftMargin: 6 }
            Label {
                visible: linkPopup.uri !== ""
                text: linkPopup.uri
                elide: Text.ElideMiddle
                Layout.maximumWidth: 320
            }
            Button {
                objectName: "linkButton"
                flat: true
                text: linkPopup.uri !== "" ? qsTr("Open") : linkPopup.page >= 0 ? qsTr("Go to page %1").arg(linkPopup.page + 1)
                                                                              : qsTr("Page not in this document")
                enabled: linkPopup.uri !== "" || linkPopup.page >= 0
                onClicked: {
                    if (linkPopup.uri !== "") app.openLink(linkPopup.uri)
                    else app.jumpToPage(linkPopup.page)
                    linkPopup.close()
                }
            }
        }
    }

    SearchBar {
        id: searchBar
        objectName: "searchBar"
        anchors.top: canvas.top
        anchors.topMargin: 12
        anchors.horizontalCenter: canvas.horizontalCenter
    }

    // Scroll bars over the canvas: wide enough to be dragged with a finger or the pen.
    ScrollBar {
        id: vbar
        orientation: Qt.Vertical
        anchors.top: canvas.top
        anchors.right: canvas.right
        // (beside the strip that brings a right tool bar back, not under it)
        anchors.rightMargin: toolbarShow.visible && toolbarShow.side === "right" ? toolbarShow.width : 0
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: hbar.visible ? hbar.height : 0
        visible: canvas.contentHeight > canvas.height + 1 && !pageGrid.visible
        policy: ScrollBar.AlwaysOn
        padding: 6
        minimumSize: 0.05
        size: canvas.contentHeight > 0 ? Math.min(1, canvas.height / canvas.contentHeight) : 1
        position: canvas.contentHeight > 0 ? canvas.contentY / canvas.contentHeight : 0
        onPositionChanged: if (pressed) canvas.scrollTo(canvas.contentX, position * canvas.contentHeight)
        contentItem: Rectangle {
            implicitWidth: vbar.pressed || vbar.hovered ? 10 : 7
            implicitHeight: 48
            radius: width / 2
            // Light handle with a dark outline: visible on the grey background and on white pages.
            color: vbar.pressed ? "#ffffff" : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            opacity: vbar.pressed || vbar.hovered ? 1.0 : 0.9
        }
        background: Rectangle { color: vbar.pressed || vbar.hovered ? "#30ffffff" : "transparent" }
    }
    ScrollBar {
        id: hbar
        orientation: Qt.Horizontal
        anchors.left: canvas.left
        anchors.right: canvas.right
        anchors.bottom: canvas.bottom
        anchors.rightMargin: vbar.visible ? vbar.width : 0
        visible: canvas.contentWidth > canvas.width + 1 && !pageGrid.visible
        policy: ScrollBar.AlwaysOn
        padding: 6
        minimumSize: 0.05
        size: canvas.contentWidth > 0 ? Math.min(1, canvas.width / canvas.contentWidth) : 1
        position: canvas.contentWidth > 0 ? canvas.contentX / canvas.contentWidth : 0
        onPositionChanged: if (pressed) canvas.scrollTo(position * canvas.contentWidth, canvas.contentY)
        contentItem: Rectangle {
            implicitWidth: 48
            implicitHeight: hbar.pressed || hbar.hovered ? 10 : 7
            radius: height / 2
            // Light handle with a dark outline: visible on the grey background and on white pages.
            color: hbar.pressed ? "#ffffff" : "#e8eaed"
            border.width: 1
            border.color: "#80000000"
            opacity: hbar.pressed || hbar.hovered ? 1.0 : 0.9
        }
        background: Rectangle { color: hbar.pressed || hbar.hovered ? "#30ffffff" : "transparent" }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open document or PDF")
        currentFolder: app.openFolder()
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf *.md *.png *.jpg *.jpeg *.webp *.heic *.heif)"),
                      qsTr("Xournal++ files (*.xopp *.xoj)"), qsTr("PDF files (*.pdf)"), qsTr("Markdown files (*.md)"),
                      qsTr("Images (*.png *.jpg *.jpeg *.webp *.heic *.heif)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: app.openUrls(selectedFiles)
    }
    FileDialog {
        id: saveDialog
        property var afterSave: null
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xopp"
        nameFilters: [qsTr("Xournal++ files (*.xopp)")]
        onAccepted: {
            if (app.saveAs(selectedFile) && afterSave) afterSave()
            afterSave = null
        }
        onRejected: afterSave = null
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export as PDF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: app.exportPdf(selectedFile)
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("Add a color to the tool bar")
        selectedColor: app.color
        onAccepted: {
            app.addToolbarColor(selectedColor)
            app.setColor(selectedColor)
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Insert image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"), qsTr("All files (*)")]
        onAccepted: app.insertImage(selectedFile)
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Unsaved changes")
        width: Math.min(win.width * 0.9, 480)
        Label {
            width: unsavedDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("\"%1\" has unsaved changes.").arg(app.title)
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onAccepted: { unsavedDialog.close(); saveOrAsk(afterDiscardCheck) }
            onRejected: { unsavedDialog.close(); afterDiscardCheck = null }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    unsavedDialog.close()
                    var action = afterDiscardCheck
                    afterDiscardCheck = null
                    if (action) action()
                }
            }
        }
    }

    // After a crash: offer the documents with unsaved changes (from emergency and autosave files).
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(win.width * 0.9, 560)
        title: qsTr("Recover unsaved changes?")
        ColumnLayout {
            width: recoveryDialog.availableWidth
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Xournal Qt did not close properly. These documents have changes that were not saved:")
            }
            Repeater {
                model: app.recoveryItems
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.title; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Label { text: modelData.time; color: "#6b6f75" }
                }
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Recover"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard changes"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            onAccepted: { recoveryDialog.close(); app.recover(true) }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    recoveryDialog.close()
                    app.recover(false)
                }
            }
        }
    }
    Component.onCompleted: if (app.recoveryItems.length > 0) recoveryDialog.open()

    Dialog {
        id: closeAllDialog
        objectName: "closeAllDialog"
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width * 0.8, 480)
        title: qsTr("Close all documents?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        ColumnLayout {
            width: closeAllDialog.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("%1 documents are open. Documents with unsaved changes ask before they close.")
                        .arg(app.tabs.count)
            }
        }
        onAccepted: { tabOverview.close(); closeAllTabs() }
    }

    Dialog {
        id: messageDialog
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width * 0.8, 640)
        standardButtons: Dialog.Ok
        property alias text: messageLabel.text
        Label { id: messageLabel; wrapMode: Text.Wrap; width: parent.width }
    }
    Snackbar {
        id: snackbar
        objectName: "snackbar"
        z: 100
        anchors.horizontalCenter: canvas.horizontalCenter
        anchors.bottom: canvas.bottom
        anchors.bottomMargin: 96
    }
    Connections {
        target: app
        function onPageActionDone(text, undoable) { snackbar.show(text, undoable) }
    }

    Connections {
        target: app
        function onMessage(title, text, error) {
            messageDialog.title = title !== "" ? title : (error ? qsTr("Error") : qsTr("Information"))
            messageDialog.text = text
            messageDialog.open()
        }
    }

    // Library and recent documents: over the document area while shown.
    HomeView {
        id: homeView
        anchors.fill: parent
        z: 50
        visible: app.homeVisible
        onOpenFileRequested: openDialog.open()
        onSettingsRequested: settingsPage.open()
    }

    // Putting the tool bar away and getting it back: a small tab at its end, and a slim strip while it is away.
    Rectangle {
        objectName: "toolbarToggle"
        visible: !app.homeVisible && !win.fullScreenMode && !app.toolbarHidden
        z: 58
        width: win.sideToolbar ? 18 : 42
        height: win.sideToolbar ? 42 : 18
        radius: 6
        color: "#ffffff"
        border.width: 1
        border.color: "#d5d8dc"
        // At the end of the bar, a little into the pages
        x: win.toolbarPosition === "left" ? sideTools.width - width / 2
           : win.toolbarPosition === "right" ? parent.width - sideTools.width - width / 2
           : parent.width - width - 18
        y: win.sideToolbar ? Math.round(parent.height / 2) : -height / 2
        Image {  // towards the bar it puts away: up, left or right
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-up")
            sourceSize.width: 15
            sourceSize.height: 15
            rotation: win.toolbarPosition === "left" ? -90 : win.toolbarPosition === "right" ? 90 : 0
        }
        TapHandler { onTapped: app.toolbarHidden = true }
        ToolTip.visible: hoverHandler.hovered
        ToolTip.text: qsTr("Hide the tool bar")
        ToolTip.delay: 600
        HoverHandler { id: hoverHandler }
    }
    // While it is away: a slim strip at the edge where it was (top, left or right) brings it back
    Rectangle {
        id: toolbarShow
        objectName: "toolbarShow"
        visible: !app.homeVisible && !win.fullScreenMode && app.toolbarHidden
        readonly property string side: win.sideToolbar ? win.toolbarPosition : "top"
        z: 60  // over the edge of the pen pill, which sits at the right edge by default
        width: side === "top" ? 96 : 16
        height: side === "top" ? 16 : 96
        x: side === "left" ? 0 : side === "right" ? parent.width - width : Math.round((parent.width - width) / 2)
        y: side === "top" ? 0 : Math.round((parent.height - height) / 2)
        radius: 8
        color: "#f1f3f4"
        border.width: 1
        border.color: "#d5d8dc"
        opacity: showHover.hovered ? 1 : 0.75
        Image {  // where the bar comes in from: down from the top, into the pages from a side
            anchors.centerIn: parent
            source: app.iconUrl("xqt-chevron-down")
            sourceSize.width: 15
            sourceSize.height: 15
            rotation: toolbarShow.side === "left" ? -90 : toolbarShow.side === "right" ? 90 : 0
        }
        TapHandler { onTapped: app.toolbarHidden = false }
        HoverHandler { id: showHover }
        ToolTip.visible: showHover.hovered
        ToolTip.text: qsTr("Show the tool bar")
        ToolTip.delay: 600
    }

    // Without a tool bar and with an ink tool: colors, width and pen / highlighter at a side of the screen
    PenPill {}
    // The setsquare / compass: what it can do, and putting it aside for a moment
    GeometryPill {
        anchors.top: canvas.top
        anchors.right: canvas.right
        anchors.topMargin: 12
        anchors.rightMargin: 20
        z: 57
    }
    ColorDialog {
        id: pillColorDialog
        onAccepted: app.addPenColor(selectedColor)
    }

    // Full screen: the current tool in a small square (drag it anywhere); a tap offers all tools and colors.
    Rectangle {
        id: quickToolSquare
        objectName: "quickToolSquare"
        // Only in full screen: with the bar merely put away, the arrow strip brings it back at once
        visible: win.fullScreenMode && !app.homeVisible
        z: 60
        x: 16
        y: 16
        width: 56
        height: 56
        radius: 12
        color: "#f7ffffff"
        border.width: 2
        border.color: app.color
        readonly property var toolIcons: ({
            "pen": "xopp-tool-pencil", "highlighter": "xopp-tool-highlighter", "eraser": "xopp-tool-eraser",
            "hand": "xopp-hand", "text": "xopp-tool-text", "selectRect": "xopp-select-rect",
            "selectRegion": "xopp-select-lasso", "selectPdfTextLinear": "xopp-select-pdf-text-ht",
            "selectPdfTextRect": "xopp-select-pdf-text-area"
        })
        Image {
            anchors.centerIn: parent
            source: app.iconUrl(quickToolSquare.toolIcons[app.tool] || "xopp-tool-pencil")
            sourceSize.width: 30
            sourceSize.height: 30
        }
        Rectangle {  // the color
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 5
            width: 14; height: 14; radius: 7
            color: app.color
            border.width: 1
            border.color: "#80000000"
        }
        DragHandler {
            target: parent
            xAxis.minimum: 0; xAxis.maximum: win.contentItem.width - quickToolSquare.width
            yAxis.minimum: 0; yAxis.maximum: win.contentItem.height - quickToolSquare.height
        }
        TapHandler { onTapped: quickTools.visible ? quickTools.close() : quickTools.open() }
    }
    Popup {
        id: quickTools
        objectName: "quickTools"
        parent: Overlay.overlay
        // next to the square, inside the window
        x: Math.min(quickToolSquare.x + quickToolSquare.width + 8, win.contentItem.width - width - 8)
        y: Math.max(8, Math.min(quickToolSquare.y, win.contentItem.height - height - 8))
        padding: 8
        width: 6 * 50 + 30
        height: Math.min(win.contentItem.height - 16, quickToolsColumn.implicitHeight + 16)
        Column {
            id: quickToolsColumn
            width: parent.width
            spacing: 4
            Item {
                id: quickToolsHolder
                width: parent.width
                height: Math.min(toolRow.implicitHeight, win.contentItem.height - 90)
            }
            Button {
                objectName: "leaveFullScreenButton"
                width: parent.width
                flat: true
                text: qsTr("Leave full screen (Esc)")
                onClicked: win.fullScreenMode = false
            }
        }
        // A tool, color or size was chosen: back to writing
        Connections {
            target: app
            enabled: quickTools.opened
            function onToolChanged() { quickTools.close() }
        }
    }

    InsertPagesDialog { id: insertPagesDialog }
    PrintDialog { id: printDialog }
    ChapterDialog { id: chapterDialog }
    ContextPill { id: contextPill }
    PdfTextHandles { }
    Connections {
        target: app
        // On PDF text a long press (or right click) selects the word, and its actions offer paste as well; elsewhere
        // it offers what can be done here
        function onContextRequested(viewPos) {
            if (app.selectPdfTextAt(viewPos.x, viewPos.y)) {
                pdfTextBar.offerPaste(viewPos)
                return
            }
            contextPill.openAt(viewPos, app.pdfTextIsSelected)
        }
    }
    Connections {
        target: app
        function onChapterRequested(page) { chapterDialog.openFor(page) }
    }
    Connections {
        target: app
        function onPrintRequested(pages) { printDialog.openFor(pages) }
    }
    BackgroundDialog { id: backgroundDialog }
    Connections {
        target: app
        function onPageBackgroundRequested(pages) { backgroundDialog.openFor(pages) }
    }
    Connections {
        target: app
        function onInsertPagesRequested(position) { insertPagesDialog.openAt(position) }
    }

    SettingsPage {
        id: settingsPage
        objectName: "settingsPage"
        onQuitRequested: win.closeWindow()  // (asks about unsaved documents first)
    }
    TabOverview {
        id: tabOverview
        objectName: "tabOverview"
        onCloseRequested: function(index) { requestCloseTab(index) }
        onCloseAllRequested: app.tabs.count > 1 ? closeAllDialog.open() : closeAllTabs()
        onLibrarySearchRequested: win.searchLibrary()
    }

    // Document shortcuts do nothing while the home screen is shown.
    readonly property bool docKeys: !app.homeVisible && !app.textFlowActive && !app.markdownActive
    // The keys come from the shortcut settings (app.shortcuts); reading its revision keeps the bindings fresh.
    function keysOf(id) { return (app.shortcuts.revision, app.shortcuts.keys(id)) }
    Shortcut { sequences: win.keysOf("undo"); enabled: docKeys; onActivated: app.undo() }
    // The tools on single keys: only while the page is at hand (typing into a text or a field takes its keys first;
    // the overviews and the settings search or edit what is typed)
    readonly property bool toolKeys: docKeys && !pageGrid.visible && !contentsOverview.visible && !tabOverview.visible
                                     && !settingsPage.visible
    Shortcut { sequences: win.keysOf("toolPen"); enabled: toolKeys; onActivated: app.selectTool("pen") }
    Shortcut { sequences: win.keysOf("toolEraser"); enabled: toolKeys; onActivated: app.selectTool("eraser") }
    Shortcut { sequences: win.keysOf("toolHighlighter"); enabled: toolKeys; onActivated: app.selectTool("highlighter") }
    Shortcut { sequences: win.keysOf("toolText"); enabled: toolKeys; onActivated: app.selectTool("text") }
    Shortcut { sequences: win.keysOf("toolSelect"); enabled: toolKeys; onActivated: app.selectTool("selectRect") }
    Shortcut { sequences: win.keysOf("toolLasso"); enabled: toolKeys; onActivated: app.selectTool("selectRegion") }
    Shortcut { sequences: win.keysOf("toolHand"); enabled: toolKeys; onActivated: app.selectTool("hand") }
    Shortcut { sequences: win.keysOf("insertImage"); enabled: toolKeys; onActivated: imageDialog.open() }
    Shortcut { sequences: win.keysOf("redo"); enabled: docKeys; onActivated: app.redo() }
    Shortcut { sequences: win.keysOf("save"); enabled: docKeys; onActivated: saveOrAsk(null) }
    Shortcut { sequences: win.keysOf("saveAs"); enabled: docKeys; onActivated: openSaveDialog(null) }
    Shortcut { sequences: win.keysOf("open"); onActivated: openDialog.open() }
    // Ctrl+N adds a page (what one needs while writing), Ctrl+Shift+N a document
    Shortcut { sequences: win.keysOf("addPage"); enabled: docKeys; onActivated: app.addPageAfterCurrent() }
    Shortcut { sequences: win.keysOf("newDocument"); onActivated: app.newDocument() }
    Shortcut { sequences: win.keysOf("addPage"); enabled: app.homeVisible; onActivated: app.newDocument() }
    Shortcut { sequences: win.keysOf("closeTab"); enabled: docKeys; onActivated: requestCloseTab(app.currentTab) }
    // The home screen (library)
    Shortcut { sequences: win.keysOf("home"); onActivated: app.homeVisible = !app.homeVisible || app.tabCount() === 0 }
    // Tabs: Ctrl+Tab / Ctrl+Shift+Tab (Shift+Tab arrives as Backtab), like browsers; also Ctrl+PgDown / Ctrl+PgUp.
    Shortcut {
        // (window context: with a second window, an application shortcut would be there twice - "ambiguous",
        // and then Qt does nothing at all)
        sequences: win.keysOf("nextTab")
        onActivated: app.nextTab()
    }
    Shortcut {
        sequences: win.keysOf("previousTab")
        onActivated: app.previousTab()
    }
    // Four or five fingers on the touch screen: the pages of the document, all open documents
    // (a touch pad reports such gestures to the desktop, not to us).
    TouchGestures {
        objectName: "touchGestures"
        window: win
        function togglePages() {
            if (app.homeVisible) return
            tabOverview.visible ? tabOverview.close() : (pageGrid.visible ? pageGrid.close() : pageGrid.open())
        }
        function toggleDocuments() {
            pageGrid.visible ? pageGrid.close() : 0
            tabOverview.visible ? tabOverview.close() : tabOverview.open()
        }
        onTapped: fingers => fingers >= 5 ? toggleDocuments() : togglePages()
        onPinchedIn: fingers => fingers >= 5 ? toggleDocuments() : togglePages()  // "zoom out": step back
        onPinchedOut: {
            if (tabOverview.visible) tabOverview.close()
            else if (pageGrid.visible) pageGrid.close()
        }
    }
    Shortcut { sequences: win.keysOf("tabOverview"); onActivated: tabOverview.visible ? tabOverview.close() : tabOverview.open() }
    // Search: all open documents (in their overview), the whole library (on the home screen) - from anywhere
    Shortcut {
        sequences: win.keysOf("searchAllDocuments")
        enabled: app.tabs.count > 0  // (a property: an invokable in a binding would not be read again)
        onActivated: { pageGrid.close(); app.homeVisible = false; tabOverview.openSearch() }
    }
    function searchLibrary() {
        tabOverview.close()
        pageGrid.close()
        app.homeVisible = true
        Qt.callLater(homeView.focusSearch)  // (after the home screen is shown: it puts the focus on its grid)
    }
    Shortcut { sequences: win.keysOf("searchLibrary"); onActivated: win.searchLibrary() }
    Shortcut { sequences: win.keysOf("settings"); onActivated: settingsPage.open() }
    Shortcut { sequences: win.keysOf("shortcuts"); onActivated: shortcutSheet.open() }
    // (not StandardKey.FullScreen as well: it is F11 on KDE, twice the same key is ambiguous)
    Shortcut { sequences: win.keysOf("fullScreen"); enabled: !app.homeVisible; onActivated: win.fullScreenMode = !win.fullScreenMode }
    Shortcut { sequence: "Escape"; enabled: win.fullScreenMode && !app.hasSelection; onActivated: win.fullScreenMode = false }
    Shortcut { sequences: win.keysOf("export"); enabled: docKeys; onActivated: openExportDialog() }
    Shortcut { sequences: win.keysOf("print"); enabled: docKeys; onActivated: printDialog.open() }
    Shortcut { sequences: win.keysOf("back"); enabled: docKeys; onActivated: app.navigateBack() }
    Shortcut { sequences: win.keysOf("forward"); enabled: docKeys; onActivated: app.navigateForward() }
    Shortcut { sequences: win.keysOf("pageGrid"); enabled: docKeys; onActivated: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
    Shortcut { sequences: win.keysOf("contents"); enabled: docKeys; onActivated: contentsOverview.visible ? contentsOverview.close() : contentsOverview.open() }
    Shortcut { sequences: win.keysOf("textMode"); enabled: !app.homeVisible; onActivated: textFlowPanel.visible ? textFlowPanel.close(true) : textFlowPanel.open() }
    Shortcut {
        // (Markdown written on the page: its source beside the page)
        sequences: win.keysOf("markdownMode"); enabled: !app.homeVisible
        onActivated: {
            if (markdownPanel.visible) { markdownPanel.close(true); return }
            const onPage = app.takeMarkdownFromPage()
            if (onPage.page === undefined) markdownPanel.open()
            else if (onPage.pageText) markdownPanel.open(onPage.page)
            else markdownPanel.openBox(onPage.page, onPage.x, onPage.y)
        }
    }
    Shortcut { sequences: win.keysOf("find"); onActivated: app.homeVisible ? homeView.focusSearch() : searchBar.openBar() }
    // Selected elements (the page sidebar and grid handle these keys themselves when they have the focus)
    Shortcut { sequences: win.keysOf("copy"); enabled: docKeys; onActivated: app.copySelection() }
    Shortcut { sequences: win.keysOf("cut"); enabled: docKeys; onActivated: app.cutSelection() }
    Shortcut { sequences: win.keysOf("paste"); enabled: docKeys; onActivated: app.pasteElements() }
    Shortcut { sequences: win.keysOf("deleteSelection"); enabled: docKeys && app.hasSelection; onActivated: app.deleteSelection() }
    Shortcut { sequences: win.keysOf("selectAll"); enabled: docKeys; onActivated: app.selectAllOnPage() }
    Shortcut { sequence: "Escape"; enabled: docKeys && app.hasSelection; onActivated: app.clearSelection() }
    Shortcut { sequences: win.keysOf("findNext"); enabled: docKeys; onActivated: app.searchNext() }
    Shortcut { sequences: win.keysOf("findPrevious"); enabled: docKeys; onActivated: app.searchPrevious() }
    Shortcut { sequences: win.keysOf("zoomIn"); enabled: docKeys; onActivated: app.zoomIn() }
    Shortcut { sequences: win.keysOf("zoomOut"); enabled: docKeys; onActivated: app.zoomOut() }
    Shortcut { sequences: win.keysOf("fitWidth"); enabled: docKeys; onActivated: app.fitWidth() }
    Shortcut { sequences: win.keysOf("quit"); onActivated: win.close() }
    ShortcutSheet {
        id: shortcutSheet
        onChangeRequested: { settingsPage.open(); settingsPage.showShortcuts() }
    }
}
