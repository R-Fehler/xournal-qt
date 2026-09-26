// A name edited in place (qt/rename): a tab's title, a card's title in the library or the overview of open documents.
// The name is selected, the extension stays beside it as fixed text. Enter renames (a name that cannot be used says
// why below the field and stays); Escape or clicking elsewhere cancels.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

RowLayout {
    id: inline
    /// The extension that stays (".xopp", ".md"; "" for none)
    property string extension
    /// function(name) -> why it cannot be used ("": it can)
    property var check: null
    /// Why the name cannot be used (from `check`, or given, e.g. a read-only file)
    property string problem: ""
    property alias field: field
    property real fontSize: 14
    readonly property bool editing: visible && field.activeFocus
    signal accepted(string name)
    signal canceled()
    spacing: 2

    property bool active: false
    function start(name, problemText) {
        field.text = name
        problem = problemText || ""
        active = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function commit() {
        if (!active) return
        problem = check ? check(field.text) : ""
        if (problem !== "") {
            field.forceActiveFocus()
            return
        }
        active = false
        accepted(field.text.trim())
    }
    function cancel() {
        if (!active) return
        active = false
        problem = ""
        canceled()
    }

    TextField {
        id: field
        objectName: "inlineRenameField"
        Layout.fillWidth: true
        Layout.minimumWidth: 60
        selectByMouse: true
        font.pixelSize: inline.fontSize
        topPadding: 4
        bottomPadding: 4
        leftPadding: 6
        rightPadding: 6
        background: Rectangle {
            radius: 4
            color: "#ffffff"
            border.width: 2
            border.color: inline.problem !== "" ? "#b3261e" : Material.accentColor
        }
        onTextEdited: inline.problem = inline.check ? inline.check(text) : ""
        // (Enter and Escape are the field's, not the window's shortcuts)
        Keys.onShortcutOverride: function(event) {
            event.accepted = event.key === Qt.Key_Escape || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        }
        Keys.onReturnPressed: inline.commit()
        Keys.onEnterPressed: inline.commit()
        Keys.onEscapePressed: inline.cancel()
        // Clicking elsewhere cancels
        onActiveFocusChanged: if (!activeFocus) inline.cancel()
        ToolTip.visible: inline.active && inline.problem !== ""
        ToolTip.text: inline.problem
    }
    Label {
        objectName: "inlineRenameExtension"
        visible: inline.extension !== ""
        text: inline.extension
        font.pixelSize: inline.fontSize
        color: "#80868b"
    }
}
