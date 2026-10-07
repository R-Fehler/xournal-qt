// xournal-qt: which kinds of files the library shows (a setting of each library): the Show button's menu and
// View → Show of the home screen (HomeHeader.qml).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

AdaptiveMenu {
    id: showMenu
    /// The start of the objectNames of its switches ("show": showNotes, showPdfs, …, showDefaults)
    property string prefix: "show"
    /// Its title on top (the Show button's menu; as a submenu and in the sheet the title is the entry)
    property bool heading: false
    title: qsTr("Kinds of files shown")
    ColumnLayout {
        width: parent ? parent.width : implicitWidth
        spacing: 0
        Label {
            visible: showMenu.heading
            text: qsTr("Show in this library")
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: "#5f6368"
            Layout.leftMargin: 12
            Layout.topMargin: 4
            Layout.bottomMargin: 4
        }
        ShowToggle { objectName: showMenu.prefix + "Notes"; key: "notes"; text: qsTr("Notes (.xopp, .xoj)") }
        ShowToggle { objectName: showMenu.prefix + "Pdfs"; key: "pdfs"; text: qsTr("PDFs") }
        ShowToggle {
            objectName: showMenu.prefix + "OnlyPdfsWithNotes"
            key: "onlyPdfsWithNotes"
            text: qsTr("Only PDFs with notes")
            enabled: app.library.show.pdfs === true
            leftPadding: 40
            font.pixelSize: 13
        }
        ShowToggle {
            objectName: showMenu.prefix + "OnlyTextDocuments"
            key: "onlyTextDocuments"
            text: qsTr("Only PDF text documents")
            enabled: app.library.show.pdfs === true
            leftPadding: 40
            font.pixelSize: 13
        }
        ShowToggle { objectName: showMenu.prefix + "Markdown"; key: "markdown"; text: qsTr("Markdown (.md)") }
        ShowToggle { objectName: showMenu.prefix + "Images"; key: "images"; text: qsTr("Images") }
        ShowToggle { objectName: showMenu.prefix + "Text"; key: "text"; text: qsTr("Text and code (.txt, .tex, .py, …)") }
        ShowToggle { objectName: showMenu.prefix + "Other"; key: "other"; text: qsTr("All other files") }
        Button {
            objectName: showMenu.prefix + "Defaults"
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: 8
            flat: true
            text: qsTr("Defaults")
            enabled: app.library.showFiltered
            onClicked: app.library.resetShown()
        }
    }

    // A kind of file shown or not (the library's "Show" setting)
    component ShowToggle: CheckDelegate {
        property string key
        Layout.fillWidth: true
        checked: app.library.show[key] === true
        onToggled: app.library.setShown(key, checked)
        font.pixelSize: 14
        topPadding: 6
        bottomPadding: 6
        implicitHeight: Math.max(home.minTarget, Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding)
    }
}
