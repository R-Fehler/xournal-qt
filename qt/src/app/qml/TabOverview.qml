// Overview of all open documents (tabs) as a grid of cards with the current page of each: tap a card to switch to
// it, × to close it, + for a new document. Keyboard: arrows, Enter, Delete, Escape.
// The search field searches all open documents: documents with hits are marked; opening one shows its hits (the
// document's own search, from its current page on).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: overview
    signal closeRequested(int index)
    signal closeAllRequested()

    modal: true
    focus: true
    // The whole window, including the tab strip and tool bar.
    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    padding: 0
    closePolicy: Popup.CloseOnEscape
    // Thumbnails are rendered again each time the overview opens (their URLs change with this counter).
    property int generation: 0
    onAboutToShow: {
        generation++
        grid.currentIndex = app.currentTab
        grid.forceActiveFocus()
    }

    background: Rectangle { color: "#eef0f3" }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }

    readonly property bool searching: searchField.text !== ""

    function activate(index) {
        if (searching) {
            app.openSearchResult(index)
        } else {
            app.currentTab = index
        }
        overview.close()
    }

    Timer {
        id: searchTyping
        property bool pending: false  // short text typed, waiting for Enter
        interval: 300
        onTriggered: { pending = false; app.searchAllTabs(searchField.text) }
    }
    /// Short texts (fewer than 4 characters) are searched on Enter or the search icon only, not while typing.
    function typed() {
        searchTyping.pending = searchField.text.length > 0 && searchField.text.length < 4
        if (!searchTyping.pending) searchTyping.restart()
        else searchTyping.stop()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Label {
                text: grid.count === 1 ? qsTr("1 open document") : qsTr("%1 open documents").arg(grid.count)
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            // Search in all open documents
            Rectangle {
                Layout.preferredWidth: Math.min(380, overview.width * 0.4)
                Layout.preferredHeight: 44
                radius: 22
                color: "#ffffff"
                border.width: searchField.activeFocus ? 2 : 1
                border.color: searchField.activeFocus ? Material.accentColor : "#c9ccd1"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 4
                    ToolButton {
                        implicitWidth: 36; implicitHeight: 36
                        icon.source: app.iconUrl("xqt-search")
                        icon.color: "#3c4043"
                        display: AbstractButton.IconOnly
                        onClicked: { searchTyping.stop(); searchTyping.pending = false; app.searchAllTabs(searchField.text) }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Search (Enter)")
                        ToolTip.delay: 600
                    }
                    TextField {
                        id: searchField
                        objectName: "overviewSearchField"
                        Layout.fillWidth: true
                        background: null
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.leftPadding
                            visible: parent.text === "" && parent.preeditText === ""
                            text: qsTr("Search all documents")
                            color: "#8a8d91"
                        }
                        selectByMouse: true
                        onTextEdited: overview.typed()
                        Keys.onReturnPressed: { searchTyping.stop(); searchTyping.pending = false; app.searchAllTabs(text); grid.forceActiveFocus() }
                        Keys.onEnterPressed: { searchTyping.stop(); searchTyping.pending = false; app.searchAllTabs(text); grid.forceActiveFocus() }
                        Keys.onDownPressed: grid.forceActiveFocus()
                    }
                    Label {
                        visible: searchField.text !== "" && searchField.text.length < 4 && searchTyping.pending
                        text: qsTr("Enter ↵")
                        color: "#6b6f75"
                        font.pixelSize: 12
                    }
                    ToolButton {
                        visible: searchField.text !== ""
                        implicitWidth: 36; implicitHeight: 36
                        icon.source: app.iconUrl("xqt-close")
                        icon.color: "#3c4043"
                        display: AbstractButton.IconOnly
                        onClicked: { searchField.text = ""; searchTyping.stop(); searchTyping.pending = false; app.searchAllTabs("") }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
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
            IconButton { iconName: "xqt-close"; tip: qsTr("Back"); onClicked: overview.close() }
        }

        GridView {
            id: grid
            objectName: "tabGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            clip: true
            model: app.tabs
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(1, Math.floor(width / 280))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round(cellWidth * 1.25)
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
                required property string thumbnail
                required property int pageCount
                required property int searchHits
                required property bool searchRunning
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool highlighted: GridView.isCurrentItem && grid.activeFocus
                // Searching: documents without hits step back.
                readonly property bool hit: overview.searching && searchHits > 0
                opacity: overview.searching && searchHits === 0 && !searchRunning ? 0.45 : 1

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
                            Image {
                                anchors.fill: parent
                                anchors.margins: 6
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                source: overview.visible ? cell.thumbnail + "/" + overview.generation : ""
                                sourceSize.width: Math.round(width * Screen.devicePixelRatio)
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
                                Layout.fillWidth: true
                                text: cell.title
                                elide: Text.ElideMiddle
                                font.weight: cell.current ? Font.DemiBold : Font.Normal
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
                                color: cell.searchHits > 0 ? "#fff3c4" : "#eceef1"
                                Label {
                                    id: hitLabel
                                    objectName: "hitLabel"
                                    anchors.centerIn: parent
                                    font.pixelSize: 12
                                    font.weight: cell.searchHits > 0 ? Font.DemiBold : Font.Normal
                                    color: cell.searchHits > 0 ? "#7a5200" : "#6b6f75"
                                    text: cell.searchHits > 0
                                          ? (cell.searchHits === 1 ? qsTr("1 hit") : qsTr("%1 hits").arg(cell.searchHits))
                                            + (cell.searchRunning ? "…" : "")
                                          : (cell.searchRunning ? qsTr("Searching…") : qsTr("No hits"))
                                }
                            }
                        }
                    }
                    TapHandler {
                        onTapped: overview.activate(cell.index)
                    }
                    ToolButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        implicitWidth: 40
                        implicitHeight: 40
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
}
