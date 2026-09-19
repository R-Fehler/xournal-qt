// Text mode: type the text of the current page like in a word processor. The page shows it as you type, laid out
// from its top-left margin (Xournal++ text boxes, one per heading / paragraph / list item). Formatting is per
// paragraph: the kind (text, heading 1-3, bullets, numbers), bold, italic, size, color. Markdown shortcuts at the
// start of a line: "# ", "## ", "### ", "- ", "1. ". Done keeps it (one undo step), Cancel restores the page.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import XournalQt.Canvas

Pane {
    id: panel
    objectName: "textFlowPanel"
    visible: false
    padding: 0
    background: Rectangle {
        color: "#fafafa"
        Rectangle { width: 1; anchors.top: parent.top; anchors.bottom: parent.bottom; color: "#d5d8dc" }  // towards the pages
    }

    property int zoomBefore: 100
    function open() {
        editor.family = app.textFlowFamily()
        editor.bodySize = app.fontSize
        const blocks = app.beginTextFlow()
        editor.load(blocks)
        zoomBefore = app.zoomPercent
        visible = true
        Qt.callLater(app.fitWidth)  // the whole page beside the panel
        area.cursorPosition = area.length
        area.forceActiveFocus()
    }
    function close(keep) {
        pageUpdate.stop()
        if (keep) app.updateTextFlow(editor.blocks())
        app.endTextFlow(keep)
        visible = false
        Qt.callLater(app.setZoomPercent, zoomBefore)
    }
    // The page follows shortly after typing
    Timer {
        id: pageUpdate
        interval: 150
        onTriggered: app.updateTextFlow(editor.blocks())
    }
    Connections {
        target: app
        // Another tab or the home screen: the text mode ended there
        function onTextFlowChanged() { if (!app.textFlowActive && panel.visible) { pageUpdate.stop(); panel.visible = false } }
    }

    TextFlowEditor {
        id: editor
        document: area.textDocument
        cursorPosition: area.cursorPosition
        selectionStart: area.selectionStart
        selectionEnd: area.selectionEnd
        onContentChanged: pageUpdate.restart()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title and Done
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 6
            Layout.topMargin: 4
            Label {
                Layout.fillWidth: true
                text: qsTr("Text on page %1").arg(app.textFlowPage + 1)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Button { objectName: "textFlowCancel"; text: qsTr("Cancel"); flat: true; onClicked: panel.close(false) }
            Button { objectName: "textFlowDone"; text: qsTr("Done"); highlighted: true; onClicked: panel.close(true) }
        }

        // Formatting of the paragraph(s) at the cursor
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 2
            component KindButton: AbstractButton {
                id: kb
                property int kind
                property string label
                implicitWidth: Math.max(40, kbLabel.implicitWidth + 16)
                implicitHeight: 40
                onClicked: { editor.setBlockKind(kind); area.forceActiveFocus() }
                background: Rectangle { radius: 8; color: editor.blockKind === kb.kind ? "#e0e3f5" : (kb.hovered ? "#eceef1" : "transparent") }
                contentItem: Label {
                    id: kbLabel
                    text: kb.label
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.weight: kb.kind >= 1 && kb.kind <= 3 ? Font.Bold : Font.Normal
                    color: editor.blockKind === kb.kind ? Material.accentColor : "#303030"
                }
            }
            KindButton { objectName: "kindParagraph"; kind: 0; label: qsTr("Text") }
            KindButton { objectName: "kindHeading1"; kind: 1; label: "H1" }
            KindButton { objectName: "kindHeading2"; kind: 2; label: "H2" }
            KindButton { objectName: "kindHeading3"; kind: 3; label: "H3" }
            IconButton {
                objectName: "kindBullet"; iconName: "xqt-list"; tip: qsTr("Bullet list (- at the start)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                checked: editor.blockKind === 4
                onClicked: { editor.setBlockKind(checked ? 0 : 4); area.forceActiveFocus() }
            }
            IconButton {
                objectName: "kindNumbered"; iconName: "xqt-list-ordered"; tip: qsTr("Numbered list (1. at the start)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                checked: editor.blockKind === 5
                onClicked: { editor.setBlockKind(checked ? 0 : 5); area.forceActiveFocus() }
            }
            IconButton {
                iconName: "xqt-outdent"; tip: qsTr("List level up (Shift+Tab)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                enabled: editor.blockKind >= 4
                onClicked: { editor.indent(-1); area.forceActiveFocus() }
            }
            IconButton {
                iconName: "xqt-indent"; tip: qsTr("List level down (Tab)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                enabled: editor.blockKind >= 4
                onClicked: { editor.indent(1); area.forceActiveFocus() }
            }
            ToolSeparator {}
            IconButton {
                objectName: "boldButton"; iconName: "xqt-bold"; tip: qsTr("Bold paragraph (Ctrl+B)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                checked: editor.bold
                enabled: editor.blockKind < 1 || editor.blockKind > 3
                onClicked: { editor.toggleBold(); area.forceActiveFocus() }
            }
            IconButton {
                objectName: "italicButton"; iconName: "xqt-italic"; tip: qsTr("Italic paragraph (Ctrl+I)")
                implicitWidth: 40; implicitHeight: 40; icon.width: 20; icon.height: 20
                checked: editor.italic
                onClicked: { editor.toggleItalic(); area.forceActiveFocus() }
            }
            ToolButton { text: "A−"; implicitWidth: 40; enabled: editor.blockKind < 1 || editor.blockKind > 3; onClicked: { editor.changeSize(-1); area.forceActiveFocus() } }
            Label { text: Math.round(editor.fontSize); anchors.verticalCenter: undefined; height: 40; verticalAlignment: Text.AlignVCenter; color: "#5f6368" }
            ToolButton { text: "A+"; implicitWidth: 40; enabled: editor.blockKind < 1 || editor.blockKind > 3; onClicked: { editor.changeSize(1); area.forceActiveFocus() } }
            ToolSeparator {}
            // Text color: the pen colors of the tool bar
            Repeater {
                model: app.toolbarColors
                delegate: AbstractButton {
                    id: swatch
                    required property color modelData
                    objectName: "textColor"
                    implicitWidth: 34
                    implicitHeight: 40
                    onClicked: { editor.setColor(modelData); area.forceActiveFocus() }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Text color of the paragraph")
                    ToolTip.delay: 600
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 24; height: 24; radius: 12
                            color: swatch.modelData
                            border.width: Qt.colorEqual(editor.color, swatch.modelData) ? 3 : 1
                            border.color: Qt.colorEqual(editor.color, swatch.modelData) ? Material.accentColor : "#9e9e9e"
                        }
                    }
                }
            }
        }

        // The text does not fit on the page
        Label {
            objectName: "textFlowOverflow"
            visible: app.textFlowOverflow > 0
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            wrapMode: Text.Wrap
            color: "#b3261e"
            font.pixelSize: 12
            text: qsTr("The text is longer than the page (about %1 lines below the margin). Insert a page and go on there.")
                    .arg(Math.max(1, Math.round(app.textFlowOverflow / (app.fontSize * 1.25))))
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            background: Rectangle { color: "#ffffff"; border.width: 1; border.color: "#d5d8dc"; radius: 4 }
            TextArea {
                id: area
                objectName: "textFlowArea"
                textFormat: TextEdit.RichText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                background: null
                padding: 14
                placeholderText: qsTr("Type here. # Heading, - list, 1. numbered list")
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (!(event.modifiers & Qt.ShiftModifier)) event.accepted = editor.returnPressed()
                    } else if (event.key === Qt.Key_Tab && editor.blockKind >= 4) {
                        editor.indent(1); event.accepted = true
                    } else if (event.key === Qt.Key_Backtab && editor.blockKind >= 4) {
                        editor.indent(-1); event.accepted = true
                    } else if (event.key === Qt.Key_B && (event.modifiers & Qt.ControlModifier)) {
                        editor.toggleBold(); event.accepted = true
                    } else if (event.key === Qt.Key_I && (event.modifiers & Qt.ControlModifier)) {
                        editor.toggleItalic(); event.accepted = true
                    } else if (event.key === Qt.Key_1 && (event.modifiers & Qt.ControlModifier)) {
                        editor.setBlockKind(1); event.accepted = true
                    } else if (event.key === Qt.Key_2 && (event.modifiers & Qt.ControlModifier)) {
                        editor.setBlockKind(2); event.accepted = true
                    } else if (event.key === Qt.Key_3 && (event.modifiers & Qt.ControlModifier)) {
                        editor.setBlockKind(3); event.accepted = true
                    } else if (event.key === Qt.Key_0 && (event.modifiers & Qt.ControlModifier)) {
                        editor.setBlockKind(0); event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        panel.close(true); event.accepted = true
                    }
                }
            }
        }
    }
}
