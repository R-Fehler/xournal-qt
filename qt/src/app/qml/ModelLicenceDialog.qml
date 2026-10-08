// xournal-qt: the licence note of a handwriting model that comes with the app (its LICENCE.md, shown as it is), from
// Settings → Search and Settings → Help → About (qt/docs/features/handwriting-search.md, "The built-in model").
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: licence
    /// The model's folder (app.handwriting.builtInModels: folder) and its name
    property string folder: ""
    property string modelName: ""
    kind: "form"
    preferredWidth: 640
    closeButton: true
    title: qsTr("Licence of the handwriting model %1").arg(modelName)
    Label {
        objectName: licence.objectName + "Text"
        width: licence.availableWidth
        wrapMode: Text.Wrap
        textFormat: Text.MarkdownText
        text: licence.folder !== "" ? app.handwriting.licenceText(licence.folder) : ""
        onLinkActivated: (link) => Qt.openUrlExternally(link)
    }
    footer: DialogButtonBox {
        Button { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    /// Show the note of a model (a map of app.handwriting.builtInModels, or name and folder)
    function show(name, modelFolder) {
        modelName = name
        folder = modelFolder
        open()
    }
}
