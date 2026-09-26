// "Look up" on selected text (qt/docs/citations.md): the pill of selected PDF text and the context pill open it. Each
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
    readonly property string translateUrl: (app.settings.revision, text !== "" ? app.citations.translateUrl(text) : "")

    /// An entry with its web address as a second line
    component WebItem: MenuItem {
        id: item
        property string url: ""
        property string label: ""
        property string purpose: label
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
        onTriggered: win.openWebAddress(url, purpose)
    }

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
