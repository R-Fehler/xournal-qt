// xournal-qt: main window (first usable version: one document, pen / highlighter / eraser / hand).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import XournalQt
import XournalQt.Canvas

ApplicationWindow {
    id: win
    width: 1280
    height: 900
    visible: true
    title: app.homeVisible ? (app.library.available ? app.library.name + " — Xournal Qt" : "Xournal Qt")
                           : (app.modified ? "• " : "") + app.title + " — Xournal Qt"
    Material.theme: Material.Light
    Material.accent: Material.Indigo
    color: "#5f6368"

    property var afterDiscardCheck: null
    property bool sidebarShown: width >= 900
    property bool quitting: false

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
        }
    }

    Connections {
        target: app
        function onRaiseRequested() {
            if (win.visibility === Window.Minimized) win.showNormal()
            win.raise()
            win.requestActivate()
        }
    }

    header: Column {
      TabStrip {
        width: parent.width
        onCloseRequested: function(index) { requestCloseTab(index) }
      }
      ToolBar {
        width: parent.width
        visible: !app.homeVisible
        Material.background: "#ffffff"
        Material.foreground: "#303030"
        height: 56
        // Scrolls sideways when the window is too narrow for all tools (portrait tablet).
        Flickable {
            id: toolFlick
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            contentWidth: toolRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width
            clip: true
        RowLayout {
            id: toolRow
            height: toolFlick.height
            width: Math.max(toolFlick.width, implicitWidth)
            spacing: 2

            IconButton { iconName: "xopp-sidebar-page-preview"; tip: qsTr("Pages"); checked: sidebarShown; onClicked: sidebarShown = !sidebarShown }
            IconButton { objectName: "pageGridButton"; iconName: "xqt-pages-grid"; tip: qsTr("All pages (Ctrl+Alt+G)"); checked: pageGrid.visible; onClicked: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
            ToolSeparator {}
            IconButton { iconName: "xopp-document-new"; tip: qsTr("New document (new tab)"); onClicked: app.newDocument() }
            IconButton { iconName: "xopp-document-open"; tip: qsTr("Open (in a new tab)"); onClicked: openDialog.open() }
            IconButton { iconName: "xopp-document-save"; tip: qsTr("Save"); onClicked: saveOrAsk(null) }
            ToolSeparator {}
            IconButton { iconName: "xopp-edit-undo"; tip: qsTr("Undo"); enabled: app.canUndo; onClicked: app.undo() }
            IconButton { iconName: "xopp-edit-redo"; tip: qsTr("Redo"); enabled: app.canRedo; onClicked: app.redo() }
            ToolSeparator {}
            IconButton { iconName: "xopp-tool-pencil"; tip: qsTr("Pen"); checked: app.tool === "pen"; onClicked: app.selectTool("pen") }
            IconButton { iconName: "xopp-tool-highlighter"; tip: qsTr("Highlighter"); checked: app.tool === "highlighter"; onClicked: app.selectTool("highlighter") }
            IconButton { iconName: "xopp-tool-eraser"; tip: qsTr("Eraser"); checked: app.tool === "eraser"; onClicked: app.selectTool("eraser") }
            IconButton { iconName: "xopp-hand"; tip: qsTr("Hand"); checked: app.tool === "hand"; onClicked: app.selectTool("hand") }
            IconButton {
                objectName: "textButton"
                iconName: "xopp-tool-text"
                tip: qsTr("Text (tap to write; tap a text to edit it)")
                checked: app.tool === "text"
                onClicked: app.tool === "text" ? fontPopup.open() : app.selectTool("text")
                onPressAndHold: fontPopup.open()
                Popup {
                    id: fontPopup
                    y: parent.height
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
                            Label { text: qsTr("Size"); Layout.fillWidth: true }
                            SpinBox {
                                from: 4; to: 200
                                value: Math.round(app.fontSize)
                                editable: true
                                onValueModified: app.fontSize = value
                            }
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
                onClicked: checked ? pdfTextMenu.popup() : app.selectTool("selectPdfTextLinear")
                onPressAndHold: pdfTextMenu.popup()
                Menu {
                    id: pdfTextMenu
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
                onClicked: shapeMenu.popup()
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
                }
            }
            ToolSeparator {}
            Repeater {
                model: app.palette.slice(0, 8)
                delegate: AbstractButton {
                    required property color modelData
                    implicitWidth: 40
                    implicitHeight: 44
                    onClicked: app.setColor(modelData)
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: modelData
                            border.width: Qt.colorEqual(app.color, modelData) ? 3 : 1
                            border.color: Qt.colorEqual(app.color, modelData) ? Material.accentColor : "#9e9e9e"
                        }
                    }
                }
            }
            ToolSeparator {}
            Repeater {
                model: [ { size: 1, dot: 6 }, { size: 2, dot: 10 }, { size: 3, dot: 15 } ]
                delegate: AbstractButton {
                    required property var modelData
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
            Item { Layout.fillWidth: true }
            IconButton { iconName: "xopp-page-add"; tip: qsTr("Add page after the current one"); onClicked: app.addPageAfterCurrent() }
            ToolSeparator {}
            IconButton { objectName: "searchButton"; iconName: "xqt-search"; tip: qsTr("Search (Ctrl+F)"); checked: searchBar.visible; onClicked: searchBar.visible ? searchBar.closeBar() : searchBar.openBar() }
            IconButton { objectName: "overviewButton"; iconName: "xqt-tabs-grid"; tip: qsTr("All open documents (Ctrl+Shift+E)"); onClicked: tabOverview.open() }
            IconButton { objectName: "settingsButton"; iconName: "xqt-settings"; tip: qsTr("Settings (Ctrl+,)"); onClicked: settingsPage.open() }
            IconButton {
                objectName: "moreButton"
                iconName: "xqt-more"
                tip: qsTr("More")
                onClicked: moreMenu.popup()
                Menu {
                    id: moreMenu
                    MenuItem { text: qsTr("Save as…"); onTriggered: openSaveDialog(null) }
                    MenuItem { text: qsTr("Export as PDF…"); onTriggered: openExportDialog() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Insert image…"); onTriggered: imageDialog.open() }
                    MenuItem { text: qsTr("All pages"); onTriggered: pageGrid.open() }
                    MenuItem { text: qsTr("All open documents"); onTriggered: tabOverview.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Settings"); onTriggered: settingsPage.open() }
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
        anchors.left: parent.left
        width: 210
        visible: sidebarShown
    }

    DocumentCanvas {
        id: canvas
        objectName: "canvas"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: sidebar.visible ? sidebar.right : parent.left
        clip: true  // zoomed-in pages must not paint over the sidebar
        view: app.view
    }

    // Page and zoom status, floating over the canvas.
    Pane {
        id: viewPill
        visible: !pageGrid.visible
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
                objectName: "layoutButton"
                iconName: "xqt-columns"
                tip: qsTr("Page layout")
                implicitWidth: 40; implicitHeight: 40
                onClicked: layoutMenu.popup()
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
            Label { text: app.pageNumber + " / " + app.pageCount; color: "#505050"; Layout.rightMargin: 6 }
            ToolSeparator {}
            ToolButton { text: "−"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomOut() }
            ToolButton {
                text: app.zoomPercent + " %"
                implicitWidth: 72
                onClicked: app.fitWidth()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit page width")
            }
            ToolButton { text: "+"; font.pixelSize: 22; implicitWidth: 44; onClicked: app.zoomIn() }
        }
    }

    PageGrid {
        id: pageGrid
        objectName: "pageGrid"
        anchors.fill: canvas
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

    // Selected PDF text (select mode): mark or copy it.
    Pane {
        id: pdfTextBar
        objectName: "pdfTextBar"
        visible: false
        padding: 2
        Material.foreground: "#303030"
        background: Rectangle {
            radius: height / 2
            color: "#f7fafafa"
            border.width: 1
            border.color: "#40000000"
        }
        Connections {
            target: app
            function onPdfTextSelected(rect) {
                pdfTextBar.x = Math.max(canvas.x + 8, Math.min(canvas.x + rect.x, canvas.x + canvas.width - pdfTextBar.width - 8))
                pdfTextBar.y = canvas.y + rect.y - pdfTextBar.height - 8 < canvas.y
                        ? canvas.y + rect.y + rect.height + 8 : canvas.y + rect.y - pdfTextBar.height - 8
                pdfTextBar.visible = true
            }
            function onPdfTextSelectionCleared() { pdfTextBar.visible = false }
            function onDocumentChanged() { pdfTextBar.visible = false }
        }
        RowLayout {
            spacing: 0
            IconButton { iconName: "xopp-select-pdf-text-ht"; tip: qsTr("Highlight"); onClicked: app.markPdfText("highlight") }
            IconButton { iconName: "xqt-underline"; tip: qsTr("Underline"); onClicked: app.markPdfText("underline") }
            IconButton { iconName: "xqt-strikethrough"; tip: qsTr("Strike through"); onClicked: app.markPdfText("strikethrough") }
            IconButton { iconName: "xopp-edit-copy"; tip: qsTr("Copy text"); onClicked: app.copyPdfText() }
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
        nameFilters: [qsTr("Documents (*.xopp *.xoj *.pdf)"), qsTr("Xournal++ files (*.xopp *.xoj)"), qsTr("PDF files (*.pdf)"), qsTr("All files (*)")]
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
    }

    SettingsPage { id: settingsPage; objectName: "settingsPage" }
    TabOverview {
        id: tabOverview
        objectName: "tabOverview"
        onCloseRequested: function(index) { requestCloseTab(index) }
    }

    // Document shortcuts do nothing while the home screen is shown.
    readonly property bool docKeys: !app.homeVisible
    Shortcut { sequences: [StandardKey.Undo]; enabled: docKeys; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: docKeys; onActivated: app.redo() }
    Shortcut { sequences: [StandardKey.Save]; enabled: docKeys; onActivated: saveOrAsk(null) }
    Shortcut { sequences: [StandardKey.SaveAs]; enabled: docKeys; onActivated: openSaveDialog(null) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.New, StandardKey.AddTab]; onActivated: app.newDocument() }
    Shortcut { sequences: [StandardKey.Close]; enabled: docKeys; onActivated: requestCloseTab(app.currentTab) }
    // The home screen (library)
    Shortcut { sequences: ["Ctrl+Shift+L", "Alt+Home"]; onActivated: app.homeVisible = !app.homeVisible || app.tabCount() === 0 }
    // Tabs: Ctrl+Tab / Ctrl+Shift+Tab (Shift+Tab arrives as Backtab), like browsers; also Ctrl+PgDown / Ctrl+PgUp.
    Shortcut {
        sequences: ["Ctrl+Tab", "Ctrl+PgDown"]
        context: Qt.ApplicationShortcut
        onActivated: app.nextTab()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Ctrl+Backtab", "Ctrl+Shift+Backtab", "Ctrl+PgUp"]
        context: Qt.ApplicationShortcut
        onActivated: app.previousTab()
    }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: tabOverview.visible ? tabOverview.close() : tabOverview.open() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsPage.open() }
    Shortcut { sequence: "Ctrl+E"; enabled: docKeys; onActivated: openExportDialog() }
    Shortcut { sequences: [StandardKey.Back]; enabled: docKeys; onActivated: app.navigateBack() }
    Shortcut { sequences: [StandardKey.Forward]; enabled: docKeys; onActivated: app.navigateForward() }
    Shortcut { sequence: "Ctrl+Alt+G"; enabled: docKeys; onActivated: pageGrid.visible ? pageGrid.close() : pageGrid.open() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: app.homeVisible ? homeView.focusSearch() : searchBar.openBar() }
    // Selected elements (the page sidebar and grid handle these keys themselves when they have the focus)
    Shortcut { sequences: [StandardKey.Copy]; enabled: docKeys; onActivated: app.copySelection() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: docKeys; onActivated: app.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: docKeys; onActivated: app.pasteElements() }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: docKeys && app.hasSelection; onActivated: app.deleteSelection() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: docKeys; onActivated: app.selectAllOnPage() }
    Shortcut { sequence: "Escape"; enabled: docKeys && app.hasSelection; onActivated: app.clearSelection() }
    Shortcut { sequences: [StandardKey.FindNext]; enabled: docKeys; onActivated: app.searchNext() }
    Shortcut { sequences: [StandardKey.FindPrevious]; enabled: docKeys; onActivated: app.searchPrevious() }
    Shortcut { sequences: [StandardKey.ZoomIn]; enabled: docKeys; onActivated: app.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; enabled: docKeys; onActivated: app.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; enabled: docKeys; onActivated: app.fitWidth() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: win.close() }
}
