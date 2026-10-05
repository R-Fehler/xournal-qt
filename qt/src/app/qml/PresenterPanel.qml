// The presenter's console beside the current page (qt/docs/presenter-view.md): the clock, the time since the start
// (paused, resumed, reset), the page number, the next page smaller, swap screens and end. The current page itself is
// the window's canvas at its left, with its space for notes; the audience's screen shows only the slide.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "presenterPanel"
    property QtObject presenter: app.presenter
    signal stopRequested()
    color: "#202124"
    Material.theme: Material.Dark

    /// The time of day, read again every second while the console shows
    property date now: new Date()
    Timer {
        interval: 1000
        repeat: true
        running: panel.visible
        triggeredOnStart: true
        onTriggered: panel.now = new Date()
    }
    function twoDigits(n) { return (n < 10 ? "0" : "") + n }
    /// "4:05", "1:02:03"
    function duration(seconds) {
        const h = Math.floor(seconds / 3600)
        const m = Math.floor(seconds / 60) % 60
        const s = seconds % 60
        return h > 0 ? h + ":" + twoDigits(m) + ":" + twoDigits(s) : m + ":" + twoDigits(s)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            objectName: "presenterClock"
            text: Qt.formatTime(panel.now, Qt.locale().timeFormat(Locale.ShortFormat))
            font.pixelSize: 34
            color: "#e8eaed"
        }
        RowLayout {
            spacing: 4
            Label {
                objectName: "presenterElapsed"
                text: panel.duration(panel.presenter.elapsedSeconds)
                font.pixelSize: 26
                color: panel.presenter.timerRunning ? "#e8eaed" : "#9aa0a6"
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "presenterTimerToggle"
                icon.source: app.iconUrl(panel.presenter.timerRunning ? "xopp-audio-playback-pause" : "xopp-object-play")
                focusPolicy: Qt.NoFocus
                onClicked: panel.presenter.toggleTimer()
                ToolTip.visible: hovered
                ToolTip.text: panel.presenter.timerRunning ? qsTr("Pause the time") : qsTr("Go on with the time")
                Accessible.name: ToolTip.text
            }
            ToolButton {
                objectName: "presenterTimerReset"
                icon.source: app.iconUrl("xqt-rotate-left")
                focusPolicy: Qt.NoFocus
                onClicked: panel.presenter.resetTimer()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to 0:00")
                Accessible.name: ToolTip.text
            }
        }
        Label {
            objectName: "presenterPageLabel"
            text: qsTr("Page %1 of %2").arg(panel.presenter.page + 1).arg(panel.presenter.pageCount)
            font.pixelSize: 18
            color: "#e8eaed"
        }
        Label {
            objectName: "presenterNextLabel"
            text: panel.presenter.nextPicture !== "" ? qsTr("Next") : qsTr("The last page")
            color: "#9aa0a6"
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width * panel.presenter.nextAspect, panel.height * 0.45)
            visible: panel.presenter.nextPicture !== ""
            Image {
                objectName: "presenterNext"
                anchors.fill: parent
                source: panel.presenter.nextPicture
                asynchronous: true
                cache: false
                sourceSize.width: Math.round(width)
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                verticalAlignment: Image.AlignTop
                smooth: true
            }
        }
        Item { Layout.fillHeight: true }
        Label {
            objectName: "presenterNotesHint"
            visible: panel.presenter.pageHasNotes
            text: panel.presenter.showNotes ? qsTr("The audience sees the space for notes too.")
                                          : qsTr("The space for notes shows only here, not to the audience.")
            wrapMode: Text.WordWrap
            color: "#9aa0a6"
            Layout.fillWidth: true
        }
        // The audience sees the whole page with its space for notes (and the ink written there), not only the slide
        // (a setting: Settings → Pen → Presenting)
        Switch {
            objectName: "presenterShowNotes"
            text: qsTr("Notes for the audience too")
            checked: panel.presenter.showNotes
            focusPolicy: Qt.NoFocus
            padding: 0
            onToggled: app.settings.set("presenterShowNotes", checked)
            Layout.fillWidth: true
        }
        // Zooming and scrolling the page here shows the audience the same part (a frame on the page shows what they
        // see); "Fit": both back to the whole slide
        RowLayout {
            spacing: 8
            Switch {
                objectName: "presenterFollowView"
                text: qsTr("The audience follows my zoom")
                checked: panel.presenter.followView
                focusPolicy: Qt.NoFocus
                padding: 0
                onToggled: app.settings.set("presenterFollowView", checked)
                Layout.fillWidth: true
            }
            Button {
                objectName: "presenterFit"
                text: qsTr("Fit")
                focusPolicy: Qt.NoFocus
                flat: true
                onClicked: panel.presenter.fitPage()
                ToolTip.visible: hovered
                ToolTip.text: panel.presenter.showNotes ? qsTr("The whole page on both screens")
                                                      : qsTr("The whole slide on both screens")
                Accessible.name: ToolTip.text
            }
        }
        Label {
            text: qsTr("Audience: %1").arg(panel.presenter.audienceScreenName)
            visible: panel.presenter.audienceScreenName !== ""
            elide: Text.ElideRight
            color: "#9aa0a6"
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
                onClicked: app.settings.set("presenterSwapScreens", !panel.presenter.swapScreens)
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "presenterStop"
                text: qsTr("End")
                focusPolicy: Qt.NoFocus
                onClicked: panel.stopRequested()
            }
        }
    }
}
