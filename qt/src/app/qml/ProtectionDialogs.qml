// xournal-qt: passwords (qt/docs/hybrid-pdf.md, "Encrypted PDFs"; Main.qml's part): the password a PDF needs to open,
// and protecting the document with one.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: protectionDialogs
    anchors.fill: parent
    visible: false
    readonly property alias protectDialog: protectDialog
    // A PDF that needs a password to open (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): asked here, kept in memory only
    // while the document is open. A wrong one is said so and asked again; Cancel leaves it closed.
    AdaptiveDialog {
        id: pdfPasswordDialog
        objectName: "pdfPasswordDialog"
        kind: "question"
        property string file: ""
        property bool wrong: false
        preferredWidth: 420
        title: qsTr("Password")
        onOpened: pdfPasswordField.forceActiveFocus()
        ColumnLayout {
            width: pdfPasswordDialog.availableWidth
            spacing: 8
            Label {
                objectName: "pdfPasswordText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("%1 is protected with a password. Enter it to open the document.").arg(pdfPasswordDialog.file)
            }
            TextField {
                id: pdfPasswordField
                objectName: "pdfPasswordField"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
                onAccepted: pdfPasswordDialog.accept()
            }
            Label {
                objectName: "pdfPasswordWrong"
                Layout.fillWidth: true
                visible: pdfPasswordDialog.wrong
                wrapMode: Text.Wrap
                color: "#b3261e"
                text: qsTr("The password is not right. Try again.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "pdfPasswordOpen"
                text: qsTr("Open")
                enabled: pdfPasswordField.text !== ""
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                objectName: "pdfPasswordCancel"
                text: qsTr("Cancel")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            const password = pdfPasswordField.text
            pdfPasswordField.text = ""  // (not kept in the window)
            app.openWithPassword(password)
        }
        onRejected: {
            pdfPasswordField.text = ""
            app.cancelPassword()
        }
    }
    Connections {
        target: app
        function onPasswordNeeded(file, wrong) {
            pdfPasswordDialog.file = file
            pdfPasswordDialog.wrong = wrong
            pdfPasswordDialog.open()
        }
    }
    // ⋮ → Document → "Protect with a password…" / "Change or remove the password…": AES-256 for its PDF, optional
    // restrictions with a second password
    AdaptiveDialog {
        id: protectDialog
        objectName: "protectDialog"
        kind: "question"
        /// The document is protected already: change or remove its password
        property bool changing: false
        readonly property string problem: {
            if (protectPassword.text !== protectConfirm.text) return qsTr("The two passwords differ.")
            return app.checkProtection(protectPassword.text, protectOwnerPassword.text, !restrictBox.checked || printBox.checked,
                                       !restrictBox.checked || copyBox.checked, !restrictBox.checked || editBox.checked)
        }
        function openFor(change) {
            changing = change
            protectPassword.text = ""
            protectConfirm.text = ""
            protectOwnerPassword.text = ""
            restrictBox.checked = false
            printBox.checked = true
            copyBox.checked = true
            editBox.checked = true
            open()
            protectPassword.forceActiveFocus()
        }
        function clearFields() {
            protectPassword.text = ""
            protectConfirm.text = ""
            protectOwnerPassword.text = ""
        }
        preferredWidth: 480
        title: changing ? qsTr("Change or remove the password") : qsTr("Protect with a password")
        ColumnLayout {
            width: protectDialog.availableWidth
            spacing: 6
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Anyone who opens %1 needs this password, in any PDF app. If it is forgotten, nobody can "
                           + "open the document again, not even this app.").arg(app.title)
            }
            Label {
                objectName: "protectVersionsNote"
                Layout.fillWidth: true
                visible: app.versions.on
                wrapMode: Text.Wrap
                color: "#b06000"
                text: qsTr("The file is written anew: the earlier versions it keeps are removed.")
            }
            TextField {
                id: protectPassword
                objectName: "protectPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: protectDialog.changing ? qsTr("New password") : qsTr("Password")
            }
            TextField {
                id: protectConfirm
                objectName: "protectConfirm"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("The same password again")
                onAccepted: if (protectDialog.problem === "") protectDialog.accept()
            }
            CheckBox {
                id: restrictBox
                objectName: "protectRestrict"
                Layout.fillWidth: true
                text: qsTr("Restrict what others can do with it")
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                visible: restrictBox.checked
                spacing: 2
                CheckBox { id: printBox; objectName: "protectAllowPrint"; text: qsTr("Allow printing"); checked: true }
                CheckBox { id: copyBox; objectName: "protectAllowCopy"; text: qsTr("Allow copying text"); checked: true }
                CheckBox { id: editBox; objectName: "protectAllowEdit"; text: qsTr("Allow changes (notes, forms, pages)"); checked: true }
                TextField {
                    id: protectOwnerPassword
                    objectName: "protectOwnerPassword"
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: qsTr("A second password, to lift the restrictions")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#6b6f75"
                    text: qsTr("PDF apps that respect restrictions (Acrobat, Preview) apply them; others may not.")
                }
            }
            Label {
                objectName: "protectProblem"
                Layout.fillWidth: true
                visible: protectPassword.text !== "" && protectDialog.problem !== ""
                wrapMode: Text.Wrap
                color: "#b3261e"
                text: protectDialog.problem
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "protectRemove"
                visible: protectDialog.changing
                text: qsTr("Remove the password")
                flat: true
                onClicked: {
                    protectDialog.clearFields()
                    protectDialog.close()
                    app.removeProtection()
                }
            }
            Button {
                objectName: "protectAccept"
                text: protectDialog.changing ? qsTr("Change") : qsTr("Protect")
                enabled: protectPassword.text !== "" && protectDialog.problem === ""
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            const password = protectPassword.text
            const owner = protectOwnerPassword.text
            const restricted = restrictBox.checked
            clearFields()
            app.protectDocument(password, owner, !restricted || printBox.checked, !restricted || copyBox.checked,
                                !restricted || editBox.checked)
        }
        onRejected: clearFields()
    }
}
