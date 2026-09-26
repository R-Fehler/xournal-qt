// "Find this paper" (qt/docs/citations.md): a bibliography entry (selected text) is looked for in the library by its
// title, not by file names (arXiv papers are named by numbers). The guessed title can be corrected; each hit opens
// beside the notes (the reference), in a tab, or copies a link to paste onto the citation. Not in the library: Google
// Scholar and arXiv, with their addresses shown.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: sheet
    objectName: "findPaperSheet"
    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 24 : 640, 640)
    height: Math.min(parent ? parent.height - 48 : 640, 640)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// The cleaned entry (the fallback; the whole of it is matched as well)
    property string raw: ""
    readonly property var hits: app.citations.paperHits
    readonly property bool searching: app.citations.searchingPapers
    readonly property string arxivUrl: titleField.text.trim() !== "" ? app.citations.arxivSearchUrl(titleField.text) : ""
    readonly property string scholarUrl: titleField.text.trim() !== "" ? app.citations.scholarUrl(titleField.text) : ""

    /// The documents shown when it opened (the reference is in one of them: it is not its own paper)
    property var shownFiles: []
    function openFor(text) {
        const guess = app.citations.guessTitle(text)
        raw = guess.raw
        shownFiles = app.shownDocumentFiles()
        titleField.text = guess.title
        guessed = guess.title
        open()
        search()
    }
    /// The title as guessed: the whole entry is matched too while it is not changed (a changed title is what is
    /// wanted, and the entry would still find the paper it names)
    property string guessed: ""
    function search() {
        app.citations.findPapers(titleField.text, titleField.text === guessed ? raw : "", shownFiles)
    }

    background: Rectangle { color: "#fafafa"; radius: 14; border.width: 1; border.color: "#d5d8dc" }

    Timer { id: typing; interval: 300; onTriggered: sheet.search() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Find this paper in the library")
                font.pixelSize: 18
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            IconButton { iconName: "xqt-close"; tip: qsTr("Close"); onClicked: sheet.close() }
        }
        Label { text: qsTr("Title"); color: "#50555b"; font.pixelSize: 13 }
        TextField {
            id: titleField
            objectName: "findPaperTitle"
            Layout.fillWidth: true
            selectByMouse: true
            onTextEdited: typing.restart()
            onAccepted: { typing.stop(); sheet.search() }
        }
        Label {
            objectName: "findPaperRaw"
            Layout.fillWidth: true
            text: qsTr("From: %1").arg(sheet.raw)
            visible: sheet.raw !== "" && sheet.raw !== titleField.text
            color: "#6b6f75"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Label {
            objectName: "findPaperStatus"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#50555b"
            text: sheet.searching ? qsTr("Looking through the library…")
                  : sheet.hits.length === 0 ? qsTr("No document of the library has this title.")
                  : sheet.hits.length === 1 ? qsTr("1 document") : qsTr("%1 documents").arg(sheet.hits.length)
        }
        Label {
            Layout.fillWidth: true
            visible: app.library.indexing
            wrapMode: Text.Wrap
            color: "#b06000"
            font.pixelSize: 12
            text: qsTr("The library is still being read (%1 of %2): papers read later are not found yet.")
                    .arg(app.library.indexed).arg(app.library.indexTotal)
        }

        ListView {
            id: list
            objectName: "findPaperHits"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: sheet.hits
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                implicitHeight: rowColumn.implicitHeight + 16
                radius: 10
                color: "#ffffff"
                border.width: 1
                border.color: "#e3e5e8"
                ColumnLayout {
                    id: rowColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            objectName: "findPaperHitTitle"
                            Layout.fillWidth: true
                            text: row.modelData.title
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "findPaperHitScore"
                            text: qsTr("%1 %").arg(row.modelData.score)
                            color: row.modelData.score >= 85 ? "#1b7a3a" : "#8a6d00"
                            font.pixelSize: 12
                            ToolTip.visible: scoreHover.hovered
                            ToolTip.text: row.modelData.matched === "title" ? qsTr("Matched by the PDF's title")
                                        : row.modelData.matched === "heading" ? qsTr("Matched by the title on its first page")
                                        : row.modelData.matched === "name" ? qsTr("Matched by its file name")
                                        : qsTr("Matched by the text of its first page")
                            HoverHandler { id: scoreHover }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.folder !== "" ? row.modelData.folder + " / " + row.modelData.fileName
                                                          : row.modelData.fileName
                        color: "#6b6f75"
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 2
                        Button {
                            objectName: "findPaperReference"
                            text: qsTr("Open as reference")
                            flat: true
                            onClicked: { const p = row.modelData.path; sheet.close(); app.openAsReference(p) }
                        }
                        Button {
                            objectName: "findPaperTab"
                            text: qsTr("New tab")
                            flat: true
                            onClicked: { const p = row.modelData.path; sheet.close(); app.openPath(p) }
                        }
                        Button {
                            objectName: "findPaperCopyLink"
                            text: qsTr("Copy link")
                            flat: true
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Paste it onto the citation number or into the margin: a link to the paper")
                            onClicked: { const p = row.modelData.path; sheet.close(); app.copyDocumentLink(p, -1) }
                        }
                    }
                }
            }
        }

        // Not in the library: the web, with its address shown
        Rectangle { Layout.fillWidth: true; height: 1; color: "#e3e5e8" }
        Label {
            text: sheet.hits.length === 0 && !sheet.searching ? qsTr("Not in the library? Search the web:")
                                                               : qsTr("Or search the web:")
            color: "#50555b"
            font.pixelSize: 13
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Button {
                objectName: "findPaperScholar"
                text: qsTr("Search in Google Scholar")
                flat: true
                enabled: sheet.scholarUrl !== ""
                onClicked: { const u = sheet.scholarUrl; sheet.close(); win.openWebAddress(u, text) }
            }
            Label {
                objectName: "findPaperScholarUrl"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                text: sheet.scholarUrl !== "" ? app.citations.displayUrl(sheet.scholarUrl) : ""
                color: "#6b6f75"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Button {
                objectName: "findPaperArxiv"
                text: qsTr("Search arXiv")
                flat: true
                enabled: sheet.arxivUrl !== ""
                onClicked: { const t = titleField.text; sheet.close(); win.arxivSearch(t) }
            }
            Label {
                objectName: "findPaperArxivUrl"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                text: sheet.arxivUrl
                color: "#6b6f75"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }
}
