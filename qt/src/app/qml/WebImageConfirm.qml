// A web picture of a Markdown text is never fetched unasked (qt/docs/md-images.md): its "Load image" shows the whole
// address and where it goes, and, while connecting to the web was not decided yet, what that means (the same opt-in
// as for arXiv, qt/docs/citations.md). "Load" fetches it into the app's cache; Cancel sends nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: dlg
    objectName: "webImageConfirm"
    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 32 : 520, 520)
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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

    background: Rectangle { color: "#ffffff"; radius: 12; border.width: 1; border.color: "#d5d8dc" }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8
        Label {
            text: qsTr("Load this picture from the web?")
            font.pixelSize: 17
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
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
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: "webImageCancel"
                text: qsTr("Cancel")
                flat: true
                onClicked: dlg.close()
            }
            Button {
                objectName: "webImageLoad"
                text: qsTr("Load")
                highlighted: true
                onClicked: {
                    const u = dlg.url
                    dlg.close()
                    app.loadWebImage(u)
                }
            }
        }
    }
}
