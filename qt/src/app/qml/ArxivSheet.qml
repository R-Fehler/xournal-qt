// arXiv (qt/docs/features/citations.md): search by title, or look up an arXiv ID, then download the paper's PDF into
// the library, named by its title. The app's own networking is opt-in: the first request asks (what goes where), and
// Settings can turn it off. Every address is shown next to its button before anything is fetched.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: sheet
    objectName: "arxivSheet"
    preferredWidth: 640
    closeButton: true
    title: sheet.mode === "id" ? qsTr("arXiv %1").arg(sheet.arxivId) : qsTr("Search arXiv")

    /// "search" (by title) or "id" (one arXiv ID)
    property string mode: "search"
    property string arxivId: ""
    readonly property var c: app.citations
    readonly property string requestUrl: mode === "id" ? c.arxivLookUpUrl(arxivId)
                                         : (titleField.text.trim() !== "" ? c.arxivSearchUrl(titleField.text) : "")
    readonly property string access: (app.settings.revision, c.networkAccess())
    /// The folder of the library the paper goes into (relative; "" the top)
    property string folder: ""
    property var folders: [""]
    /// The results changed since a download: which ones are in the chosen folder already (read again when needed)
    property int filesRevision: 0

    function openSearch(title) {
        mode = "search"
        titleField.text = title
        prepare()
    }
    function openId(id) {
        mode = "id"
        arxivId = id
        prepare()
    }
    function prepare() {
        folders = c.libraryFolders()
        folder = c.currentFolder()
        open()
    }
    /// The request, once networking is allowed (the first time: asked)
    function request() {
        if (access === "ask") {
            optIn.then = sheet.request
            optIn.open()
            return
        }
        if (mode === "id") c.arxivLookUp(arxivId)
        else c.arxivSearch(titleField.text)
    }
    function download(index) {
        if (access === "ask") {
            optIn.then = function() { sheet.download(index) }
            optIn.open()
            return
        }
        c.arxivDownload(index, folder)
    }


    Connections {
        target: sheet.c
        function onPaperDownloaded(path) { sheet.filesRevision++ }
    }

    ColumnLayout {
        width: sheet.availableWidth
        spacing: 8

        TextField {
            id: titleField
            objectName: "arxivTitle"
            visible: sheet.mode === "search"
            Layout.fillWidth: true
            selectByMouse: true
            placeholderText: qsTr("Title")
            onAccepted: if (sheet.access !== "off") sheet.request()
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "arxivRequest"
                text: sheet.mode === "id" ? qsTr("Look it up on arXiv") : qsTr("Search arXiv")
                highlighted: true
                enabled: sheet.requestUrl !== "" && sheet.access !== "off" && !sheet.c.arxivBusy
                onClicked: sheet.request()
            }
            Label {
                objectName: "arxivRequestUrl"
                Layout.fillWidth: true
                text: sheet.requestUrl
                color: "#6b6f75"
                font.pixelSize: 11
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
        Label {
            objectName: "arxivStatus"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: sheet.c.arxivError !== "" || sheet.access === "off" ? "#b3261e" : "#50555b"
            text: sheet.access === "off" ? qsTr("Connecting to arXiv is turned off (Settings → Documents → Web and citations).")
                  : sheet.c.arxivWaiting ? qsTr("Waiting for arXiv (one request every 3 seconds)…")
                  : sheet.c.arxivBusy ? qsTr("Asking arXiv…")
                  : sheet.c.arxivError
            visible: text !== ""
        }

        // Where it goes
        RowLayout {
            Layout.fillWidth: true
            visible: sheet.c.arxivResults.length > 0
            Label { text: qsTr("Save into"); color: "#50555b" }
            ComboBox {
                objectName: "arxivFolder"
                Layout.fillWidth: true
                model: sheet.folders.map(function(f) { return f === "" ? qsTr("%1 (the library)").arg(app.library.name) : f })
                currentIndex: Math.max(0, sheet.folders.indexOf(sheet.folder))
                onActivated: function(index) { sheet.folder = sheet.folders[index]; sheet.filesRevision++ }
            }
        }

        ListView {
            id: list
            objectName: "arxivResults"
            Layout.fillWidth: true
            // (all of them: the sheet's body scrolls)
            Layout.preferredHeight: contentHeight
            interactive: false
            spacing: 6
            model: sheet.c.arxivResults
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool inLibrary: (sheet.filesRevision, sheet.folder, sheet.c.downloadExists(index, sheet.folder))
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
                    Label {
                        objectName: "arxivResultTitle"
                        Layout.fillWidth: true
                        text: row.modelData.title
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: [row.modelData.authors, row.modelData.year, "arXiv:" + row.modelData.full]
                                .filter(function(s) { return s !== "" }).join(" · ")
                        color: "#6b6f75"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.inLibrary ? qsTr("In the library: %1").arg(row.modelData.fileName)
                                            : qsTr("Saved as: %1").arg(row.modelData.fileName)
                        color: row.inLibrary ? "#1b7a3a" : "#50555b"
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    Label {
                        objectName: "arxivPdfUrl"
                        Layout.fillWidth: true
                        visible: !row.inLibrary
                        text: row.modelData.pdfUrl
                        color: "#6b6f75"
                        font.pixelSize: 11
                        font.family: "monospace"
                        elide: Text.ElideMiddle
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 2
                        Button {
                            objectName: "arxivDownload"
                            visible: !row.inLibrary
                            text: qsTr("Download into the library")
                            flat: true
                            enabled: sheet.access !== "off" && !sheet.c.arxivBusy
                            onClicked: sheet.download(row.index)
                        }
                        Button {
                            objectName: "arxivOpenReference"
                            visible: row.inLibrary
                            text: qsTr("Open as reference")
                            flat: true
                            onClicked: { const p = sheet.c.downloadPath(row.index, sheet.folder); sheet.close(); app.openAsReference(p) }
                        }
                        Button {
                            objectName: "arxivOpenTab"
                            visible: row.inLibrary
                            text: qsTr("Open in a tab")
                            flat: true
                            onClicked: { const p = sheet.c.downloadPath(row.index, sheet.folder); sheet.close(); app.openPath(p) }
                        }
                        Button {
                            objectName: "arxivAbs"
                            text: qsTr("Open on arxiv.org")
                            flat: true
                            onClicked: { const u = row.modelData.absUrl; win.actions.openWebAddress(u, text) }
                        }
                    }
                }
            }
        }
    }

    // The first request: what the app sends where, and the choice (kept in Settings)
    AdaptiveDialog {
        id: optIn
        objectName: "networkOptIn"
        kind: "question"
        preferredWidth: 480
        title: qsTr("Connect to arXiv?")
        property var then: null
        onAccepted: {
            app.settings.set("networkAccess", "on")
            const f = optIn.then
            optIn.then = null
            if (f) f()
        }
        ColumnLayout {
            width: optIn.availableWidth
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Searching sends the words of the title to export.arxiv.org; downloading fetches the "
                           + "paper's PDF from arxiv.org into your library. Nothing else is sent, and no account is "
                           + "used. The address is shown before each request.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#6b6f75"
                font.pixelSize: 12
                text: qsTr("You can turn this off in Settings → Documents → Web and citations.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "networkOptInNo"
                text: qsTr("Not now")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "networkOptInAllow"
                text: qsTr("Allow")
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }
}
