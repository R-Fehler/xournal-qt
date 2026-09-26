// A web picture of a Markdown text is never fetched unasked (qt/docs/md-images.md): its "Load image" shows the whole
// address and where it goes, and, while connecting to the web was not decided yet, what that means (the same opt-in
// as for arXiv, qt/docs/citations.md). "Load" fetches it into the app's cache; Cancel sends nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "webImageConfirm"
    kind: "question"
    preferredWidth: 520
    title: qsTr("Load this picture from the web?")

    property string url: ""
    property string host: ""
    /// Settings `networkAccess`: "ask" (not decided yet), "on"
    property string access: "ask"

    function ask(address, where, how) {
        if (how === "off") {
            app.loadWebImage(address)  // (it says that connecting is off)
            return
        }
        url = address
        host = where
        access = how
        open()
    }

    onAccepted: app.loadWebImage(dlg.url)

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 8
        Label {
            objectName: "webImageHost"
            text: qsTr("It is fetched from %1 and kept in the app's cache (not in the document):").arg(dlg.host)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#50555b"
        }
        TextArea {
            objectName: "webImageUrl"
            Layout.fillWidth: true
            Layout.maximumHeight: 160
            text: dlg.url
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
            font.family: "monospace"
            font.pixelSize: 13
            background: Rectangle { color: "#f1f3f4"; radius: 6 }
        }
        Label {
            objectName: "webImageOptIn"
            visible: dlg.access === "ask"
            text: qsTr("The app does not connect to the web unless you allow it. Loading allows it from now on (also for "
                       + "arXiv); the address is shown before each request. You can turn this off in Settings → "
                       + "Documents → Web and citations.")
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#6b6f75"
            font.pixelSize: 12
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "webImageCancel"
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "webImageLoad"
            text: qsTr("Load")
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
