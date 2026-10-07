// The History panel of the page sidebar (qt/docs/features/hybrid-pdf.md, "Version history"): the versions a PDF with
// notes keeps inside itself, newest first. While versions are not kept, what it does and the switch to keep them (off
// by default, but here to be found). A row's menu: show it beside the document (read-only), compare it with now or with
// another version (picked next in the list; app.compare), restore it (a new version on top, undoable), open it as a
// copy, give it a message. "Save with a message…" (Ctrl+Alt+S) makes a milestone.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: panel
    readonly property int target: typeof win !== "undefined" && win && win.adaptive.touchProfile ? win.adaptive.minTarget : 36
    readonly property var model: app.versions
    signal picked()
    /// Open the dialog that gives version `id` a message (-1: the next save, "Save with a message…")
    signal messageRequested(int id)
    /// "Compare with another version…": the version to compare (-1: none); the next version tapped is the other one
    property int compareFrom: -1

    Flickable {
        id: intro
        objectName: "historyIntro"
        visible: !panel.model.available || !panel.model.on
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: introColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: introColumn
            width: intro.width
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("Version history")
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Label {
                objectName: "historyExplanation"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: "#3c4043"
                text: qsTr("Keep the versions of this document inside its PDF: one for each day you save it, plus "
                           + "milestones you name (Ctrl+Alt+S, \"Save with a message…\"). Go back to any of them, show "
                           + "it beside the document or open it as a copy. The versions travel with the file; any PDF "
                           + "app still opens it as it is now.")
            }
            Label {
                objectName: "historyUnavailable"
                Layout.fillWidth: true
                visible: !panel.model.available
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: "#b06000"
                text: panel.model.unavailableReason
            }
            Button {
                objectName: "historySaveAsPdf"
                visible: panel.model.needsPdf
                text: qsTr("Save as PDF with notes…")
                onClicked: openSaveDialog(null, "pdf")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.model.available
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Keep versions of this document")
                    font.weight: Font.DemiBold
                }
                Switch {
                    objectName: "historySwitch"
                    checked: panel.model.on
                    onToggled: panel.model.on = checked
                }
            }
            Label {
                Layout.fillWidth: true
                visible: panel.model.available
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#5f6368"
                text: qsTr("The file grows a little with each day's version (older versions are kept as the changes "
                           + "only). Sharing sends the PDF without its versions unless you choose otherwise. New PDFs "
                           + "with notes can keep versions from the start: Settings → Documents.")
            }
        }
    }

    ColumnLayout {
        id: main
        visible: panel.model.available && panel.model.on
        anchors.fill: parent
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 2
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: qsTr("Keep versions")
                font.pixelSize: 13
            }
            BusyIndicator {
                running: panel.model.busy || panel.model.restoring
                visible: running
                implicitWidth: 24
                implicitHeight: 24
            }
            Switch {
                objectName: "historySwitchOn"
                checked: panel.model.on
                onToggled: {
                    if (!checked) {
                        checked = true  // (asked first)
                        stopDialog.open()
                    }
                }
            }
        }
        // The switch was changed here: written by the next save
        Pane {
            objectName: "historyPending"
            Layout.fillWidth: true
            visible: panel.model.pending
            padding: 8
            background: Rectangle { color: "#fef7e0"; radius: 6 }
            ColumnLayout {
                width: parent.width
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    text: qsTr("Versions are kept from the next save.")
                }
                Button {
                    objectName: "historySaveNow"
                    text: qsTr("Save now")
                    flat: true
                    onClicked: saveOrAsk(null)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 4
            Button {
                objectName: "historySaveWithMessage"
                text: qsTr("Save with a message…")
                flat: true
                font.pixelSize: 12
                onClicked: panel.messageRequested(-1)
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                objectName: "historyMilestonesOnly"
                text: qsTr("Milestones")
                font.pixelSize: 12
                checked: panel.model.milestonesOnly
                onToggled: panel.model.milestonesOnly = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Only the versions with a message")
            }
        }
        Label {
            objectName: "historySize"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            visible: text !== ""
            text: panel.model.sizeText
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: "#6b6f75"
        }
        // Picking the second version of a comparison
        Pane {
            objectName: "historyPickBanner"
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            visible: panel.compareFrom >= 0
            padding: 8
            background: Rectangle { color: "#f3e8fd"; radius: 6 }
            RowLayout {
                width: parent.width
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    text: qsTr("Tap the version to compare with %1.").arg(panel.model.titleOf(panel.compareFrom))
                }
                Button {
                    objectName: "historyPickCancel"
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: panel.compareFrom = -1
                }
            }
        }
        Label {
            objectName: "historyRemoved"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            visible: panel.model.removed > 0
            text: qsTr("%n version(s) were removed by another app (it wrote the file anew).", "", panel.model.removed)
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: "#b06000"
        }
        ListView {
            id: list
            objectName: "historyList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.model
            boundsBehavior: Flickable.StopAtBounds
            spacing: 2
            ScrollBar.vertical: ScrollBar {}
            TouchpadMomentum { flickable: list }
            delegate: ItemDelegate {
                id: entry
                objectName: "historyItem"
                required property int index
                required property int versionId
                required property string kind
                required property string title
                required property string message
                required property bool milestone
                required property string detail
                required property bool current
                width: ListView.view.width
                leftPadding: 8
                rightPadding: 8
                topPadding: 5
                bottomPadding: 5
                enabled: kind === "version"
                Accessible.name: title + (message !== "" ? ", " + message : "")
                onClicked: {
                    if (panel.compareFrom >= 0) {
                        // (the second version of "Compare with another version…")
                        const first = panel.compareFrom
                        panel.compareFrom = -1
                        if (first !== versionId && app.compareVersions(first, versionId)) panel.picked()
                        return
                    }
                    rowMenu.versionId = versionId
                    rowMenu.current = current
                    rowMenu.received = detail === qsTr("The PDF as it was received")
                    rowMenu.openMenu(null, entry)
                }
                contentItem: RowLayout {
                    spacing: 6
                    Rectangle {
                        Layout.fillHeight: true
                        implicitWidth: 3
                        radius: 1.5
                        color: entry.kind === "unsaved" ? "#f9ab00" : entry.milestone ? Material.accentColor
                                                                    : entry.kind === "other" ? "#9aa0a6" : "#c4c7c5"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        RowLayout {
                            spacing: 4
                            Image {
                                visible: entry.milestone
                                source: app.iconUrl("xqt-flag")
                                sourceSize.width: 14
                                sourceSize.height: 14
                            }
                            Label {
                                Layout.fillWidth: true
                                text: entry.title + (entry.current ? "  " + qsTr("(now)") : "")
                                font.pixelSize: 12
                                color: entry.kind === "unsaved" ? "#b06000" : "#5f6368"
                                elide: Text.ElideRight
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: entry.message !== ""
                            text: entry.message
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            font.weight: entry.milestone ? Font.DemiBold : Font.Normal
                            color: "#202124"
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: entry.detail !== ""
                            text: entry.detail
                            font.pixelSize: 11
                            color: "#6b6f75"
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: 16
            visible: list.count === 0 && !panel.model.busy
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#6b6f75"
            text: panel.model.milestonesOnly ? qsTr("No milestones yet. Save with a message%1 to make one.").arg(win.keyNote("saveWithMessage"))
                                             : qsTr("No versions yet: the next save keeps the first one.")
        }
    }

    AdaptiveMenu {
        id: rowMenu
        objectName: "historyRowMenu"
        property int versionId: -1
        property bool current: false
        property bool received: false
        title: panel.model.titleOf(versionId)
        AdaptiveMenuItem {
            objectName: "historyView"
            text: qsTr("Show beside the document")
            icon.source: app.iconUrl("xqt-book-open")
            onTriggered: { if (app.viewVersion(rowMenu.versionId)) panel.picked() }
        }
        AdaptiveMenuItem {
            objectName: "historyCompareNow"
            text: qsTr("Compare with now")
            icon.source: app.iconUrl("xqt-compare")
            onTriggered: { if (app.compareWithNow(rowMenu.versionId)) panel.picked() }
        }
        AdaptiveMenuItem {
            objectName: "historyCompareTwo"
            offered: panel.model.versionCount > 1
            text: qsTr("Compare with another version…")
            icon.source: app.iconUrl("xqt-compare")
            onTriggered: panel.compareFrom = rowMenu.versionId
        }
        AdaptiveMenuItem {
            objectName: "historyRestore"
            offered: !rowMenu.current && !rowMenu.received
            text: qsTr("Restore this version…")
            icon.source: app.iconUrl("xqt-history")
            onTriggered: { restoreDialog.versionId = rowMenu.versionId; restoreDialog.open() }
        }
        AdaptiveMenuItem {
            objectName: "historyOpenCopy"
            text: qsTr("Open as a copy")
            icon.source: app.iconUrl("xqt-copy")
            onTriggered: app.openVersionAsCopy(rowMenu.versionId)
        }
        AdaptiveMenuItem {
            objectName: "historyEditMessage"
            text: panel.model.messageOf(rowMenu.versionId) === "" ? qsTr("Add a message…") : qsTr("Change the message…")
            icon.source: app.iconUrl("xqt-pencil")
            onTriggered: panel.messageRequested(rowMenu.versionId)
        }
    }

    AdaptiveDialog {
        id: restoreDialog
        objectName: "historyRestoreDialog"
        kind: "question"
        property int versionId: -1
        preferredWidth: 420
        title: qsTr("Restore the version of %1?").arg(panel.model.titleOf(versionId))
        Label {
            width: restoreDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Its pages replace the pages of the document (Undo brings them back). Nothing of the history is "
                       + "lost: the next save keeps it as a new version, and the versions after it stay.")
        }
        footer: DialogButtonBox {
            Button { objectName: "historyRestoreConfirm"; text: qsTr("Restore"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: panel.model.restore(versionId)
    }

    AdaptiveDialog {
        id: stopDialog
        objectName: "historyStopDialog"
        kind: "question"
        preferredWidth: 420
        title: qsTr("Stop keeping versions?")
        Label {
            width: stopDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("From the next save on, the list of versions is no longer kept. The older versions stay in the "
                       + "file until it is next written in full, but they are not listed any more.")
        }
        footer: DialogButtonBox {
            Button { objectName: "historyStopConfirm"; text: qsTr("Stop"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: panel.model.on = false
    }
}
