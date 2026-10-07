// xournal-qt: the switch in the home screen's header: the library's name with ▾ (the libraries), Recent,
// Favourites, Tags, Bookmarks, To-dos.
// Part of HomeView.qml (the home screen, qt/docs/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Rectangle {
    id: switchBox
    // (what the other parts use)
    readonly property alias libraryTab: libraryTab
    readonly property alias libraryMenuButton: libraryMenuButton
    parent: homeHeader.switchSlot
    anchors.fill: parent
    radius: height / 2
    color: "#e1e4e8"
    implicitWidth: switchTabs.implicitWidth + 8
    implicitHeight: switchTabs.implicitHeight + 6
    RowLayout {
        id: switchTabs
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        anchors.topMargin: 3
        anchors.bottomMargin: 3
        spacing: 2
        PageTab {
            id: libraryTab
            objectName: "libraryPageButton"
            pageIndex: 0
            label: app.library.available ? app.library.name : qsTr("Library")
            iconName: "xqt-library"
            enabled: app.library.available
        }
        // The libraries: another one, a new one, a folder as library, …
        ToolButton {
            id: libraryMenuButton
            objectName: "libraryMenuButton"
            readonly property string tip: qsTr("Libraries")
            Layout.fillHeight: true
            implicitWidth: 30
            leftPadding: 0
            rightPadding: 0
            Accessible.name: tip
            icon.source: app.iconUrl("xqt-chevron-down")
            icon.width: 20
            icon.height: 20
            icon.color: "#3c4043"
            display: AbstractButton.IconOnly
            onClicked: Popups.openAt(libraryMenu)
            // (a finger held on it says what it is, as the other buttons: qt/docs/adaptive-layout.md)
            property bool heldTip: false
            onPressAndHold: heldTip = true
            onReleased: heldTip = false
            onCanceled: heldTip = false
            ToolTip.visible: heldTip || hovered
            ToolTip.text: tip
            ToolTip.delay: heldTip ? 0 : 600
            background: Rectangle {
                radius: height / 2
                color: libraryMenuButton.pressed ? "#d5d8dc" : "transparent"
            }
                    // The libraries: this window shows one (highlighted); another one opens in a new window.
                    AdaptiveMenu {
                        id: libraryMenu
                        objectName: "libraryMenu"
                        minimumWidth: 300
                        property var libraries: []
                        onAboutToShow: libraries = app.libraries()
                        Label {
                            text: app.libraryWindows ? qsTr("Libraries (another one opens in a new window)")
                                                     : qsTr("Libraries (the window switches to another one)")
                            leftPadding: 16
                            rightPadding: 16
                            topPadding: 8
                            bottomPadding: 4
                            width: libraryMenu.width
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: "#6b6f75"
                        }
                        Instantiator {
                            id: libraryList
                            model: libraryMenu.libraries
                            delegate: AdaptiveMenuItem {
                                id: libraryItem
                                objectName: "libraryMenuEntry"
                                required property var modelData
                                readonly property bool current: modelData.current
                                readonly property string label: modelData.downloads ? qsTr("Downloads folder (quick library)") : modelData.name
                                text: current ? qsTr("%1 — this window").arg(label) : label
                                font.weight: current ? Font.DemiBold : Font.Normal
                                icon.source: app.iconUrl(modelData.downloads ? "xqt-download" : "xqt-library")
                                icon.color: current ? Material.accentColor : "#566d86"
                                background: Rectangle {
                                    color: libraryItem.current ? "#e8eaf6" : (libraryItem.highlighted ? "#f1f3f4" : "transparent")
                                }
                                // This library: just the home screen; another: a new window (one library per window)
                                // (a path, not "file://" + path: on Windows that makes the drive letter a host)
                                onTriggered: current ? (app.homeVisible = true) : app.openLibraryAt(modelData.path)
                            }
                            // after the heading
                            onObjectAdded: function(index, object) { libraryMenu.insertItem(index + 1, object) }
                            onObjectRemoved: function(index, object) { libraryMenu.removeItem(object) }
                        }
                        MenuSeparator {}
                        // The library's tags (on a phone the switch has no room for their tab)
                        AdaptiveMenuItem {
                            objectName: "libraryMenuTags"
                            text: qsTr("Tags")
                            icon.source: app.iconUrl("xqt-tag")
                            offered: home.phoneLayout
                            enabled: app.library.available
                            onTriggered: home.page = 4
                        }
                        AdaptiveMenuItem {
                            text: app.libraryWindows ? qsTr("New library… (new window)") : qsTr("New library…")
                            onTriggered: libraryDialogs.newLibraryDialog.open()
                        }
                        AdaptiveMenuItem {
                            objectName: "openFolderAsLibraryItem"
                            text: app.libraryWindows ? qsTr("Open a folder as library… (new window)") : qsTr("Open a folder as library…")
                            onTriggered: home.pickLibraryFolder()
                        }
                        // (not on Android: there is no file manager the app could show a folder in reliably)
                        AdaptiveMenuItem {
                            objectName: "libraryShowInFileManagerItem"
                            text: qsTr("Show in file manager")
                            enabled: app.library.available
                            offered: app.canShowInFileManager
                            onTriggered: app.showInFileManager(app.library.rootPath)
                        }
                        AdaptiveMenuItem {
                            objectName: "shareLibraryItem"
                            text: qsTr("Share library…")
                            enabled: app.library.available && !app.libraryShare.running
                            onTriggered: libraryDialogs.shareZip.openFor("")
                        }
                        AdaptiveMenuItem {
                            objectName: "shareThisFolderItem"
                            text: qsTr("Share this folder…")
                            offered: app.library.folder !== "" && !app.library.flat
                            enabled: app.library.available && !app.libraryShare.running
                            onTriggered: libraryDialogs.shareZip.openFor(app.library.folder)
                        }
                        AdaptiveMenuItem {
                            objectName: "exportLibraryArchiveItem"
                            text: qsTr("Export library as archive…")
                            enabled: app.library.available && !app.libraryArchive.running
                            onTriggered: archiveDialogs.libraryArchiveDialog.open()
                        }
                    }
        }
        PageTab {
            id: recentTab
            objectName: "recentPageButton"
            pageIndex: 1
            label: qsTr("Recent")
            iconName: "xqt-history"
        }
        // Only the favourites: a filter of the library and of its bookmarks (starred documents of the whole
        // library, combined with Show and the search), not a page of its own, as the chip was. Filled and marked
        // while on; on Recent a tap shows the library's favourites.
        PageTab {
            id: favouritesTab
            objectName: "favouritesChip"
            label: qsTr("Favourites")
            tip: chosen ? qsTr("Only favourites are shown - tap: all documents")
                        : qsTr("Show only favourites (starred documents)")
            iconName: chosen ? "xqt-star-filled" : "xqt-star"
            enabled: app.library.available
            chosen: app.library.favouritesOnly && home.page !== 1
            chosenColor: "#fdf1d0"
            chosenBorder: "#f4b400"
            chosenText: "#8a5a00"
            tapAction: function() {
                if (home.page === 1) {
                    home.page = 0
                    app.library.favouritesOnly = true
                } else {
                    app.library.favouritesOnly = !app.library.favouritesOnly
                }
            }
        }
        // The tags of the library's documents (#tags typed in them, keywords of PDFs): a page listing them with
        // counts; a tap on one filters the library (qt/docs/tags.md)
        PageTab {
            id: tagsTab
            objectName: "tagsPageButton"
            pageIndex: 4
            label: qsTr("Tags")
            iconName: "xqt-tag"
            enabled: app.library.available
            // (a phone has no room for it in the switch: the ▾ menu has it)
            visible: !home.phoneLayout
        }
        PageTab {
            id: bookmarksTab
            objectName: "bookmarksPageButton"
            pageIndex: 2
            label: qsTr("Bookmarks")
            iconName: "xqt-bookmark"
            enabled: app.library.available
        }
        PageTab {
            id: todosTab
            objectName: "todosPageButton"
            pageIndex: 3
            label: qsTr("To-dos")
            iconName: "xqt-list-todo"
            enabled: app.library.available
        }
    }

    // A tab of the switch: the library's name (its icon beside it outside the phones), or the icon of Recent,
    // Favourites or Bookmarks, with its word only where the header has room for all words (roomy); an icon alone says
    // its word in a tip on hover and while a finger is held on it
    component PageTab: AbstractButton {
        id: tab
        property int pageIndex: -1
        property string label
        property string iconName
        /// Its tip (default: its word)
        property string tip: label
        /// Shown as chosen: the page shown (the star: only favourites)
        property bool chosen: home.page === pageIndex
        property color chosenColor: "#ffffff"
        property color chosenBorder: "transparent"
        property color chosenText: "#202124"
        /// What a tap does (default: show its page)
        property var tapAction: null
        /// The library's tab: its name, never the icon alone
        readonly property bool named: pageIndex === 0
        readonly property bool iconOnly: !named && !home.roomy
        Layout.fillWidth: named && home.portraitPhone
        Layout.maximumWidth: named && home.shortLayout ? 136 : Number.POSITIVE_INFINITY  // (held sideways: room for the breadcrumbs)
        Layout.fillHeight: true
        implicitHeight: home.touch ? 44 : 40
        implicitWidth: iconOnly ? (home.touch ? 44 : 40) : tabRow.implicitWidth + (named ? 20 : 28)
        onClicked: tapAction ? tapAction() : (home.page = pageIndex)
        property bool heldTip: false
        onPressAndHold: heldTip = true
        onReleased: heldTip = false
        onCanceled: heldTip = false
        ToolTip.visible: (iconOnly || tip !== label) && (hovered || heldTip)
        ToolTip.text: tip
        ToolTip.delay: heldTip ? 0 : 600
        Accessible.name: label
        background: Rectangle {
            radius: height / 2
            color: tab.chosen ? tab.chosenColor : (tab.pressed ? "#d5d8dc" : "transparent")
            border.width: tab.chosen && tab.chosenBorder !== Qt.color("transparent") ? 1 : 0
            border.color: tab.chosenBorder
        }
        contentItem: Item {
            RowLayout {
                id: tabRow
                anchors.centerIn: parent
                // (a narrow tab: its word elided)
                width: Math.min(implicitWidth, parent.width)
                spacing: 6
                Image {
                    visible: !(tab.named && home.phoneLayout)
                    source: app.iconUrl(tab.iconName)
                    sourceSize.width: tab.iconOnly ? 22 : 18
                    sourceSize.height: tab.iconOnly ? 22 : 18
                    opacity: tab.enabled ? 1 : 0.4
                }
                Label {
                    visible: !tab.iconOnly
                    text: tab.label
                    font.weight: tab.chosen ? Font.DemiBold : Font.Normal
                    color: !tab.enabled ? "#9aa0a6" : tab.chosen ? tab.chosenText : "#202124"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.maximumWidth: 220
                }
            }
        }
    }
}
