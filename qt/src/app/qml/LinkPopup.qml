// xournal-qt: Main.qml's part; a direct child of the window (it places itself in the content item's coordinates).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A tapped link: open it / go to the page (not at once: a tap can be a mistake). A link to a document
// (qt/docs/links.md) offers a new tab, the reference or "here", unless a choice was remembered (Settings). A page
// or a place of this document offers going there, or showing it in the reference: a second view of the document
// beside it (qt/self-reference).
Popup {
    id: linkPopup
    objectName: "linkPopup"
    property string uri
    property int page: -1
    property var doc: null  // app.documentLink(uri) of a link to a document, else null
    property bool inDocument: false  // a link to a place of this document (`#page=…`, a chapter)
    padding: 6
    function follow(how) {
        if (linkRemember.checked) app.settings.set("linkOpening", how)
        const uri = linkPopup.uri
        linkPopup.close()
        app.followDocumentLink(uri, how)
    }
    Connections {
        target: app
        function onLinkTapped(uri, page, rect) {
            const info = uri !== "" ? app.documentLink(uri) : null
            if (info && info.document && info.found && !info.here) {
                const how = (app.settings.revision, app.settings.get("linkOpening"))
                if (how !== "ask") {
                    app.followDocumentLink(uri, how)
                    return
                }
            }
            const inDocument = !!(info && info.document && info.here)
            if (inDocument) {  // (a place in this document: there, or in the reference if that was chosen)
                const how = (app.settings.revision, app.settings.get("linkOpening"))
                if (how !== "ask") {
                    app.followDocumentLink(uri, how === "reference" ? "reference" : "here")
                    return
                }
            }
            linkPopup.uri = uri
            linkPopup.page = page
            linkPopup.doc = info && info.document ? info : null
            linkPopup.inDocument = inDocument
            linkRemember.checked = false
            linkPopup.x = Math.max(8, Math.min(canvas.x + rect.x, win.width - linkPopup.width - 8))
            linkPopup.y = canvas.y + rect.y + rect.height + 6
            if (linkPopup.y + 120 > win.height) linkPopup.y = canvas.y + rect.y - (linkPopup.doc ? 120 : 60)
            linkPopup.open()
        }
    }
    ColumnLayout {
        spacing: 2
        RowLayout {
            spacing: 4
            Image { source: app.iconUrl("xqt-link"); sourceSize.width: 18; sourceSize.height: 18; Layout.leftMargin: 6 }
            Label {
                objectName: "linkLabel"
                visible: linkPopup.uri !== ""
                text: !linkPopup.doc ? linkPopup.uri
                      : linkPopup.inDocument ? (linkPopup.doc.place !== "" ? linkPopup.doc.place : linkPopup.uri)
                      : !linkPopup.doc.found ? qsTr("%1 was not found").arg(linkPopup.doc.name)
                      : linkPopup.doc.place !== "" ? qsTr("%1, %2").arg(linkPopup.doc.name).arg(linkPopup.doc.place)
                                                   : linkPopup.doc.name
                elide: Text.ElideMiddle
                Layout.maximumWidth: 320
                Layout.rightMargin: linkPopup.doc ? 6 : 0
            }
            Button {
                objectName: "linkButton"
                visible: !linkPopup.doc || linkPopup.inDocument
                flat: true
                text: linkPopup.inDocument ? qsTr("Go there")
                      : linkPopup.uri !== "" ? qsTr("Open") : linkPopup.page >= 0 ? qsTr("Go to page %1").arg(linkPopup.page + 1)
                                                                                : qsTr("Page not in this document")
                enabled: linkPopup.uri !== "" || linkPopup.page >= 0
                onClicked: {
                    const uri = linkPopup.uri
                    linkPopup.close()
                    if (linkPopup.inDocument) app.followDocumentLink(uri, "here")
                    else if (uri !== "") app.openLink(uri)
                    else app.jumpToPage(linkPopup.page)
                }
            }
            // A page of this document: in a second view of it beside it (qt/self-reference)
            Button {
                objectName: "linkInReference"
                visible: linkPopup.inDocument || (linkPopup.uri === "" && linkPopup.page >= 0)
                flat: true
                text: qsTr("In the reference")
                onClicked: {
                    const uri = linkPopup.uri
                    linkPopup.close()
                    if (linkPopup.inDocument) app.followDocumentLink(uri, "reference")
                    else app.reference.showBeside(linkPopup.page)
                }
            }
        }
        RowLayout {
            visible: !!linkPopup.doc && linkPopup.doc.found && !linkPopup.inDocument
            spacing: 0
            Button {
                objectName: "linkNewTab"
                flat: true
                text: qsTr("Open in a new tab")
                onClicked: linkPopup.follow("tab")
            }
            Button {
                objectName: "linkAsReference"
                flat: true
                text: qsTr("Open as reference")
                onClicked: linkPopup.follow("reference")
            }
            Button {
                objectName: "linkHere"
                flat: true
                text: qsTr("Open here")
                onClicked: linkPopup.follow("here")
            }
        }
        CheckBox {
            id: linkRemember
            objectName: "linkRemember"
            visible: !!linkPopup.doc && linkPopup.doc.found && !linkPopup.inDocument
            text: qsTr("Remember my choice")
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Links open this way from now on (Settings → Documents)")
        }
    }
}
