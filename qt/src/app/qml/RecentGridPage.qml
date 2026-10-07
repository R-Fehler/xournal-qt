// xournal-qt: the home screen's page of the documents opened lately (DocumentGrid), and its empty state.
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

Item {
    // (what the other parts use)
    readonly property alias recentGrid: recentGrid
    DocumentGrid {
        id: recentGrid
        objectName: "recentGrid"
        recent: true
    }
    ColumnLayout {
        anchors.centerIn: parent
        visible: recentGrid.count === 0
        spacing: 10
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: app.iconUrl("xqt-history")
            sourceSize.width: 56; sourceSize.height: 56
            opacity: 0.5
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 16
            color: "#5f6368"
            text: qsTr("Documents you open appear here")
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button { text: qsTr("New document"); highlighted: true; onClicked: newDocumentDialog.open() }
            Button { text: qsTr("Open…"); flat: true; onClicked: home.openFileRequested() }
        }
    }
}
