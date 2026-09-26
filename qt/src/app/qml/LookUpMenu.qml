// "Look up" on selected text (qt/docs/citations.md): the pill of selected PDF text and the context pill open it. A
// bibliography entry finds its paper in the library (FindPaperSheet). Each
// entry that leads to the web shows its address under its name, so the address is seen before it is chosen; the
// window then asks with the whole address (WebConfirm) unless that was turned off.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Menu {
    id: menu
    objectName: named("lookUpMenu")
    width: 340
    /// The selected text it looks up (set before it opens)
    property string text: ""
    property string namePrefix: ""
    function named(n) { return namePrefix === "" ? n : namePrefix + n.charAt(0).toUpperCase() + n.slice(1) }

    readonly property string scholarUrl: text !== "" ? app.citations.scholarUrl(text) : ""
    /// The arXiv IDs in the text: [{ id, full, absUrl, pdfUrl, lookUpUrl }]
    readonly property var arxivIds: text !== "" ? app.citations.arxivIdsIn(text) : []
    readonly property string translateUrl: (app.settings.revision, text !== "" ? app.citations.translateUrl(text) : "")

    /// An entry with its web address as a second line
    component WebItem: MenuItem {
        id: item
        property string url: ""
        property string label: ""
        property string purpose: label
        /// What it does instead of opening the address in the browser (null: that)
        property var run: null
        text: label
        height: visible ? implicitHeight : 0
        visible: url !== ""
        contentItem: ColumnLayout {
            spacing: 0
            Label { text: item.label; font: item.font; color: item.enabled ? "#202124" : "#9aa0a6" }
            Label {
                objectName: item.objectName + "Url"
                text: item.url !== "" ? app.citations.displayUrl(item.url) : ""
                font.pixelSize: 11
                color: "#6b6f75"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        onTriggered: run ? run() : win.openWebAddress(url, purpose)
    }

    MenuItem {
        objectName: menu.named("lookUpFindPaper")
        text: qsTr("Find this paper in the library")
        enabled: menu.text !== ""
        onTriggered: win.findPaper(menu.text)
    }
    // An arXiv ID in the text (the first one): its paper into the library (the arXiv sheet looks up its title first,
    // to name the file, showing that address again with its button), and its page in the browser
    WebItem {
        objectName: menu.named("lookUpArxiv")
        label: menu.arxivIds.length > 0 ? qsTr("arXiv %1: into the library…").arg(menu.arxivIds[0].full) : ""
        url: menu.arxivIds.length > 0 ? menu.arxivIds[0].lookUpUrl : ""
        run: function() { win.arxivPaper(menu.arxivIds[0].full) }
    }
    WebItem {
        objectName: menu.named("lookUpArxivPage")
        label: menu.arxivIds.length > 0 ? qsTr("arXiv %1 on arxiv.org").arg(menu.arxivIds[0].full) : ""
        purpose: qsTr("The paper on arxiv.org")
        url: menu.arxivIds.length > 0 ? menu.arxivIds[0].absUrl : ""
    }
    MenuSeparator {}
    WebItem {
        objectName: menu.named("lookUpScholar")
        label: qsTr("Search in Google Scholar")
        url: menu.scholarUrl
    }
    WebItem {
        objectName: menu.named("lookUpTranslate")
        label: qsTr("Translate")
        url: menu.translateUrl
    }
}
