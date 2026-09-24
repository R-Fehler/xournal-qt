// The fuzzy search's toggle in a search field (library, tab overview): the search reads fzf's syntax - names fuzzy
// and ranked, words ANDed, | for or, ! for not, parentheses. An app-wide setting (app.library.fuzzySearch), off by
// default; the tooltip is its help.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ToolButton {
    id: toggle
    text: qsTr("Fuzzy")
    checkable: true
    checked: app.library.fuzzySearch
    onToggled: app.library.fuzzySearch = checked
    implicitHeight: 36
    leftPadding: 6
    rightPadding: 6
    font.pixelSize: 13
    font.weight: checked ? Font.DemiBold : Font.Normal
    Material.foreground: checked ? Material.accentColor : "#5f6368"
    ToolTip.visible: hovered || pressed
    ToolTip.delay: 600
    ToolTip.timeout: 20000
    ToolTip.text: (checked ? qsTr("Fuzzy search is on (like fzf) - tap for the plain search.")
                           : qsTr("Fuzzy search (like fzf): tap to turn it on."))
                  + "<br>" + qsTr("Names match when their letters come in this order, best matches first; the text "
                                  + "of the documents is searched for each word.")
                  + "<table cellspacing=\"4\">"
                  + "<tr><td><tt>a b</tt></td><td>" + qsTr("both") + "</td></tr>"
                  + "<tr><td><tt>a | b</tt></td><td>" + qsTr("either (<tt>a b | c</tt>: a and (b or c))") + "</td></tr>"
                  + "<tr><td><tt>!a</tt></td><td>" + qsTr("without a (not in the name, the folder or the text)") + "</td></tr>"
                  + "<tr><td><tt>( … )</tt></td><td>" + qsTr("a group: <tt>(a b) | c</tt>, <tt>!(a b)</tt>") + "</td></tr>"
                  + "<tr><td><tt>'a</tt></td><td>" + qsTr("exactly a, also in names") + "</td></tr>"
                  + "<tr><td><tt>'a'</tt></td><td>" + qsTr("the whole word a") + "</td></tr>"
                  + "<tr><td><tt>^a</tt> &nbsp; <tt>a$</tt></td><td>" + qsTr("the name (or a word) starts / ends with a") + "</td></tr>"
                  + "<tr><td><tt>^a$</tt></td><td>" + qsTr("the name (or a word) is a") + "</td></tr>"
                  + "<tr><td><tt>\\&nbsp;</tt></td><td>" + qsTr("a space within a word") + "</td></tr>"
                  + "</table>"
    background: Rectangle {
        radius: 10
        color: toggle.checked ? "#e0e3f5" : (toggle.pressed ? "#e8e8e8" : "transparent")
    }
}
