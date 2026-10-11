// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z and parent are the window's.
//
// The presenter's console on the laptop's screen (qt/docs/features/presenter-view.md): presenting as on one screen,
// with this small pill at the top left corner of the page. Folded it shows only the minutes since the start ("12 min")
// and the button of all pages (the page grid, G); a tap on the minutes unfolds it: the time of day, the time since the
// start with pause / go on and back to 0:00, "Page 3 of 20" and the next page smaller, the notes for the audience,
// the audience following the zoom and Fit, which screen is the audience's, swap screens, End. A tap beside it (on the
// page: it writes nothing then), Esc or Back folds it again (WindowShortcuts: the first step). Only the console has
// it: the audience never sees it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: pill
    objectName: "presenterPill"
    property QtObject presenter: app.presenter
    /// Unfolded: the time, the page, the next page and the console's settings
    property bool expanded: false
    signal stopRequested()
    signal pagesRequested()
    visible: win.modes.presenterConsole && !app.homeVisible
    onVisibleChanged: if (!visible) expanded = false
    z: 93
    // At the top left of the page (the slide's corner rarely holds what is pointed at; the toolbox floats at another
    // edge by default), clear of the toolbox where it floats at the left or the top
    readonly property bool railLeft: toolboxPane.visible && toolboxPane.floating && toolboxPane.edge === "left"
    readonly property bool railTop: toolboxPane.visible && toolboxPane.floating && toolboxPane.edge === "top"
                                    && toolboxPane.x < 16 + width + win.layout.canvasControlsLeft
    x: (railLeft ? toolboxPane.x + toolboxPane.width : win.layout.canvasControlsLeft) + 12
    y: (railTop ? toolboxPane.y + toolboxPane.height : win.layout.canvasControlsTop) + 12
    padding: 2
    Material.foreground: "#303030"
    background: Rectangle {
        radius: pill.expanded ? 16 : height / 2
        color: "#f7fafafa"
        border.width: 1
        border.color: "#40000000"
    }
    /// A finger's target in the touch profile
    readonly property real target: Math.max(44, win.adaptive.minTarget)

    /// The time of day, read again every second while it shows
    property date now: new Date()
    Timer {
        interval: 1000
        repeat: true
        running: pill.visible && pill.expanded
        triggeredOnStart: true
        onTriggered: pill.now = new Date()
    }
    function twoDigits(n) { return (n < 10 ? "0" : "") + n }
    /// "4:05", "1:02:03"
    function duration(seconds) {
        const h = Math.floor(seconds / 3600)
        const m = Math.floor(seconds / 60) % 60
        const s = seconds % 60
        return h > 0 ? h + ":" + twoDigits(m) + ":" + twoDigits(s) : m + ":" + twoDigits(s)
    }
    /// Folded: the whole minutes only ("0 min", "12 min", "1 h 05 min"), nothing that changes every second
    function minutes(seconds) {
        const m = Math.floor(seconds / 60)
        return m < 60 ? qsTr("%1 min").arg(m) : qsTr("%1 h %2 min").arg(Math.floor(m / 60)).arg(twoDigits(m % 60))
    }

    ColumnLayout {
        spacing: 0
        // Folded: the minutes (a tap unfolds it) and all pages
        RowLayout {
            spacing: 0
            ToolButton {
                id: toggle
                objectName: "presenterPillToggle"
                implicitHeight: pill.target
                leftPadding: 12
                rightPadding: 10
                focusPolicy: Qt.NoFocus
                display: AbstractButton.TextBesideIcon
                icon.source: app.iconUrl(pill.presenter.timerRunning ? "xqt-clock-light" : "xopp-audio-playback-pause")
                icon.width: 18
                icon.height: 18
                icon.color: pill.presenter.timerRunning ? "#303030" : "#80868b"
                text: pill.minutes(pill.presenter.elapsedSeconds)
                font.pixelSize: 15
                font.features: { "tnum": 1 }
                Material.foreground: pill.presenter.timerRunning ? "#303030" : "#80868b"
                checked: pill.expanded
                onClicked: pill.expanded = !pill.expanded
                Accessible.name: qsTr("The time since the start, the next page and the presenter's settings")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: pill.expanded ? qsTr("Fold (Esc)")
                                            : qsTr("The time since the start, the next page and the presenter's settings")
            }
            Rectangle { implicitWidth: 1; implicitHeight: 24; color: "#d5d8dc" }
            IconButton {
                objectName: "presenterGridButton"
                implicitWidth: pill.target
                implicitHeight: pill.target
                icon.width: 22
                icon.height: 22
                iconName: "xqt-pages-grid"
                checked: pageGrid.visible
                label: qsTr("All pages")
                tip: qsTr("All pages: tap one to go there (G)")
                onClicked: pill.pagesRequested()
            }
        }

        // Unfolded
        ColumnLayout {
            objectName: "presenterPillDetails"
            visible: pill.expanded
            spacing: 6
            Layout.preferredWidth: 360
            Layout.leftMargin: 12
            Layout.rightMargin: 10
            Layout.topMargin: 4
            Layout.bottomMargin: 10

            Label {
                objectName: "presenterClock"
                text: Qt.formatTime(pill.now, Qt.locale().timeFormat(Locale.ShortFormat))
                font.pixelSize: 28
            }
            RowLayout {
                spacing: 2
                Label {
                    objectName: "presenterElapsed"
                    text: pill.duration(pill.presenter.elapsedSeconds)
                    font.pixelSize: 22
                    font.features: { "tnum": 1 }
                    color: pill.presenter.timerRunning ? "#303030" : "#80868b"
                    Layout.fillWidth: true
                }
                IconButton {
                    objectName: "presenterTimerToggle"
                    implicitWidth: pill.target
                    implicitHeight: pill.target
                    icon.width: 22
                    icon.height: 22
                    iconName: pill.presenter.timerRunning ? "xopp-audio-playback-pause" : "xopp-object-play"
                    tip: pill.presenter.timerRunning ? qsTr("Pause the time") : qsTr("Go on with the time")
                    onClicked: pill.presenter.toggleTimer()
                }
                IconButton {
                    objectName: "presenterTimerReset"
                    implicitWidth: pill.target
                    implicitHeight: pill.target
                    icon.width: 22
                    icon.height: 22
                    iconName: "xqt-rotate-left"
                    tip: qsTr("Back to 0:00")
                    onClicked: pill.presenter.resetTimer()
                }
            }
            Label {
                objectName: "presenterPageLabel"
                text: qsTr("Page %1 of %2").arg(pill.presenter.page + 1).arg(pill.presenter.pageCount)
                font.pixelSize: 16
            }
            Label {
                objectName: "presenterNextLabel"
                text: pill.presenter.nextPicture !== "" ? qsTr("Next") : qsTr("The last page")
                color: "#5f6368"
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(width * pill.presenter.nextAspect, win.height * 0.3)
                visible: pill.presenter.nextPicture !== ""
                // (a white page on the pill: a thin frame around it)
                Rectangle {
                    width: nextPicture.paintedWidth + 2
                    height: nextPicture.paintedHeight + 2
                    x: -1
                    y: -1
                    color: "transparent"
                    border.width: 1
                    border.color: "#c5c8cc"
                    visible: nextPicture.status === Image.Ready
                }
                Image {
                    id: nextPicture
                    objectName: "presenterNext"
                    anchors.fill: parent
                    source: pill.expanded ? pill.presenter.nextPicture : ""
                    asynchronous: true
                    cache: false
                    sourceSize.width: Math.round(width)
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignLeft
                    verticalAlignment: Image.AlignTop
                    smooth: true
                }
            }
            Label {
                objectName: "presenterNotesHint"
                visible: pill.presenter.pageHasNotes
                text: pill.presenter.showNotes ? qsTr("The audience sees the space for notes too.")
                                               : qsTr("The space for notes shows only here, not to the audience.")
                wrapMode: Text.WordWrap
                color: "#5f6368"
                Layout.fillWidth: true
            }
            // The audience sees the whole page with its space for notes (and the ink written there), not only the
            // slide (a setting: Settings → Pen → Presenting)
            Switch {
                objectName: "presenterShowNotes"
                text: qsTr("Notes for the audience too")
                checked: pill.presenter.showNotes
                focusPolicy: Qt.NoFocus
                padding: 0
                implicitHeight: pill.target
                onToggled: app.settings.set("presenterShowNotes", checked)
                Layout.fillWidth: true
            }
            // Zooming and scrolling the page here shows the audience the same part (a frame on the page shows what
            // they see; off by default: the audience keeps the whole slide); "Fit": both back to the whole slide
            RowLayout {
                spacing: 4
                Switch {
                    objectName: "presenterFollowView"
                    text: qsTr("The audience follows my zoom")
                    checked: pill.presenter.followView
                    focusPolicy: Qt.NoFocus
                    padding: 0
                    implicitHeight: pill.target
                    onToggled: app.settings.set("presenterFollowView", checked)
                    Layout.fillWidth: true
                }
                Button {
                    objectName: "presenterFit"
                    text: qsTr("Fit")
                    focusPolicy: Qt.NoFocus
                    flat: true
                    onClicked: pill.presenter.fitPage()
                    ToolTip.visible: hovered
                    ToolTip.text: pill.presenter.followView
                                  ? (pill.presenter.showNotes ? qsTr("The whole page on both screens")
                                                              : qsTr("The whole slide on both screens"))
                                  : qsTr("The whole page here")
                    Accessible.name: ToolTip.text
                }
            }
            Label {
                text: qsTr("Audience: %1").arg(pill.presenter.audienceScreenName)
                visible: pill.presenter.audienceScreenName !== ""
                elide: Text.ElideRight
                color: "#5f6368"
                Layout.fillWidth: true
            }
            RowLayout {
                spacing: 8
                Button {
                    objectName: "presenterSwapScreens"
                    text: qsTr("Swap screens")
                    icon.source: app.iconUrl("xqt-swap-sides")
                    focusPolicy: Qt.NoFocus
                    flat: true
                    onClicked: app.settings.set("presenterSwapScreens", !pill.presenter.swapScreens)
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "presenterStop"
                    text: qsTr("End")
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        pill.expanded = false
                        pill.stopRequested()
                    }
                }
            }
        }
    }
}
