// Search in the current document: a floating bar over the canvas (Ctrl+F). Enter / Shift+Enter or the arrows go to
// the next / previous hit; Escape or × ends the search. Short texts (fewer than 4 characters: thousands of hits in a
// long document) are only searched on Enter or a tap on the search icon, not while typing. "Fuzzy" reads the text with
// the fuzzy search's syntax (FuzzyToggle): it shows the mode of the document's search (on for one handed over from
// the library or the tab overview with Fuzzy on), and a tap sets the app-wide setting and searches the text again.
//
// Find and replace (Ctrl+H, the bar's replace button, ⋮; qt/docs/md-editor.md "Find and replace"): a second row, only
// where text can be written (app.canReplace: a .md or .txt edited, Markdown text on pages), with the options (case,
// whole words, regular expression: the search takes them while the row is shown), Replace (the current hit, then the
// next; Enter in its field) and Replace all (one undo step, Ctrl+Enter; a snackbar says how many). While the source
// is written beside the page (`sourcePanel`), they work on that source.
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
        radius: bar.replaceShown ? 22 : height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }

    /// The replace row was asked for (Ctrl+H, the replace button); it shows where text can be written
    property bool replaceOpen: false
    /// Replacing is possible here (Main.qml: not while reading)
    property bool replaceAllowed: true
    readonly property bool replaceShown: visible && replaceOpen && replaceAllowed && app.canReplace
    /// The source beside the page (MarkdownPanel) while it is open: Replace works on it
    property var sourcePanel: null
    /// Something to tell (Main.qml: a snackbar); `undo`: it can be undone (Replace all)
    signal notice(string text, bool undo)

    /// An option of the replace row (on or off)
    component OptionButton: ToolButton {
        property string tip
        checkable: true
        focusPolicy: Qt.NoFocus  // (the keys stay with the fields)
        // (a finger's size in the touch profile, as the search button)
        implicitWidth: typeof win !== "undefined" && win && win.adaptive.touchProfile ? win.adaptive.minTarget : 36
        implicitHeight: implicitWidth
        font.pixelSize: 13
        font.weight: Font.DemiBold
        Material.foreground: checked ? Material.accentColor : "#505050"
        ToolTip.visible: hovered
        ToolTip.text: tip
        ToolTip.delay: 600
    }
    onReplaceShownChanged: app.replacing = replaceShown

    function openBar() {
        open = true
        field.forceActiveFocus()
        field.selectAll()
    }
    /// Ctrl+H: the bar with its replace row (the field to replace with when there is a text to find)
    function openReplace() {
        open = true
        replaceOpen = true
        if (field.text === "" || !replaceShown) {
            field.forceActiveFocus()
            field.selectAll()
        } else {
            replaceField.forceActiveFocus()
            replaceField.selectAll()
        }
    }
    function closeBar() {
        open = false
        replaceOpen = false
        typing.stop()
        sent = ""
        app.clearSearch()
        field.text = ""
    }

    /// The text in the field searched for, if it is not yet (Replace acts on what is typed)
    function searchTyped() {
        if (typing.running || app.searchQuery !== field.text) searchNow()
    }
    /// Replace: the current hit, then the next
    function replaceOne() {
        searchTyped()
        if (app.searchQuery === "") return
        if (sourcePanel) {
            const r = sourcePanel.replaceInSource(replaceField.text, false)
            if (!r || (r.count === 0 && !r.replaced)) notice(qsTr("No match in the source"), false)
            return
        }
        const r = app.replaceCurrent(replaceField.text)
        if (r === "skipped") notice(qsTr("Skipped a hit that is not written text (PDF text, handwriting)"), false)
        else if (r === "" && app.searchHitCount === 0 && !app.searchRunning) notice(qsTr("No results"), false)
    }
    /// Replace all: one undo step, and how many
    function replaceEvery() {
        searchTyped()
        if (app.searchQuery === "") return
        const n = sourcePanel ? (sourcePanel.replaceInSource(replaceField.text, true) || {}).count || 0
                              : app.replaceAll(replaceField.text)
        notice(n > 0 ? qsTr("Replaced %n time(s)", "", n) : qsTr("Nothing replaced"), n > 0 && !sourcePanel)
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 2
        RowLayout {
            Layout.fillWidth: true  // (a narrow window: the field gives way)
            spacing: 2
            IconButton {
                objectName: "searchNowButton"
                iconName: "xqt-search"
                tip: qsTr("Search (Enter)")
                // (a finger's size in the touch profile: audit F14)
                implicitWidth: typeof win !== "undefined" && win && win.adaptive.touchProfile ? win.adaptive.minTarget : 36
                implicitHeight: implicitWidth
                icon.width: 20; icon.height: 20
                checked: bar.waitingForEnter
                onClicked: bar.searchNow()
            }
            TextField {
                id: field
                objectName: "searchField"
                Layout.preferredWidth: 240
                Layout.minimumWidth: 64
                Layout.fillWidth: true
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
            // Fuzzy search: an expression that is not valid is searched as plain text, and says why
            Label {
                objectName: "searchSyntaxHint"
                visible: app.searchHint !== ""
                Layout.maximumWidth: 110
                text: app.searchHint
                elide: Text.ElideRight
                color: "#b3261e"
                font.pixelSize: 12
                ToolTip.visible: syntaxHover.hovered
                ToolTip.text: (bar.replaceShown && app.searchRegex ? qsTr("%1 - not searched") : qsTr("%1 - searched as plain text"))
                              .arg(app.searchHint)
                ToolTip.delay: 300
                HoverHandler { id: syntaxHover }
            }
            FuzzyToggle {
                objectName: "searchFuzzy"
                visible: !bar.replaceShown  // (replacing: plain text, with the options of its row)
                fuzzy: app.searchFuzzy
                setFuzzy: function(on) { app.searchFuzzy = on }
                focusPolicy: Qt.NoFocus  // (Enter and Escape stay the field's)
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
            // The replace row (where text can be written)
            IconButton {
                objectName: "replaceToggle"
                iconName: "xqt-replace"; tip: qsTr("Find and replace") + win.keyNote("replace")
                label: qsTr("Replace")
                visible: bar.replaceAllowed && app.canReplace
                implicitWidth: 40; implicitHeight: 40
                checked: bar.replaceShown
                onClicked: bar.replaceOpen ? (bar.replaceOpen = false) : bar.openReplace()
            }
            IconButton {
                iconName: "xqt-close"; tip: qsTr("Close (Esc)")
                implicitWidth: 40; implicitHeight: 40
                onClicked: bar.closeBar()
            }
        }
        RowLayout {
            objectName: "replaceRow"
            visible: bar.replaceShown
            Layout.fillWidth: true
            spacing: 2
            TextField {
                id: replaceField
                objectName: "replaceField"
                Layout.preferredWidth: 200
                Layout.minimumWidth: 64
                Layout.fillWidth: true
                Layout.leftMargin: 38  // (under the search field)
                selectByMouse: true
                background: null
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.leftPadding
                    visible: parent.text === "" && parent.preeditText === ""
                    text: qsTr("Replace with")
                    color: "#8a8d91"
                }
                Keys.onReturnPressed: function(event) { go(event) }
                Keys.onEnterPressed: function(event) { go(event) }
                Keys.onEscapePressed: bar.closeBar()
                function go(event) {
                    if (event.modifiers & Qt.ControlModifier) bar.replaceEvery()
                    else bar.replaceOne()
                }
            }
            // The options: the search takes them too while the row is shown
            OptionButton {
                objectName: "replaceCaseSensitive"
                text: "Aa"
                tip: qsTr("Match case")
                checked: app.searchCaseSensitive
                onToggled: app.searchCaseSensitive = checked
            }
            OptionButton {
                objectName: "replaceWholeWord"
                text: "ab"
                font.underline: true
                tip: qsTr("Whole words")
                checked: app.searchWholeWord
                onToggled: app.searchWholeWord = checked
            }
            OptionButton {
                objectName: "replaceRegex"
                text: ".*"
                tip: qsTr("Regular expression ($1 in the replacement: its first group)")
                checked: app.searchRegex
                onToggled: app.searchRegex = checked
            }
            Button {
                objectName: "replaceOneButton"
                text: qsTr("Replace")
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: field.text !== ""
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Replace this hit and go to the next (Enter)")
                ToolTip.delay: 600
                onClicked: bar.replaceOne()
            }
            Button {
                objectName: "replaceAllButton"
                text: qsTr("All")
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: field.text !== ""
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Replace all (Ctrl+Enter), one undo step")
                ToolTip.delay: 600
                onClicked: bar.replaceEvery()
            }
        }
    }
}
