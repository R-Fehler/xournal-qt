// xournal-qt: where we are in the library: the breadcrumbs (eliding from the middle), or what the list shows
// (search, flat, favourites, a tag), with what the library is busy with.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

RowLayout {
    id: crumbBar
    // (what the other parts use)
    readonly property alias crumbRow: crumbRow
    parent: home.shortLayout ? homeHeader.headerCrumbSlot : crumbRowSlot
    anchors.fill: parent
    spacing: 2
    readonly property bool showCrumbs: !home.searching && !app.library.flat && !app.library.favouritesOnly
    /// At the library's top the breadcrumbs would only repeat its name (the switch shows it): no row for them then,
    /// unless it says what the library is doing (importing, indexing)
    readonly property bool atRoot: showCrumbs && app.library.folder === ""
    readonly property bool needed: !atRoot || app.library.importing || app.library.tagFilter !== ""
                                   || (app.library.indexing && app.library.indexTotal > 0)
                                   || (app.handwriting.enabled && app.handwriting.libraryLeft > 0)

    IconButton {
        objectName: "folderUpButton"
        label: qsTr("Up")
        iconName: "xqt-arrow-up"
        tip: qsTr("Up (Backspace)")
        implicitWidth: home.touch ? 44 : 40
        implicitHeight: home.touch ? 44 : 40
        visible: crumbBar.showCrumbs && !crumbBar.atRoot
        enabled: app.library.folder !== ""
        onClicked: app.library.goUp()
    }
    Item {
        id: crumbArea
        objectName: "crumbArea"
        visible: crumbBar.showCrumbs && !crumbBar.atRoot
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
        clip: true
        /// Which crumbs show: the library (unless even the last folder is short of room then), "…" for the
        /// folders left out, and the ones from `first` on; the widths of the library's and of the last one (elided
        /// when they do not fit)
        readonly property var plan: {
            const n = crumbRepeater.count, room = width
            const widths = []
            for (let i = 0; i < n; ++i) {
                const c = crumbRepeater.itemAt(i)
                widths.push(c ? c.naturalWidth : 0)
            }
            const total = widths.reduce(function(a, b) { return a + b }, 0)
            if (n <= 1 || total <= room)
                return { first: 1, showRoot: true, rootWidth: n > 0 ? Math.min(widths[0], room) : 0,
                         lastWidth: n > 0 ? Math.min(widths[n - 1], room) : 0 }
            const ellipsis = crumbEllipsis.implicitWidth
            const last = widths[n - 1]
            // The last folder first (up to 60 % of the room, at least 120), then the library with what is left
            // (at most half the room), left out too when that is less than 56
            const lastWant = Math.min(last, Math.max(120, room * 0.6))
            const rootRoom = room - (n > 2 ? ellipsis : 0) - lastWant
            const showRoot = rootRoom >= 56
            const rootWidth = Math.min(widths[0], rootRoom, room * 0.5)
            let first = n - 1
            let used = (showRoot ? rootWidth : 0) + (n > 2 || !showRoot ? ellipsis : 0) + last
            while (first - 1 >= 1 && used + widths[first - 1] <= room) {
                first--
                used += widths[first]
            }
            const shownEllipsis = first > 1 || !showRoot
            let middle = 0
            for (let i = first; i < n - 1; ++i) middle += widths[i]
            const lastRoom = room - (showRoot ? rootWidth : 0) - (shownEllipsis ? ellipsis : 0) - middle
            return { first: first, showRoot: showRoot, rootWidth: rootWidth,
                     lastWidth: Math.max(40, Math.min(last, lastRoom)) }
        }
        /// The folders left out ("…"): [{name, folder}]
        readonly property var hidden: {
            const list = [], crumbs = app.library.breadcrumbs
            for (let i = plan.showRoot ? 1 : 0; i < plan.first && i < crumbs.length; ++i) list.push(crumbs[i])
            return list
        }
        Row {
            id: crumbRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Repeater {
                id: crumbRepeater
                model: app.library.breadcrumbs
                delegate: Row {
                    id: crumbItem
                    required property int index
                    required property var modelData
                    readonly property string folder: modelData.folder
                    readonly property bool last: index === crumbRepeater.count - 1
                    /// Its width with its whole name
                    readonly property real naturalWidth: crumbLabel.implicitWidth + (index > 0 ? 34 : 18)
                    /// Where the crumb itself starts (after "…")
                    readonly property real crumbX: crumb.x
                    visible: index === 0 ? crumbArea.plan.showRoot : index >= crumbArea.plan.first
                    // "…" before the first folder shown after the library: the ones left out
                    AbstractButton {
                        id: crumbEllipsisHere
                        objectName: "crumbEllipsis"
                        visible: crumbItem.index > 0 && crumbItem.index === crumbArea.plan.first
                                 && (crumbItem.index > 1 || !crumbArea.plan.showRoot)
                        implicitHeight: home.touch ? 44 : 40
                        implicitWidth: crumbEllipsis.implicitWidth
                        onClicked: Popups.openAt(crumbMenu)
                        Accessible.name: qsTr("More folders")
                        background: Rectangle {
                            radius: 8
                            color: crumbEllipsisHere.hovered || crumbEllipsisHere.pressed ? "#e1e4e8" : "transparent"
                        }
                        contentItem: Item {
                            Image {
                                visible: crumbArea.plan.showRoot
                                anchors.verticalCenter: parent.verticalCenter
                                source: app.iconUrl("xqt-chevron-right")
                                sourceSize.width: 16; sourceSize.height: 16
                                opacity: 0.6
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                x: crumbArea.plan.showRoot ? 24 : 12
                                text: "…"
                                font.pixelSize: 15
                                color: "#3c4043"
                            }
                        }
                    }
                    AbstractButton {
                        id: crumb
                        objectName: "crumb"
                        readonly property bool dropTarget: moveDrag.active && moveDrag.hasTarget && moveDrag.target === crumbItem.folder
                        implicitHeight: home.touch ? 44 : 40
                        implicitWidth: crumbItem.naturalWidth
                        width: crumbItem.index === 0 ? crumbArea.plan.rootWidth
                               : crumbItem.last ? crumbArea.plan.lastWidth : implicitWidth
                        onClicked: app.library.folder = crumbItem.folder
                        Accessible.name: crumbItem.modelData.name
                        background: Rectangle {
                            radius: 8
                            color: crumb.dropTarget ? "#c5cae9" : (crumb.hovered ? "#e1e4e8" : "transparent")
                        }
                        contentItem: Item {
                            Image {
                                visible: crumbItem.index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                x: 0
                                source: app.iconUrl("xqt-chevron-right")
                                sourceSize.width: 16; sourceSize.height: 16
                                opacity: 0.6
                            }
                            Label {
                                id: crumbLabel
                                objectName: "crumbLabel"
                                anchors.verticalCenter: parent.verticalCenter
                                x: crumbItem.index > 0 ? 24 : 9
                                width: crumb.width - x - (crumbItem.index > 0 ? 10 : 9)
                                text: crumbItem.modelData.name
                                elide: Text.ElideRight
                                font.pixelSize: 15
                                font.weight: crumbItem.last ? Font.DemiBold : Font.Normal
                                color: "#3c4043"
                            }
                        }
                    }
                }
            }
        }
        // (its size: the "…" of the crumbs is as wide)
        Item { id: crumbEllipsis; visible: false; implicitWidth: 48 }
        AdaptiveMenu {
            id: crumbMenu
            objectName: "crumbMenu"
            title: qsTr("Folders")
            Instantiator {
                model: crumbArea.hidden
                delegate: AdaptiveMenuItem {
                    required property var modelData
                    objectName: "crumbMenuEntry"
                    text: modelData.name
                    icon.source: app.iconUrl("xqt-folder")
                    onTriggered: app.library.folder = modelData.folder
                }
                onObjectAdded: function(index, object) { crumbMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { crumbMenu.removeItem(object) }
            }
        }
    }
    // Only the documents with a tag (the Tags page): the tag, a tap takes the filter away
    AbstractButton {
        id: tagFilterChip
        objectName: "tagFilterChip"
        visible: app.library.tagFilter !== ""
        Layout.maximumWidth: 260
        implicitHeight: home.touch ? 40 : 32
        implicitWidth: tagChipRow.implicitWidth + 20
        Accessible.name: qsTr("Only #%1 - tap: all documents").arg(app.library.tagFilter)
        onClicked: app.library.tagFilter = ""
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Only documents tagged #%1 - tap: all documents").arg(app.library.tagFilter)
        ToolTip.delay: 600
        background: Rectangle {
            radius: height / 2
            color: tagFilterChip.pressed ? "#c2d7f5" : "#d2e3fc"
        }
        contentItem: Item {
            RowLayout {
                id: tagChipRow
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width)
                spacing: 4
                Label {
                    objectName: "tagFilterLabel"
                    text: "#" + app.library.tagFilter
                    elide: Text.ElideRight
                    color: "#174ea6"
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Label { text: "✕"; color: "#174ea6" }
            }
        }
    }
    Item { Layout.fillWidth: true; visible: app.library.tagFilter !== "" && crumbBar.atRoot }
    Label {
        objectName: "listCaption"
        visible: !crumbBar.showCrumbs
        Layout.leftMargin: 8
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: home.searching ? (libraryPage.libraryGrid.count === 1 ? qsTr("1 result") : qsTr("%1 results").arg(libraryPage.libraryGrid.count))
              : app.library.favouritesOnly ? qsTr("Favourites in %1").arg(app.library.name)
                             : qsTr("All documents in %1").arg(app.library.name)
        elide: Text.ElideRight
        font.pixelSize: 15
        color: "#3c4043"
    }
    BusyIndicator {
        visible: app.library.importing
        running: visible
        implicitWidth: 28; implicitHeight: 28
    }
    Label {
        objectName: "indexStatus"
        visible: app.library.indexing && app.library.indexTotal > 0
        text: home.shortLayout ? qsTr("Indexing %1/%2").arg(app.library.indexed).arg(app.library.indexTotal)
                               : qsTr("Indexing for search %1/%2").arg(app.library.indexed).arg(app.library.indexTotal)
        font.pixelSize: 12
        color: "#6b6f75"
    }
    Label {
        objectName: "handwritingLibraryStatus"
        visible: app.handwriting.enabled && app.handwriting.libraryLeft > 0
        text: home.shortLayout ? qsTr("Handwriting: %1 left").arg(app.handwriting.libraryLeft)
                               : qsTr("Reading handwriting: %1 documents left").arg(app.handwriting.libraryLeft)
        font.pixelSize: 12
        color: "#6b6f75"
    }
}
