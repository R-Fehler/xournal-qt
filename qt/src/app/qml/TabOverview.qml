// Overview of all open documents (tabs) as a grid of cards with the current page of each: tap a card to switch to
// it, × to close it, + for a new document. Keyboard: arrows, Enter, Delete, Escape. A double click (mouse) or a press
// and hold on a card's title edits the document's name in place (qt/rename).
// The search field searches all open documents: documents with hits are marked; opening one shows its hits (the
// document's own search, from its current page on). "Fuzzy" in the field: fzf's syntax, as in the library (the same
// app-wide toggle): a document is marked when the expression holds with the terms in its title or text.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Fuzzy.js" as Fuzzy

Popup {
    id: overview
    signal closeRequested(int index)
    signal closeAllRequested()
    /// Ctrl+Alt+F here: search the library instead (the window's shortcuts do not reach into this modal overview)
    signal librarySearchRequested()

    modal: true
    focus: true
    // The whole window, including the tab strip and tool bar.
    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    padding: 0
    // Its controls clear of the system's bars, a cut-out and the soft keyboard (Main.qml); its color under them
    readonly property bool inWindow: typeof win !== "undefined" && win !== null && win.insets !== undefined
    /// The buttons of a card (×, the star, the reference): a finger's size in the touch profile (audit F14)
    readonly property bool touch: inWindow && win.adaptive.touchProfile
    readonly property int cardTarget: touch ? win.adaptive.minTarget : 40
    topPadding: inWindow ? win.insets.top : 0
    leftPadding: inWindow ? win.insets.left : 0
    rightPadding: inWindow ? win.insets.right : 0
    bottomPadding: inWindow ? Math.max(win.insets.keyboardHeight, win.insets.bottom) : 0
    /// A card's name is being edited (Escape cancels that, it does not close the overview)
    property bool renaming: false
    closePolicy: renaming ? Popup.NoAutoClose : Popup.CloseOnEscape
    onAboutToShow: {
        grid.currentIndex = app.currentTab
        grid.forceActiveFocus()
    }

    background: Rectangle { color: "#eef0f3" }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }

    /// Changes when a star is set or taken away (the cards ask for theirs again)
    property int favouriteRevision: 0
    Connections {
        target: app
        function onFavouriteChanged() { overview.favouriteRevision++ }
    }
    readonly property bool searching: searchField.text !== ""
    /// Extended search: each document shows its pages with hits (as in the library)
    property bool extended: false
    /// The reduced search: the names of the open documents only, not their text
    property bool namesOnly: false
    readonly property bool extendedView: extended && searching && !namesOnly
    /// The fuzzy search (shared with the library)
    readonly property bool fuzzy: app.library.fuzzySearch
    onNamesOnlyChanged: runSearch(searchField.text)
    onFuzzyChanged: if (searching) runSearch(searchField.text)

    /// Search the text of the open documents - or, names only, nothing (the names are compared right here)
    function runSearch(text) { app.searchAllTabs(namesOnly ? "" : text) }
    function nameMatches(title) {
        if (!searching) return false
        if (fuzzy) return app.fuzzyName(searchField.text, title).match
        return title.toLowerCase().indexOf(searchField.text.trim().toLowerCase()) >= 0
    }

    function activate(index) {
        if (searching && !namesOnly) {
            app.openSearchResult(index)
        } else {
            app.currentTab = index
        }
        overview.close()
    }
    /// A page of the extended search: that document, at the first hit of that page
    function activatePage(index, page) {
        app.openSearchResultAt(index, page)
        overview.close()
    }

    Timer {
        id: searchTyping
        property bool pending: false  // short text typed, waiting for Enter
        interval: 300
        onTriggered: { pending = false; overview.runSearch(searchField.text) }
    }
    // The search shortcuts also while the overview is open (it is modal: the window's shortcuts are blocked)
    Shortcut {
        sequences: (app.shortcuts.revision, app.shortcuts.keys("searchAllDocuments"))
        enabled: overview.visible
        onActivated: overview.openSearch()
    }
    Shortcut {
        sequences: (app.shortcuts.revision, app.shortcuts.keys("searchLibrary"))
        enabled: overview.visible
        onActivated: overview.librarySearchRequested()
    }
    /// Open with the cursor in the search over all documents
    function openSearch() {
        if (!visible) open()
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    /// Open with a search for this text, run right away (selected text: the look-up menu)
    function searchFor(text) {
        if (!visible) open()
        searchTyping.stop()
        searchTyping.pending = false
        searchField.text = text
        runSearch(text)
        grid.forceActiveFocus()
    }
    /// Short texts (fewer than 4 characters) are searched on Enter or the search icon only, not while typing.
    function typed() {
        searchTyping.pending = searchField.text.length > 0 && searchField.text.length < 4
        if (!searchTyping.pending) searchTyping.restart()
        else searchTyping.stop()
    }

    // --- the layout for the window's size (qt/docs/adaptive-layout.md, "The home screen and the tab overview") ---
    readonly property var adaptive: typeof win !== "undefined" && win ? win.adaptive : null
    readonly property bool phoneLayout: adaptive !== null && ["phonePortrait", "phoneShort", "tiny"].indexOf(adaptive.layoutClass) >= 0
    readonly property bool shortLayout: phoneLayout && (adaptive.layoutClass === "phoneShort" || adaptive.orientation === "landscape")
    /// Below 600 px, and where the title, the search and the buttons do not fit side by side, the header wraps: the
    /// title and the buttons in one row, the search across the width below
    readonly property bool narrowHeader: (adaptive !== null && adaptive.widthClass === "compact")
                                         || width < overviewCount.implicitWidth + Math.min(380, width * 0.4) + 4 * 48 + 80
    /// The tallest page of the open documents (height / width): the cells follow it (asked when the overview opens
    /// and when documents come or go)
    readonly property real pageAspect: (visible, grid.count, app.tabs.tallestPageAspect())

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "overviewHeader"
            Layout.fillWidth: true
            Layout.leftMargin: overview.narrowHeader ? 16 : 24
            Layout.rightMargin: overview.narrowHeader ? 4 : 12
            Layout.topMargin: overview.shortLayout ? 4 : 10
            spacing: 4
            Label {
                id: overviewCount
                objectName: "overviewCount"
                text: grid.count === 1 ? qsTr("1 open document") : qsTr("%1 open documents").arg(grid.count)
                font.pixelSize: 20
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: overview.narrowHeader
                Layout.minimumWidth: 0
            }
            Item { Layout.fillWidth: true; visible: !overview.narrowHeader }
            // Search in all open documents (searchBox): here, or in a row of its own below in a narrow window
            Item {
                id: wideSearchSlot
                visible: !overview.narrowHeader
                Layout.preferredWidth: Math.min(380, overview.width * 0.4)
                Layout.preferredHeight: 44
            }
            IconButton {
                objectName: "overviewExtendedButton"
                iconName: "xqt-pages-grid"
                tip: qsTr("Extended search: show the pages with hits of every document")
                checked: overview.extended
                onClicked: overview.extended = !overview.extended
            }
            Item { Layout.fillWidth: true; visible: !overview.narrowHeader }
            IconButton {
                objectName: "overviewNewButton"
                iconName: "xopp-document-new"
                tip: qsTr("New document")
                onClicked: { app.newDocument(); overview.close() }
            }
            IconButton {
                objectName: "closeAllButton"
                iconName: "xqt-close-all"
                tip: qsTr("Close all documents")
                enabled: grid.count > 0
                onClicked: overview.closeAllRequested()
            }
            IconButton { objectName: "overviewBackButton"; iconName: "xqt-close"; tip: qsTr("Back"); onClicked: overview.close() }
        }
        Item {
            id: narrowSearchSlot
            visible: overview.narrowHeader
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 4
            Layout.preferredHeight: 44
        }

        GridView {
            id: grid
            objectName: "tabGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: overview.phoneLayout ? 8 : 16
            clip: true
            model: app.tabs
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            // At least two cards side by side on a phone (cells of 170 px and up), three or four held sideways
            readonly property int minCell: overview.extendedView ? 380 : overview.shortLayout ? 210 : overview.phoneLayout ? 170 : 280
            readonly property int columns: Math.max(overview.phoneLayout && !overview.extendedView ? 2 : 1, Math.floor(width / minCell))
            cellWidth: Math.floor(width / columns)
            readonly property int stripHeight: overview.extendedView ? Math.round(Math.max(120, cellWidth * 0.55)) : 0
            // As high as the page needs (its picture as wide as the card, the title below); held sideways never
            // higher than the grid, so a whole card shows
            cellHeight: overview.extendedView ? Math.round(cellWidth * 0.5 + 44 + stripHeight + 24)
                        : Math.round(Math.min(overview.shortLayout ? Math.max(160, height) : 100000,
                                              (cellWidth - 52) * overview.pageAspect + 84))
            ScrollBar.vertical: ScrollBar {}
            TouchpadMomentum { flickable: grid }

            Keys.onReturnPressed: overview.activate(currentIndex)
            Keys.onEnterPressed: overview.activate(currentIndex)
            Keys.onSpacePressed: overview.activate(currentIndex)
            Keys.onDeletePressed: overview.closeRequested(currentIndex)
            Keys.onPressed: function(event) {
                // Typing starts a search - a visible character only: Escape, Backspace, Delete and Tab have a
                // text of one (control) character too, and Escape is for closing the overview.
                const c = event.text.length === 1 ? event.text.charCodeAt(0) : 0
                if (c > 32 && c !== 127 && !(event.modifiers & Qt.ControlModifier)) {
                    searchField.forceActiveFocus()
                    searchField.text += event.text
                    overview.typed()
                    event.accepted = true
                }
            }

            delegate: Item {
                id: cell
                required property int index
                required property string title
                required property bool modified
                required property bool current
                required property bool isReference
                required property string thumbnail
                required property string sketch
                required property int pageCount
                required property int searchHits
                required property bool searchRunning
                required property bool searchMatch
                required property var hitPages
                required property string filePath
                /// A favourite (qt/docs/bookmarks.md): asked again when a star changes anywhere
                readonly property bool favourite: (overview.favouriteRevision, filePath !== "" && app.isFavouriteFile(filePath))
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool highlighted: GridView.isCurrentItem && grid.activeFocus
                // Searching: documents without hits step back (names only: those whose name does not match).
                readonly property bool hit: overview.namesOnly ? overview.nameMatches(title)
                                                               : overview.searching
                                                                 && (overview.fuzzy ? searchMatch : searchHits > 0)
                /// Fuzzy search: the letters of the title its query matched
                readonly property var titleMarks: overview.fuzzy && overview.searching
                                                  ? app.fuzzyName(searchField.text, title).marks : []
                readonly property string markedTitle: Fuzzy.marked(title, titleMarks, "#c2410c")
                opacity: overview.searching && !hit && (overview.namesOnly || !searchRunning) ? 0.45 : 1
                /// Edit its name in place (the name selected, the extension stays)
                function startRename() {
                    const info = app.tabRenameInfo(index)
                    grid.currentIndex = index
                    cellRename.extension = info.extension || ""
                    cellRename.start(info.name || "", info.problem)
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 10
                    radius: 12
                    color: "#ffffff"
                    border.width: cell.current || cell.highlighted || cell.hit ? 3 : 1
                    border.color: cell.hit ? "#f9a825" : (cell.current || cell.highlighted ? Material.accentColor : "#c9ccd1")

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#f4f5f7"
                            radius: 6
                            clip: true
                            // The sketch at once (never blank), the sharp picture on top when it is loaded
                            Image {
                                anchors.fill: parent
                                anchors.margins: 6
                                visible: cellPicture.status !== Image.Ready
                                fillMode: Image.PreserveAspectFit
                                source: cell.sketch
                                cache: false
                                smooth: true
                            }
                            Image {
                                id: cellPicture
                                anchors.fill: parent
                                anchors.margins: 6
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                // Only while the overview is open (the cards of many tabs would keep their pictures
                                // in memory); when it opens again the card shows its sketch until this is loaded
                                source: overview.visible ? cell.thumbnail : ""
                                sourceSize.width: Math.round(width)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Rectangle {
                                visible: cell.modified
                                width: 8; height: 8; radius: 4
                                color: Material.accentColor
                            }
                            Label {
                                id: cellTitle
                                objectName: "overviewTitle"
                                visible: !cellRename.active
                                Layout.fillWidth: true
                                text: cell.markedTitle !== "" ? cell.markedTitle : cell.title
                                textFormat: cell.markedTitle !== "" ? Text.StyledText : Text.AutoText
                                elide: Text.ElideMiddle
                                font.weight: cell.current ? Font.DemiBold : Font.Normal
                                // The title: a double click (mouse) or a press and hold edits the name in place
                                RenameGestures {
                                    objectName: "overviewTitleArea"
                                    anchors.fill: parent
                                    anchors.margins: -6  // (a finger's size)
                                    onTapped: overview.activate(cell.index)
                                    onRenameRequested: cell.startRename()
                                }
                            }
                            InlineRename {
                                id: cellRename
                                objectName: "overviewRename"
                                visible: active
                                Layout.fillWidth: true
                                check: function(name) { return app.tabRenameProblem(cell.index, name) }
                                onActiveChanged: overview.renaming = active
                                onAccepted: function(name) {
                                    app.renameTab(cell.index, name)
                                    grid.forceActiveFocus()
                                }
                                onCanceled: if (overview.visible) grid.forceActiveFocus()
                            }
                            Label {
                                visible: !overview.searching
                                text: cell.pageCount === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(cell.pageCount)
                                color: "#6b6f75"
                                font.pixelSize: 12
                            }
                            // Search result of this document
                            Rectangle {
                                visible: overview.searching
                                radius: 10
                                implicitWidth: hitLabel.implicitWidth + 16
                                implicitHeight: 22
                                color: cell.hit ? "#fff3c4" : "#eceef1"
                                Label {
                                    id: hitLabel
                                    objectName: "hitLabel"
                                    anchors.centerIn: parent
                                    font.pixelSize: 12
                                    font.weight: cell.hit ? Font.DemiBold : Font.Normal
                                    color: cell.hit ? "#7a5200" : "#6b6f75"
                                    text: overview.namesOnly ? (cell.hit ? qsTr("In the name") : qsTr("No hits"))
                                          : overview.fuzzy && !cell.hit && !cell.searchRunning ? qsTr("No match")
                                          : cell.searchHits > 0
                                          ? (cell.searchHits === 1 ? qsTr("1 hit") : qsTr("%1 hits").arg(cell.searchHits))
                                            + (cell.searchRunning ? "…" : "")
                                          : cell.searchRunning ? qsTr("Searching…")
                                          : cell.hit ? qsTr("In the name") : qsTr("No hits")
                                }
                            }
                        }
                        // Extended search: the pages with hits (as in the library), marked as in the page grid
                        ListView {
                            id: strip
                            objectName: "overviewHitPages"
                            visible: grid.stripHeight > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: grid.stripHeight
                            orientation: ListView.Horizontal
                            spacing: 6
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: grid.stripHeight > 0 ? cell.hitPages : []
                            cacheBuffer: Math.max(0, width)
                            ScrollBar.horizontal: ScrollBar { height: 6 }
                            Label {
                                anchors.centerIn: parent
                                visible: strip.count === 0
                                text: cell.searchRunning ? qsTr("Searching…") : qsTr("No hits")
                                color: "#80868b"
                            }
                            delegate: AbstractButton {
                                id: hitPage
                                required property var modelData
                                readonly property real thumbHeight: strip.height - 22
                                objectName: "overviewHitPage"
                                width: Math.max(24, Math.round(thumbHeight / (modelData.aspect > 0 ? modelData.aspect : 1.414)))
                                height: strip.height
                                onClicked: overview.activatePage(cell.index, modelData.page)
                                contentItem: Item {
                                    Rectangle {
                                        id: paper
                                        width: parent.width
                                        height: hitPage.thumbHeight
                                        color: "#ffffff"
                                        border.width: hitPage.hovered ? 2 : 1
                                        border.color: hitPage.hovered ? Material.accentColor : "#d5d8dc"
                                        Image {
                                            id: pageImage
                                            objectName: "overviewHitPagePicture"
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            asynchronous: true
                                            // Kept by QML (as in the library): finding another hit rebuilds this
                                            // list, and the pictures must not blink away
                                            source: hitPage.modelData.thumbnail
                                            sourceSize.width: Math.ceil(width)
                                        }
                                        Repeater {
                                            model: hitPage.modelData.rects
                                            delegate: Rectangle {
                                                required property var modelData
                                                x: 1 + modelData.x * (paper.width - 2) - 1
                                                y: 1 + modelData.y * (paper.height - 2) - 1
                                                width: Math.max(3, modelData.width * (paper.width - 2) + 2)
                                                height: Math.max(3, modelData.height * (paper.height - 2) + 2)
                                                radius: 1
                                                color: "#80ffd200"
                                                border.width: 1
                                                border.color: "#e0a800"
                                            }
                                        }
                                        HitBadge {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 3
                                            count: hitPage.modelData.count
                                        }
                                    }
                                    Label {
                                        anchors.top: paper.bottom
                                        anchors.topMargin: 2
                                        anchors.horizontalCenter: paper.horizontalCenter
                                        text: hitPage.modelData.page + 1
                                        font.pixelSize: 11
                                        color: "#5f6368"
                                    }
                                }
                                background: null
                            }
                        }
                    }
                    TapHandler {
                        onTapped: overview.activate(cell.index)
                    }
                    // Reference mode: shown beside the current document (again: not any more)
                    ToolButton {
                        objectName: "overviewReferenceButton"
                        visible: !overview.searching  // (the current document: beside itself, a second view)
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: 2
                        implicitWidth: overview.cardTarget
                        implicitHeight: overview.cardTarget
                        checkable: false
                        icon.source: app.iconUrl("xqt-reference")
                        icon.color: cell.isReference ? Material.accentColor : "#3c4043"
                        display: AbstractButton.IconOnly
                        background: Rectangle {
                            radius: 10
                            color: cell.isReference ? "#e0e3f5" : "transparent"
                        }
                        onClicked: {
                            if (cell.isReference) {
                                app.reference.close()
                            } else {
                                app.reference.showTab(cell.index)
                                app.homeVisible = false
                                overview.close()
                            }
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: cell.current ? (cell.isReference ? qsTr("Close the view beside")
                                                                       : qsTr("Show this document beside"))
                                     : cell.isReference ? qsTr("Close the reference") : qsTr("Open as reference")
                    }
                    // The star of a favourite: shown on starred documents (and on the others under the mouse)
                    ToolButton {
                        id: overviewStar
                        objectName: "overviewStar"
                        // (with the fingers, which do not hover: always)
                        visible: cell.filePath !== "" && (cell.favourite || cellHover.hovered || overview.touch)
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.rightMargin: overview.cardTarget + 2
                        anchors.topMargin: 2
                        implicitWidth: overview.cardTarget
                        implicitHeight: overview.cardTarget
                        icon.source: app.iconUrl(cell.favourite ? "xqt-star-filled" : "xqt-star")
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        onClicked: app.setFavouriteFile(cell.filePath, !cell.favourite)
                        ToolTip.visible: hovered
                        ToolTip.text: cell.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
                    }
                    HoverHandler { id: cellHover }
                    ToolButton {
                        objectName: "overviewCloseButton"
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        implicitWidth: overview.cardTarget
                        implicitHeight: overview.cardTarget
                        icon.source: app.iconUrl("xqt-close")
                        icon.color: "#3c4043"
                        display: AbstractButton.IconOnly
                        onClicked: overview.closeRequested(cell.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Close")
                    }
                }
            }
        }
    }

    // Search in all open documents: in the header, or in a row of its own below it in a narrow window
    Rectangle {
        id: searchBox
        parent: overview.narrowHeader ? narrowSearchSlot : wideSearchSlot
        anchors.fill: parent
        radius: 22
        color: "#ffffff"
        border.width: searchField.activeFocus ? 2 : 1
        border.color: searchField.activeFocus ? Material.accentColor : "#c9ccd1"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 2
            spacing: 2
            ToolButton {
                objectName: "overviewSearchButton"
                implicitWidth: 40; implicitHeight: 40
                icon.source: app.iconUrl("xqt-search")
                icon.color: "#3c4043"
                display: AbstractButton.IconOnly
                onClicked: { searchTyping.stop(); searchTyping.pending = false; overview.runSearch(searchField.text) }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Search (Enter)")
                ToolTip.delay: 600
            }
            TextField {
                id: searchField
                objectName: "overviewSearchField"
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                background: null
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.leftPadding
                    width: parent.width - parent.leftPadding - parent.rightPadding  // (a narrow field: …)
                    elide: Text.ElideRight
                    visible: parent.text === "" && parent.preeditText === ""
                    text: qsTr("Search all documents")
                    color: "#8a8d91"
                }
                selectByMouse: true
                onTextEdited: overview.typed()
                Keys.onReturnPressed: { searchTyping.stop(); searchTyping.pending = false; overview.runSearch(text); grid.forceActiveFocus() }
                Keys.onEnterPressed: { searchTyping.stop(); searchTyping.pending = false; overview.runSearch(text); grid.forceActiveFocus() }
                Keys.onDownPressed: grid.forceActiveFocus()
            }
            Label {
                visible: searchField.text !== "" && searchField.text.length < 4 && searchTyping.pending
                text: qsTr("Enter ↵")
                color: "#6b6f75"
                font.pixelSize: 12
            }
            // Fuzzy search: an expression that is not valid is searched as plain text, and says why
            Label {
                id: syntaxHint
                objectName: "overviewSyntaxHint"
                readonly property string hint: overview.fuzzy ? app.fuzzyHint(searchField.text) : ""
                visible: hint !== ""
                Layout.maximumWidth: 150
                text: hint
                elide: Text.ElideRight
                color: "#b3261e"
                font.pixelSize: 12
                ToolTip.visible: syntaxHover.hovered
                ToolTip.text: qsTr("%1 - searched as plain text").arg(hint)
                ToolTip.delay: 300
                HoverHandler { id: syntaxHover }
            }
            FuzzyToggle { objectName: "overviewSearchFuzzy"; implicitHeight: overview.cardTarget }
            // The reduced search: names only (as in the library)
            ToolButton {
                id: namesOnlyButton
                objectName: "overviewNamesOnly"
                text: qsTr("Names")
                checkable: true
                checked: overview.namesOnly
                onToggled: overview.namesOnly = checked
                implicitHeight: overview.cardTarget
                leftPadding: 6
                rightPadding: 6
                font.pixelSize: 13
                font.weight: checked ? Font.DemiBold : Font.Normal
                Material.foreground: checked ? Material.accentColor : "#5f6368"
                ToolTip.visible: hovered
                ToolTip.text: checked ? qsTr("Searching the names only - tap to search the text too")
                                      : qsTr("Search the names of the open documents only")
                ToolTip.delay: 600
                background: Rectangle {
                    radius: 10
                    color: namesOnlyButton.checked ? "#e0e3f5" : (namesOnlyButton.pressed ? "#e8e8e8" : "transparent")
                }
            }
            ToolButton {
                objectName: "overviewSearchClear"
                visible: searchField.text !== ""
                implicitWidth: 40; implicitHeight: 40
                Accessible.name: qsTr("Clear the search")
                icon.source: app.iconUrl("xqt-close")
                icon.color: "#3c4043"
                display: AbstractButton.IconOnly
                onClicked: { searchField.text = ""; searchTyping.stop(); searchTyping.pending = false; overview.runSearch("") }
            }
        }
    }
}
