// "Open in library…" for a zip (qt/docs/library.md, "Receiving a shared zip"): a zip opened with the app, from the
// file dialog or dropped on the library is unpacked into a new folder of the library, inside the folder chosen here
// (Inbox by default); a password is asked for when the zip has one. The work is app.libraryUnzip's (LibraryUnzip).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: unzip
    property string zip: ""
    property var info: ({})
    property string problem: ""
    readonly property var task: app.libraryUnzip

    function openZip(path) {
        zip = path
        info = app.inspectZip(path)
        problem = ""
        if (!info.ok) {
            problem = info.error
        } else if (info.encrypted && !info.supported) {
            problem = qsTr("This zip is protected with AES encryption, which this version of the app cannot read. "
                           + "Unpack it with another app and import the folder.")
        }
        const folders = app.library.folderList()
        let list = [{ folder: "Inbox", label: qsTr("Inbox") }]
        for (let i = 0; i < folders.length; ++i) {
            if (folders[i].folder === "Inbox")
                continue
            list.push({ folder: folders[i].folder,
                        label: folders[i].folder === "" ? folders[i].name : folders[i].folder })
        }
        target.model = list
        target.currentIndex = 0
        password.text = ""
        wrongText.text = ""
        dialog.open()
    }

    Connections {
        target: app
        function onZipOpened(path) { unzip.openZip(path) }
    }

    AdaptiveDialog {
        id: dialog
        objectName: "openZipDialog"
        preferredWidth: 520
        title: qsTr("Open in library")
        ColumnLayout {
            width: dialog.availableWidth
            spacing: 6
            Label {
                objectName: "openZipText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: unzip.problem !== "" ? unzip.problem
                      : !unzip.info.ok ? ""
                      : qsTr("“%1” goes into a new folder “%2” of the library: %n file(s)", "", unzip.info.files)
                            .arg(unzip.zip.split("/").pop()).arg(unzip.info.name)
                        + (unzip.info.share ? ", " + qsTr("with the library's readings") : "") + "."
            }
            Label {
                Layout.fillWidth: true
                visible: unzip.problem === ""
                text: qsTr("Into the folder")
            }
            ComboBox {
                id: target
                objectName: "openZipFolder"
                Layout.fillWidth: true
                visible: unzip.problem === ""
                textRole: "label"
                valueRole: "folder"
            }
            TextField {
                id: password
                objectName: "openZipPassword"
                Layout.fillWidth: true
                visible: unzip.problem === "" && unzip.info.encrypted === true
                echoMode: TextInput.Password
                placeholderText: qsTr("The zip's password")
            }
            Label {
                id: wrongText
                objectName: "openZipPasswordWrong"
                Layout.fillWidth: true
                visible: text !== ""
                color: "#c62828"
                wrapMode: Text.Wrap
                text: ""
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "openZipUnpack"
                text: qsTr("Unpack")
                visible: unzip.problem === ""
                enabled: !unzip.info.encrypted || password.text.length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            if (unzip.problem === "")
                app.unzipIntoLibrary(unzip.zip, target.currentValue, password.text)
        }
    }
    AdaptiveDialog {
        id: progress
        objectName: "openZipProgress"
        kind: "card"
        modal: false
        closePolicy: Popup.NoAutoClose
        preferredWidth: 460
        title: qsTr("Unpacking…")
        visible: unzip.task.running
        ColumnLayout {
            width: progress.availableWidth
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, unzip.task.total)
                value: unzip.task.done
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: unzip.task.current
                font.pixelSize: 13
                color: "#6b6f75"
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: app.cancelUnzip()
            }
        }
    }
    Connections {
        target: unzip.task
        function onFinished(result) {
            if (result.wrongPassword) {
                password.text = ""
                wrongText.text = result.error
                dialog.open()
            } else if (!result.ok && !result.cancelled) {
                unzip.problem = result.error
                dialog.open()
            }
        }
    }
}
