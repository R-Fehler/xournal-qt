// The library's Bookmarks view (home screen, qt/docs/bookmarks.md): the bookmarked pages of all documents of the
// library, grouped by document (its name, its folder), each a picture of the page with its label. A tap (or a click)
// opens the document at that page; press and hold or a right click: a menu. It follows the library's search text,
// its "Show" filter and its Favourites chip. The list comes from the library's index (no document is opened); the
// pictures are drawn like the pages of the extended search.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

FocusScope {
    id: view
    objectName: "bookmarksView"
    /// Shown now: the list follows the index while it is
    property bool shown: false
    readonly property var marks: app.libraryBookmarks
    /// Width of a page's picture
    property int thumbWidth: 150

    Binding { target: view.marks; property: "active"; value: view.shown }

    ListView {
        id: list
        objectName: "bookmarksList"
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        focus: true
        model: view.marks
        spacing: 6
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        TouchpadMomentum { flickable: list }
        delegate: Rectangle {
            id: doc
            required property int index
            required property var model
            objectName: "bookmarkDocument"
            width: list.width
            height: docColumn.implicitHeight + 20
            radius: 12
            color: "#ffffff"
            border.width: 1
            border.color: "#d5d8dc"
            ColumnLayout {
                id: docColumn
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Image {
                        visible: doc.model.favourite
                        source: app.iconUrl("xqt-star-filled")
                        sourceSize: Qt.size(16, 16)
                    }
                    Label {
                        objectName: "bookmarkDocumentName"
                        text: doc.model.name
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: "#202124"
                        elide: Text.ElideRight
                        Layout.maximumWidth: docColumn.width * 0.6
                    }
                    Label {
                        visible: doc.model.folder !== ""
                        text: doc.model.folder
                        font.pixelSize: 12
                        color: "#80868b"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Item { Layout.fillWidth: true; visible: doc.model.folder === "" }
                    Label {
                        text: doc.model.marks.length === 1 ? qsTr("1 bookmark") : qsTr("%1 bookmarks").arg(doc.model.marks.length)
                        font.pixelSize: 12
                        color: "#80868b"
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: doc.model.marks
                        delegate: AbstractButton {
                            id: page
                            required property var modelData
                            objectName: "bookmarkCard"
                            readonly property real aspect: modelData.aspect > 0 ? modelData.aspect : 1.414
                            width: view.thumbWidth
                            height: Math.round(view.thumbWidth * Math.min(aspect, 1.6)) + labelText.implicitHeight + 8
                            Accessible.name: modelData.label
                            onClicked: app.openBookmark(doc.model.path, modelData.page)
                            onPressAndHold: pageMenu.popup()
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: pageMenu.popup() }
                            Menu {
                                id: pageMenu
                                MenuItem {
                                    text: qsTr("Open at this page")
                                    onTriggered: app.openBookmark(doc.model.path, page.modelData.page)
                                }
                                MenuItem {
                                    objectName: "copyBookmarkLink"
                                    text: qsTr("Copy link to this page")
                                    onTriggered: app.copyDocumentLink(doc.model.path, page.modelData.page)
                                }
                            }
                            contentItem: Item {
                                Rectangle {
                                    id: paper
                                    width: parent.width
                                    height: Math.round(view.thumbWidth * Math.min(page.aspect, 1.6))
                                    color: "#ffffff"
                                    border.width: page.hovered ? 2 : 1
                                    border.color: page.hovered ? Material.accentColor : "#d5d8dc"
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectFit
                                        verticalAlignment: Image.AlignTop
                                        source: view.shown ? doc.model.pageBase + "/" + page.modelData.page : ""
                                        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                    }
                                    // The ribbon of a bookmarked page
                                    Image {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        y: -2
                                        source: app.iconUrl("xqt-bookmark-filled")
                                        sourceSize: Qt.size(18, 22)
                                    }
                                }
                                Label {
                                    id: labelText
                                    objectName: "bookmarkLabel"
                                    anchors.top: paper.bottom
                                    anchors.topMargin: 4
                                    width: parent.width
                                    text: page.modelData.label
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    maximumLineCount: 2
                                    wrapMode: Text.Wrap
                                    font.pixelSize: 12
                                    color: "#3c4043"
                                }
                            }
                            background: null
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: 10
        width: Math.min(parent.width - 40, 460)
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: app.iconUrl("xqt-bookmark")
            sourceSize.width: 56; sourceSize.height: 56
            opacity: 0.5
        }
        Label {
            objectName: "bookmarksEmpty"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: 16
            color: "#5f6368"
            text: app.library.searchQuery !== "" || app.library.favouritesOnly ? qsTr("No bookmarks match")
                                                                                : qsTr("No bookmarks yet")
        }
        Label {
            visible: app.library.searchQuery === "" && !app.library.favouritesOnly
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#80868b"
            text: qsTr("In a document, bookmark a page with its menu (press and hold a page, or its ⋮ in the pages sidebar).")
        }
    }
}
