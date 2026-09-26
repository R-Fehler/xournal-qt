// Before a web address opens in the browser (qt/docs/citations.md): the whole address, exactly as it is opened, with
// where it goes, and Open / Copy address / Cancel. "Don't ask again" turns the question off (Settings → Documents →
// Web and citations turns it on again); the look-up menu still shows each address before it is chosen.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: dlg
    objectName: "webConfirm"
    kind: "question"
    preferredWidth: 520
    title: dlg.what !== "" ? dlg.what : qsTr("Open in the browser?")

    /// The address (exactly what is opened) and what it is for ("Search in Google Scholar")
    property string url: ""
    property string what: ""
    readonly property string host: url !== "" ? app.citations.hostOf(url) : ""

    function ask(address, purpose) {
        url = address
        what = purpose === undefined ? "" : purpose
        dontAsk.checked = false
        open()
    }
    function openIt() {
        if (dontAsk.checked) app.settings.set("webConfirm", false)
        const u = url
        close()
        app.citations.openWeb(u)
    }

    onAccepted: openIt()

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 8
        Label {
            objectName: "webConfirmHost"
            text: qsTr("Opens this address on %1 in the browser:").arg(dlg.host)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#50555b"
        }
        TextArea {
            objectName: "webConfirmUrl"
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
        CheckBox {
            id: dontAsk
            objectName: "webConfirmDontAsk"
            text: qsTr("Don't ask again (the menu still shows each address)")
            Layout.fillWidth: true
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "webConfirmCopy"
            text: qsTr("Copy address")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: { app.citations.copyText(dlg.url); dlg.close() }
        }
        Button {
            objectName: "webConfirmCancel"
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "webConfirmOpen"
            text: qsTr("Open")
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
