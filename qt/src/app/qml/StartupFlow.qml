// xournal-qt: the first start and after a crash (qt/docs/features/onboarding.md; Main.qml's part): the introduction,
// the way to keep documents, the recovery, the tutorial started again. The window's Component.onCompleted calls
// start().
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: startupFlow
    anchors.fill: parent
    visible: false
    readonly property alias introDialog: introDialog
    readonly property alias restartTutorialDialog: restartTutorialDialog
    // After a crash: offer the documents with unsaved changes (from emergency and autosave files).
    AdaptiveDialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        kind: "question"
        onClosed: homeView.offerLibrariesHomeAtStart()
        closePolicy: Popup.NoAutoClose
        preferredWidth: 560
        title: qsTr("Recover unsaved changes?")
        ColumnLayout {
            width: recoveryDialog.availableWidth
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Xournal Qt did not close properly. These documents have changes that were not saved:")
            }
            Repeater {
                model: app.recoveryItems
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.title; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Label { text: modelData.time; color: "#6b6f75" }
                }
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Recover"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Discard changes"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole }
            onAccepted: { recoveryDialog.close(); app.recover(true) }
            onClicked: function(button) {
                if (button.DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole) {
                    recoveryDialog.close()
                    app.recover(false)
                }
            }
        }
    }
    // The first start shows the introduction (qt/docs/features/onboarding.md), which ends in the question which way to
    // keep documents (PDF files or Xournal++ files); the question alone when the introduction was shown already but no
    // way was chosen. Then the recovery question; then (Android) where the libraries are kept.
    function afterFirstStart() {
        if (app.recoveryItems.length > 0) recoveryDialog.open()
        else homeView.offerLibrariesHomeAtStart()
    }
    DocumentModeDialog {
        id: documentModeDialog
        onChosen: startupFlow.afterFirstStart()
    }
    IntroDialog {
        id: introDialog
        onChosen: startupFlow.afterFirstStart()
    }
    // Help → Start the tutorial again: a fresh copy replaces the one written on (qt/docs/features/onboarding.md)
    AdaptiveDialog {
        id: restartTutorialDialog
        objectName: "restartTutorialDialog"
        kind: "question"
        preferredWidth: 480
        title: qsTr("Start the tutorial again?")
        Label {
            width: restartTutorialDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("A fresh copy of the tutorial replaces yours; what you wrote on it is gone. To keep it, save it "
                       + "somewhere else first (⋮ → Save as…).")
        }
        footer: DialogButtonBox {
            Button {
                objectName: "restartTutorialConfirm"
                text: qsTr("Start again")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: app.restartTutorial()
    }
    function start() {
        if (app.askIntro()) introDialog.openFirstStart()
        else if (app.askDocumentMode()) documentModeDialog.open()
        else if (app.recoveryItems.length > 0) recoveryDialog.open()
        else homeView.offerLibrariesHomeAtStart()
    }
}
