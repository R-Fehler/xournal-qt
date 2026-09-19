import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkpad

ApplicationWindow {
    id: win
    width: 1200
    height: 900
    visible: true
    title: "xournal-qt inkpad spike (quick host)"
    color: "#3c3c40"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 6
            Label {
                Layout.fillWidth: true
                font.family: "monospace"
                font.pointSize: 8.5
                text: "[quick] " + spike.environment + "\n" + spike.hud
                elide: Text.ElideRight
            }
            TextField {
                placeholderText: "tap: on-screen keyboard?"
                Layout.preferredWidth: 200
            }
            Button { text: "Undo"; implicitHeight: 44; implicitWidth: 64; onClicked: spike.undo() }
            Button { text: "Redo"; implicitHeight: 44; implicitWidth: 64; onClicked: spike.redo() }
            Button { text: "Fit"; implicitHeight: 44; implicitWidth: 64; onClicked: spike.fitWidth() }
            Button { text: "Clear"; implicitHeight: 44; implicitWidth: 64; onClicked: spike.clear() }
        }
    }

    QuickCanvas {
        anchors.fill: parent
    }
}
