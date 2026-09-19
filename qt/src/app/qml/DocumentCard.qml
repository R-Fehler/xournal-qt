// A document or folder in the library / recent grids: first-page preview (or a folder), name and details.
// Tap to open (while items are selected: to select it too); Ctrl / Shift + click and the circle in the corner select;
// right click, the ⋮ button or press and hold (without moving) for the menu; press and hold, then move to drag it
// (with the other selected items) onto a folder (when `dragOverlay` is set).
// Extended library search (`stripHeight` > 0): below the title, the pages with hits (marked), side by side;
// tapping one opens the document at that page.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: card
    property string name
    property string path
    property string subtitle
    property string preview
    property bool isFolder: false
    property int itemCount: 0
    property bool hasPdf: false
    property bool hasXopp: false
    property int hits: 0
    property string snippet
    property bool highlighted: false
    property bool selected: false
    /// Items are being selected: the circles are shown on all cards.
    property bool selectionMode: false
    /// Load the preview (not while the grid is hidden: rendering previews costs time)
    property bool active: true
    /// A dragged document is over this folder.
    property bool dropTarget: false
    property var dragOverlay: null
    property int row: -1
    /// Pages with search hits: [{ page, count, aspect }], their image URL base, the height of their row (0: none)
    property var hitPages: []
    property string hitPageBase
    property int stripHeight: 0
    signal pageActivated(int page)
    /// A tap or click (with the keyboard modifiers of a click)
    signal activated(int modifiers)
    signal toggleRequested()
    signal menuRequested(Item item, real x, real y)

    HoverHandler { id: hover }

    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: 8
        radius: 12
        color: card.dropTarget || card.selected ? "#e8eaf6" : "#ffffff"
        border.width: card.highlighted || card.dropTarget || card.selected || card.hits > 0 ? 3 : 1
        border.color: card.dropTarget || card.selected ? Material.accentColor
                                      : card.hits > 0 ? "#f9a825" : card.highlighted ? Material.accentColor : "#d5d8dc"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Rectangle {
                id: previewBox
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: card.isFolder ? "#eef1f8" : "#f4f5f7"
                clip: true
                Image {
                    id: previewImage
                    visible: !card.isFolder
                    anchors.fill: parent
                    anchors.margins: 6
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    source: card.isFolder || !card.active ? "" : card.preview
                    sourceSize.width: 360
                    // Paper look: a little shadow around the page
                    Rectangle {
                        z: -1
                        x: (parent.width - parent.paintedWidth) / 2 - 1
                        y: (parent.height - parent.paintedHeight) / 2 - 1
                        width: parent.paintedWidth + 2
                        height: parent.paintedHeight + 2
                        color: "#20000000"
                        visible: parent.status === Image.Ready
                    }
                }
                BusyIndicator {
                    anchors.centerIn: parent
                    visible: !card.isFolder && card.preview !== "" && previewImage.status === Image.Loading
                    running: visible
                    implicitWidth: 36
                    implicitHeight: 36
                }
                Image {
                    visible: card.isFolder
                    anchors.centerIn: parent
                    source: app.iconUrl("xqt-folder")
                    readonly property int iconSize: Math.min(96, previewBox.width * 0.45)
                    sourceSize: Qt.size(iconSize, iconSize)
                    opacity: 0.8
                }
                // "PDF" for documents with a PDF (annotated or not)
                Rectangle {
                    id: pdfBadge
                    visible: card.hasPdf
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    radius: 4
                    color: "#d93025"
                    width: pdfLabel.implicitWidth + 8
                    height: 16
                    Label {
                        id: pdfLabel
                        anchors.centerIn: parent
                        text: card.hasXopp ? qsTr("PDF ✎") : qsTr("PDF")
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        color: "#ffffff"
                    }
                }
                HitBadge {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    anchors.topMargin: card.hasPdf ? 26 : 6
                    count: card.hits
                }
                // Search: the text around the first match
                Rectangle {
                    visible: card.snippet !== ""
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Math.min(parent.height * 0.5, snippetLabel.implicitHeight + 10)
                    color: "#f0fffbe6"
                    Label {
                        id: snippetLabel
                        anchors.fill: parent
                        anchors.margins: 5
                        text: card.snippet
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        maximumLineCount: 4
                        font.pixelSize: 11
                        color: "#4a3b00"
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        objectName: "cardName"
                        Layout.fillWidth: true
                        text: card.name
                        elide: Text.ElideMiddle
                        font.weight: Font.DemiBold
                        color: "#202124"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: card.subtitle
                        elide: Text.ElideMiddle
                        font.pixelSize: 12
                        color: "#6b6f75"
                    }
                }
                ToolButton {
                    objectName: "cardMenuButton"
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 36
                    implicitHeight: 40
                    icon.source: app.iconUrl("xqt-more")
                    icon.color: "#5f6368"
                    icon.width: 20
                    icon.height: 20
                    display: AbstractButton.IconOnly
                    onClicked: card.menuRequested(this, width / 2, height)
                }
            }
            // The pages with hits
            ListView {
                id: strip
                objectName: "hitPageStrip"
                visible: card.stripHeight > 0
                Layout.fillWidth: true
                Layout.preferredHeight: card.stripHeight
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: card.stripHeight > 0 ? card.hitPages : []
                cacheBuffer: Math.max(0, width)  // the next pages are ready when scrolling
                ScrollBar.horizontal: ScrollBar { height: 6 }
                Label {
                    anchors.centerIn: parent
                    visible: strip.count === 0
                    text: qsTr("Found in the name")
                    color: "#80868b"
                }
                delegate: AbstractButton {
                    id: hitPage
                    required property var modelData
                    readonly property real thumbHeight: strip.height - 22
                    objectName: "hitPage"
                    width: Math.max(24, Math.round(thumbHeight / (modelData.aspect > 0 ? modelData.aspect : 1.414)))
                    height: strip.height
                    onClicked: card.pageActivated(modelData.page)
                    contentItem: Item {
                        Rectangle {
                            id: paper
                            width: parent.width
                            height: hitPage.thumbHeight
                            color: "#ffffff"
                            border.width: hitPage.hovered ? 2 : 1
                            border.color: hitPage.hovered ? Material.accentColor : "#d5d8dc"
                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                asynchronous: true
                                fillMode: Image.PreserveAspectFit
                                source: card.active && card.hitPageBase !== "" ? card.hitPageBase + "/" + hitPage.modelData.page : ""
                                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                            }
                            HitBadge {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                count: hitPage.modelData.count
                            }
                        }
                        Label {
                            anchors.top: paper.bottom
                            anchors.topMargin: 2
                            anchors.horizontalCenter: paper.horizontalCenter
                            text: hitPage.modelData.page + 1
                            font.pixelSize: 11
                            color: "#5f6368"
                        }
                    }
                    background: null
                }
            }
        }

        // Select without opening
        AbstractButton {
            objectName: "cardCheck"
            x: 4
            y: 4
            z: 2
            width: 44
            height: 44
            visible: card.selected || card.selectionMode || hover.hovered
            onClicked: card.toggleRequested()
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    radius: 13
                    color: card.selected ? Material.accentColor : "#e6ffffff"
                    border.width: 2
                    border.color: card.selected ? Material.accentColor : "#80868b"
                    Label {
                        anchors.centerIn: parent
                        visible: card.selected
                        text: "✓"
                        color: "#ffffff"
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                }
            }
            background: null
        }

        MouseArea {
            id: area
            anchors.fill: parent
            z: -1  // under the ⋮ button
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            pressAndHoldInterval: 450
            property bool held: false
            property bool dragging: false
            property point pressPos
            onPressed: function(mouse) { pressPos = Qt.point(mouse.x, mouse.y); held = false }
            onClicked: function(mouse) {
                if (held) return
                if (mouse.button === Qt.RightButton) card.menuRequested(area, mouse.x, mouse.y)
                else card.activated(mouse.modifiers)
            }
            onPressAndHold: function(mouse) {
                if (mouse.button !== Qt.LeftButton) return
                held = true
                if (card.dragOverlay) preventStealing = true  // the grid must not scroll now
            }
            onPositionChanged: function(mouse) {
                if (!held || !card.dragOverlay) return
                if (!dragging && Math.hypot(mouse.x - pressPos.x, mouse.y - pressPos.y) > 8) {
                    dragging = true
                    card.dragOverlay.start(card.row, card.name, card.isFolder, mapToItem(card.dragOverlay, mouse.x, mouse.y))
                }
                if (dragging) card.dragOverlay.moveTo(mapToItem(card.dragOverlay, mouse.x, mouse.y))
            }
            onReleased: function(mouse) {
                if (dragging) card.dragOverlay.finish()
                else if (held) card.menuRequested(area, mouse.x, mouse.y)  // held without moving: the menu
                dragging = false
                preventStealing = false
            }
            onCanceled: {
                if (dragging) card.dragOverlay.stop()
                dragging = false
                held = false
                preventStealing = false
            }
        }
    }
}
