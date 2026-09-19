// Search in the current document: a floating bar over the canvas (Ctrl+F). Enter / Shift+Enter or the arrows go to
// the next / previous hit; Escape or × ends the search.
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
        app.clearSearch()
        field.text = ""
    }

    // The field follows the current tab's search (tab switches, search from the tab overview).
    Connections {
        target: app
        function onSearchChanged() {
            if (!field.activeFocus && field.text !== app.searchQuery) field.text = app.searchQuery
        }
    }
    Timer {
        id: typing
        interval: 250
        onTriggered: app.searchQuery = field.text
    }

    RowLayout {
        spacing: 2
        Image {
            source: app.iconUrl("xqt-search")
            sourceSize.width: 20
            sourceSize.height: 20
            Layout.rightMargin: 4
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
            text: app.searchQuery
            onTextEdited: typing.restart()
            Keys.onReturnPressed: function(event) { go(event) }
            Keys.onEnterPressed: function(event) { go(event) }
            Keys.onEscapePressed: bar.closeBar()
            function go(event) {
                if (typing.running || app.searchQuery !== text) {
                    typing.stop()
                    app.searchQuery = text  // first Enter: search now
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
            text: app.searchQuery === "" ? ""
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
