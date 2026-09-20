// Adds pages at the end of the document: − N + and a button. The pages look like the current one (a PDF or image
// background is not copied: those pages get the background chosen for new pages). One undo step.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    objectName: "appendPages"
    implicitHeight: row.implicitHeight + 24
    property int count: 1
    /// In the narrow page sidebar the button goes below the number
    readonly property bool narrow: width < 260

    function append() {
        const format = app.currentPageFormat()
        const background = format && format.background >= 0 ? format.background
                                                            : Math.max(0, app.settings.get("pageBackground"))
        app.insertPages(app.pageCount, background, -1, false, root.count)
    }

    GridLayout {
        id: row
        anchors.centerIn: parent
        width: Math.min(parent.width - 16, implicitWidth)
        columns: root.narrow ? 3 : 4
        columnSpacing: 2
        rowSpacing: 6
        ToolButton {
            objectName: "appendLess"
            text: "−"
            font.pixelSize: 20
            implicitWidth: 40; implicitHeight: 40
            enabled: root.count > 1
            autoRepeat: true
            onClicked: root.count = Math.max(1, root.count - 1)
        }
        TextField {
            objectName: "appendCount"
            text: root.count
            horizontalAlignment: Text.AlignHCenter
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 999 }
            Layout.preferredWidth: 48
            onEditingFinished: root.count = Math.min(999, Math.max(1, parseInt(text) || 1))
        }
        ToolButton {
            objectName: "appendMore"
            text: "+"
            font.pixelSize: 20
            implicitWidth: 40; implicitHeight: 40
            autoRepeat: true
            onClicked: root.count = Math.min(999, root.count + 1)
        }
        Button {
            objectName: "appendButton"
            text: root.count === 1 ? qsTr("Add page") : qsTr("Add %1 pages").arg(root.count)
            Layout.leftMargin: root.narrow ? 0 : 6
            Layout.columnSpan: root.narrow ? 3 : 1
            Layout.fillWidth: root.narrow
            highlighted: true
            onClicked: root.append()
        }
    }
}
