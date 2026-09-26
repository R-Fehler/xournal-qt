// A document or folder in the library / recent grids: first-page preview (or a folder), name and details.
// Tap to open (while items are selected: to select it too); Ctrl / Shift + click and the circle in the corner select;
// right click, the ⋮ button or press and hold (without moving) for the menu; press and hold, then move to drag it
// (with the other selected items) onto a folder (when `dragOverlay` is set).
// Extended library search (`stripHeight` > 0): below the title, the pages with hits (marked), side by side;
// tapping one opens the document at that page. A Markdown file shows a card per passage with hits instead: the
// passage drawn as it is formatted, the headings above it on top; tapping one opens the file there.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Fuzzy.js" as Fuzzy

Item {
    id: card
    property string name
    /// Fuzzy search: the characters of the name its query matched (highlighted)
    property var nameMarks: []
    readonly property string markedName: Fuzzy.marked(name, nameMarks, "#c2410c")
    property string path
    property string subtitle
    property string preview
    property bool isFolder: false
    /// A folder opened as a library (Recent): the folder with a library mark
    property bool isLibrary: false
    property int itemCount: 0
    property bool hasPdf: false
    property bool hasXopp: false
    /// "notes", "pdf", "md", "image", "text", "other" (a PDF or an image with its .xopp: "pdf" / "image" and hasXopp)
    property string kind
    /// A document whose file is a PDF: what the PDF is (the library index knows it): "plain", "notes" (a PDF with
    /// notes), "text" (a PDF text document), "archive", "archive-text"; "" while not known
    property string pdfKind
    readonly property bool pdfText: pdfKind === "text" || pdfKind === "archive-text"
    readonly property bool pdfArchive: pdfKind === "archive" || pdfKind === "archive-text"
    /// A text or other file: the icon of its type (shown instead of a preview for other files)
    property string fileIcon
    /// A text or other file: its extension in capitals ("DOCX"; "" without one)
    readonly property string extension: {
        if (kind !== "other" && kind !== "text") return ""
        const dot = name.lastIndexOf(".")
        return dot > 0 && name.length - dot <= 6 ? name.substring(dot + 1).toUpperCase() : ""
    }
    /// When it was last read in this app (formatted; "": never) and at which page (0-based; -1: not known)
    property string lastRead
    property int lastPage: -1
    property int hits: 0
    /// Conflict copies of sync apps next to it (their number): a badge that opens "compare / keep one"
    property int conflicts: 0
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
    /// Markdown: the passages with hits, [{ passage, count, headings }], and their image URL base
    property var hitPassages: []
    property string hitPassageBase
    signal pageActivated(int page)
    signal passageActivated(int passage)
    /// A tap or click (with the keyboard modifiers of a click)
    signal activated(int modifiers)
    signal toggleRequested()
    signal conflictsRequested()
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
                    visible: !card.isFolder && card.kind !== "other"
                    anchors.fill: parent
                    anchors.margins: 6
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    source: card.isFolder || !card.active || card.kind === "other" ? "" : card.preview
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
                    visible: previewImage.visible && card.preview !== "" && previewImage.status === Image.Loading
                    running: visible
                    implicitWidth: 36
                    implicitHeight: 36
                }
                Image {
                    id: folderIcon
                    visible: card.isFolder
                    anchors.centerIn: parent
                    source: app.iconUrl("xqt-folder")
                    readonly property int iconSize: Math.min(96, previewBox.width * 0.45)
                    sourceSize: Qt.size(iconSize, iconSize)
                    opacity: 0.8
                }
                // A library: its mark on the folder
                Rectangle {
                    objectName: "libraryMark"
                    visible: card.isLibrary
                    readonly property int size: Math.round(folderIcon.iconSize * 0.5)
                    x: folderIcon.x + folderIcon.width - size * 0.7
                    y: folderIcon.y + folderIcon.height - size * 0.8
                    width: size
                    height: size
                    radius: size / 2
                    color: "#ffffff"
                    border.width: 1
                    border.color: "#c5cae9"
                    Image {
                        anchors.centerIn: parent
                        source: app.iconUrl("xqt-library")
                        sourceSize: Qt.size(parent.size * 0.62, parent.size * 0.62)
                    }
                }
                // Another file: the icon of its type
                Image {
                    objectName: "fileTypeIcon"
                    visible: !card.isFolder && card.kind === "other"
                    anchors.centerIn: parent
                    source: visible ? app.iconUrl(card.fileIcon !== "" ? card.fileIcon : "xqt-file") : ""
                    readonly property int iconSize: Math.min(96, previewBox.width * 0.45)
                    sourceSize: Qt.size(iconSize, iconSize)
                    opacity: 0.85
                }
                // "PDF" for documents with a PDF (annotated or not), "MD" for Markdown files, "IMG" for images, the
                // extension of text and other files. With notes (a .xopp next to it, or in the PDF): "PDF ✎"; a PDF
                // text document: "PDF M↓" in blue (the Markdown mark: a PDF that carries Markdown); an archive PDF: "PDF/A" (qt/docs/library.md, "Kinds of PDFs").
                Rectangle {
                    id: pdfBadge
                    objectName: "kindBadge"
                    readonly property bool isPdf: card.hasPdf || card.kind === "pdf"
                    readonly property bool withNotes: card.hasXopp || (isPdf && card.pdfKind !== "" && card.pdfKind !== "plain")
                    readonly property string label: isPdf ? (card.pdfArchive ? qsTr("PDF/A") : qsTr("PDF"))
                                                    : card.kind === "md" ? qsTr("MD")
                                                    : card.kind === "image" ? qsTr("IMG")
                                                    : card.kind === "text" ? (card.extension !== "" ? card.extension : qsTr("TXT"))
                                                    : card.kind === "other" ? card.extension : ""
                    /// What the badge stands for, in words (its tooltip and accessible name)
                    readonly property string description:
                        isPdf ? (card.pdfText ? (card.pdfArchive ? qsTr("PDF text document (archive PDF)") : qsTr("PDF text document"))
                                 : card.pdfArchive ? qsTr("Archive PDF with notes")
                                 : card.pdfKind === "notes" ? qsTr("PDF with notes")
                                 : card.hasXopp ? qsTr("PDF with notes (its .xopp next to it)") : qsTr("PDF"))
                        : card.kind === "md" ? qsTr("Markdown file")
                        : card.kind === "image" ? (card.hasXopp ? qsTr("Image with notes") : qsTr("Image"))
                        : card.kind === "text" ? qsTr("Text file")
                        : label
                    visible: !card.isFolder && label !== ""
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    radius: 4
                    color: card.kind === "md" ? "#455a64" : card.kind === "image" ? "#00897b"
                           : card.kind === "text" ? "#6d4c41" : card.kind === "other" ? "#5f6368"
                           : isPdf && card.pdfText ? "#1565c0" : "#d93025"
                    width: pdfLabel.implicitWidth + 8
                    height: 16
                    Accessible.role: Accessible.StaticText
                    Accessible.name: description
                    Label {
                        id: pdfLabel
                        objectName: "kindBadgeText"
                        anchors.centerIn: parent
                        text: pdfBadge.isPdf && card.pdfText ? pdfBadge.label + " M↓"
                              : pdfBadge.withNotes ? pdfBadge.label + " ✎" : pdfBadge.label
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        color: "#ffffff"
                    }
                    HoverHandler { id: badgeHover }
                    ToolTip.visible: badgeHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: description
                }
                HitBadge {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    anchors.topMargin: pdfBadge.visible ? 26 : 6
                    count: card.hits
                }
                // Last read in this app, and at which page - a tag like "PDF", in the accent of "Last page"
                Rectangle {
                    objectName: "lastReadTag"
                    visible: !card.isFolder && card.lastRead !== "" && card.snippet === ""
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    radius: 4
                    color: Material.accentColor
                    width: readRow.implicitWidth + 8
                    height: 16
                    Row {
                        id: readRow
                        anchors.centerIn: parent
                        spacing: 3
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            source: app.iconUrl("xqt-clock-light")
                            sourceSize: Qt.size(10, 10)
                        }
                        Label {
                            objectName: "lastReadText"
                            anchors.verticalCenter: parent.verticalCenter
                            text: card.lastRead + (card.lastPage >= 0 ? " · " + qsTr("p.%1").arg(card.lastPage + 1) : "")
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            color: "#ffffff"
                        }
                    }
                }
                // Sync conflicts: a badge that opens "compare / keep one" (conflictsRequested)
                Rectangle {
                    objectName: "conflictBadge"
                    visible: !card.isFolder && card.conflicts > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    // (above the "last read" tag, which a narrow card fills across)
                    anchors.bottomMargin: card.lastRead !== "" && card.snippet === "" ? 28 : 6
                    z: 3
                    radius: 4
                    color: "#e65100"
                    width: conflictLabel.implicitWidth + 12
                    height: 22
                    Label {
                        id: conflictLabel
                        anchors.centerIn: parent
                        text: card.conflicts === 1 ? qsTr("Conflict") : qsTr("%1 conflicts").arg(card.conflicts)
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        color: "#ffffff"
                    }
                    MouseArea {
                        objectName: "conflictBadgeArea"
                        anchors.fill: parent
                        anchors.margins: -6  // (a finger's size)
                        onClicked: card.conflictsRequested()
                    }
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
                        text: card.markedName !== "" ? card.markedName : card.name
                        textFormat: card.markedName !== "" ? Text.StyledText : Text.AutoText
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
            // A Markdown file: the passages with hits, as snippet cards
            ListView {
                id: passageStrip
                objectName: "hitPassageStrip"
                visible: card.stripHeight > 0 && card.hitPassages.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: card.stripHeight
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: visible ? card.hitPassages : []
                cacheBuffer: Math.max(0, width)
                ScrollBar.horizontal: ScrollBar { height: 6 }
                delegate: AbstractButton {
                    id: passageCard
                    required property var modelData
                    objectName: "hitPassage"
                    width: Math.round(Math.max(120, Math.min(passageStrip.height * 1.8, passageStrip.width * 0.9)))
                    height: passageStrip.height - 8
                    onClicked: card.passageActivated(modelData.passage)
                    contentItem: Rectangle {
                        color: "#ffffff"
                        border.width: passageCard.hovered ? 2 : 1
                        border.color: passageCard.hovered ? Material.accentColor : "#d5d8dc"
                        radius: 4
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 2
                            Label {
                                objectName: "hitPassageHeadings"
                                Layout.fillWidth: true
                                Layout.rightMargin: 22
                                visible: text !== ""
                                text: passageCard.modelData.headings
                                elide: Text.ElideLeft
                                font.pixelSize: 10
                                font.italic: true
                                color: "#5f6368"
                            }
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                    horizontalAlignment: Image.AlignLeft
                                    verticalAlignment: Image.AlignTop
                                    source: card.active && card.hitPassageBase !== ""
                                            ? card.hitPassageBase + "/" + passageCard.modelData.passage : ""
                                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                }
                            }
                        }
                        HitBadge {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 3
                            count: passageCard.modelData.count
                        }
                    }
                    background: null
                }
            }
            // The pages with hits
            ListView {
                id: strip
                objectName: "hitPageStrip"
                visible: card.stripHeight > 0 && card.hitPassages.length === 0
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
                    text: card.hits > 0 ? qsTr("Found in the text") : qsTr("Found in the name")
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
                    // Press and hold, or a right click: a link to this page (qt/docs/links.md)
                    onPressAndHold: hitMenu.popup()
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: hitMenu.popup() }
                    Menu {
                        id: hitMenu
                        MenuItem {
                            objectName: "copyHitPageLink"
                            text: qsTr("Copy link to this page")
                            onTriggered: app.copyDocumentLink(card.path, hitPage.modelData.page)
                        }
                    }
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
            visible: !card.isLibrary && (card.selected || card.selectionMode || hover.hovered)
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
