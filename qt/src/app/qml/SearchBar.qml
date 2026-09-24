// Search in the current document: a floating bar over the canvas (Ctrl+F). Enter / Shift+Enter or the arrows go to
// the next / previous hit; Escape or × ends the search. Short texts (fewer than 4 characters: thousands of hits in a
// long document) are only searched on Enter or a tap on the search icon, not while typing.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: bar
    property bool open: false
    visible: open || app.searchQuery !== ""
    padding: 4
    leftPadding: 12
    rightPadding: 4
    Material.foreground: "#303030"
    background: Rectangle {
        radius: height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }

    function openBar() {
        open = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function closeBar() {
        open = false
        typing.stop()
        sent = ""
        app.clearSearch()
        field.text = ""
    }

    /// The text this bar searched for last. What is typed is never written back from the search: results of the
    /// text searched before may come in while typing goes on (the field used to be bound to the query, and every
    /// result set it back, dropping the letters typed meanwhile).
    property string sent: ""
    function send(text) {
        sent = text
        app.searchQuery = text
    }
    // The field follows the current tab's search when it was changed elsewhere (tab switches, search from the tab
    // overview or the library), not the results of its own.
    Connections {
        target: app
        function onSearchChanged() {
            if (app.searchQuery !== bar.sent) {
                typing.stop()
                bar.sent = app.searchQuery
                field.text = app.searchQuery
            }
        }
    }
    Timer {
        id: typing
        interval: 150
        onTriggered: bar.send(field.text)
    }
    readonly property int liveSearchLength: 4
    /// The text in the field waits for Enter (too short to search while typing).
    readonly property bool waitingForEnter: field.text !== "" && field.text !== app.searchQuery
                                            && field.text.length < liveSearchLength
    function searchNow() {
        typing.stop()
        send(field.text)
    }

    RowLayout {
        spacing: 2
        IconButton {
            objectName: "searchNowButton"
            iconName: "xqt-search"
            tip: qsTr("Search (Enter)")
            implicitWidth: 36; implicitHeight: 36
            icon.width: 20; icon.height: 20
            checked: bar.waitingForEnter
            onClicked: bar.searchNow()
        }
        TextField {
            id: field
            objectName: "searchField"
            Layout.preferredWidth: 240
            selectByMouse: true
            background: null
            // A plain hint instead of the Material style's floating placeholder.
            Label {
                anchors.verticalCenter: parent.verticalCenter
                x: parent.leftPadding
                visible: parent.text === "" && parent.preeditText === ""
                text: qsTr("Search in document")
                color: "#8a8d91"
            }
            onTextEdited: {
                if (text === "" || text.length >= bar.liveSearchLength) typing.restart()
                else typing.stop()  // short: on Enter only
            }
            Keys.onReturnPressed: function(event) { go(event) }
            Keys.onEnterPressed: function(event) { go(event) }
            Keys.onEscapePressed: bar.closeBar()
            function go(event) {
                if (typing.running || app.searchQuery !== text) {
                    typing.stop()
                    bar.send(text)  // first Enter: search now
                } else if (event.modifiers & Qt.ShiftModifier) {
                    app.searchPrevious()
                } else {
                    app.searchNext()
                }
            }
        }
        Label {
            objectName: "searchStatus"
            Layout.minimumWidth: 76
            horizontalAlignment: Text.AlignHCenter
            color: app.searchQuery !== "" && app.searchHitCount === 0 && !app.searchRunning ? "#b3261e" : "#505050"
            text: bar.waitingForEnter ? qsTr("Enter ↵")
                : app.searchQuery === "" ? ""
                : app.searchHitCount === 0 ? (app.searchRunning ? qsTr("Searching…") : qsTr("No results"))
                : (app.searchCurrent > 0 ? app.searchCurrent + " / " : "") + app.searchHitCount
                  + (app.searchRunning ? "…" : "")
        }
        IconButton {
            iconName: "xqt-chevron-up"; tip: qsTr("Previous (Shift+Enter)")
            implicitWidth: 40; implicitHeight: 40
            enabled: app.searchHitCount > 0
            onClicked: app.searchPrevious()
        }
        IconButton {
            iconName: "xqt-chevron-down"; tip: qsTr("Next (Enter)")
            implicitWidth: 40; implicitHeight: 40
            enabled: app.searchHitCount > 0
            onClicked: app.searchNext()
        }
        IconButton {
            iconName: "xqt-close"; tip: qsTr("Close (Esc)")
            implicitWidth: 40; implicitHeight: 40
            onClicked: bar.closeBar()
        }
    }
}
