// xournal-qt: part of the main window (Main.qml), a direct child of it there: its z, anchors and parent are the
// window's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A Markdown file shown read-only for now, an image to write on: what that means (closed for this tab with ×).
Pane {
    id: shownFileNote
    objectName: "shownFileNote"
    property string closedFor: ""
    visible: app.shownFileNote !== "" && closedFor !== app.title && !pageGrid.visible && !contentsOverview.visible
             && !win.hudHidden
    // (bottom left: the search bar is at the top, the page and zoom pill at the bottom right; above them where it
    // would meet them in a narrow canvas)
    anchors.left: canvas.left
    anchors.leftMargin: (zenDot.visible ? 56 : 24) + win.canvasControlsLeft - canvas.x  // (in Zen: beside the dot)
    // (through a property of its own: a binding of y that reads the geometry itself crashes Qt 6.7)
    readonly property real clearY: win.clearOfPills(shownFileNote, win.canvasControlsBottom - 24 - height, [viewPill, navPill])
    y: clearY
    width: Math.min(canvas.width - anchors.leftMargin - 16,
                    Math.max(160, Math.min(canvas.width - viewPill.width - 80, 560)))
    padding: 2
    leftPadding: 14
    background: Rectangle {
        radius: 12
        color: "#f2fff8e1"
        border.width: 1
        border.color: "#40000000"
    }
    RowLayout {
        width: parent.width
        spacing: 4
        Label {
            objectName: "shownFileNoteText"
            Layout.fillWidth: true
            text: app.shownFileNote
            wrapMode: Text.Wrap
            color: "#4a3b00"
            font.pixelSize: 13
        }
        Button {
            objectName: "editAnywayButton"
            visible: app.canEditAnyway
            flat: true
            text: qsTr("Edit anyway")
            onClicked: app.editAnyway(false)
        }
        ToolButton {
            objectName: "shownFileNoteClose"
            text: "×"
            font.pixelSize: 18
            implicitWidth: 36
            onClicked: shownFileNote.closedFor = app.title
        }
    }
}
