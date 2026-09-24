// The fuzzy search's help: what fuzzy means for names and for text (with the typo tolerance as it is set), the syntax
// with an example per row, and how the pages with hits are chosen. Opened by a long press or a right click on the
// "Fuzzy" button of a search field (FuzzyToggle) and from Settings → Search. The one place of this text: the
// "Fuzzy search" section of qt/docs/library.md mirrors it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: sheet
    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((parent ? parent.width : 800) / 2 - width / 2)
    y: Math.round((parent ? parent.height : 600) / 2 - height / 2)
    width: Math.min(parent ? parent.width - 32 : 720, 720)
    height: Math.min(parent ? parent.height - 48 : 700, 760)
    padding: 0
    background: Rectangle { color: "#ffffff"; radius: 12; border.width: 1; border.color: "#d5d8dc" }

    /// The typo tolerance as it is set (Settings → Search)
    readonly property int typos: (app.settings.revision, app.settings.get("fuzzyTypos"))
    readonly property string typoText: typos === 0
            ? qsTr("Typos are not tolerated (Settings → Search).")
            : typos === 1
              ? qsTr("A typo is tolerated in words of 5 or more letters: one letter swapped, left out, added or "
                     + "wrong (turbnie finds \"turbine\"; Settings → Search).")
              : qsTr("Typos are tolerated: one in words of 5 to 7 letters, two in words of 8 or more (one letter "
                     + "swapped, left out, added or wrong each; Settings → Search).")

    /// The syntax: what is typed, what it finds, an example
    readonly property var rows: [
        { typed: "tbine", finds: qsTr("names with these letters in this order, best first; in the text, words with "
                                      + "the letters close together from their first letter on, or with a typo"),
          example: qsTr("<tt>tbine</tt> finds \"turbine\", <tt>klmn</tt> \"Kalman\"") },
        { typed: "a b", finds: qsTr("both (in any order, anywhere in the document)"),
          example: qsTr("<tt>kalman filter</tt>") },
        { typed: "a | b", finds: qsTr("either; <tt>a b | c</tt> is a and (b or c)"),
          example: qsTr("<tt>lecture | exercise</tt>") },
        { typed: "!a", finds: qsTr("without it: not in the name, the folder or the text"),
          example: qsTr("<tt>kalman !draft</tt>") },
        { typed: "( … )", finds: qsTr("a group, also negated: <tt>!(a b)</tt>"),
          example: qsTr("<tt>(lecture | exercise) !draft</tt>") },
        { typed: "'a", finds: qsTr("exactly these letters, also in names (no fuzzy words)"),
          example: qsTr("<tt>'turbine</tt>: not \"turbnie\"") },
        { typed: "'a'", finds: qsTr("the whole word"),
          example: qsTr("<tt>'wind'</tt>: not \"window\"") },
        { typed: "^a", finds: qsTr("the name starts with it; in the text, a word"),
          example: qsTr("<tt>^intro</tt>: \"Introduction\"") },
        { typed: "a$", finds: qsTr("the name ends with it; in the text, a word"),
          example: qsTr("<tt>sheet$</tt>: \"Exercise sheet\"") },
        { typed: "^a$", finds: qsTr("the name is it; in the text, the whole word"),
          example: qsTr("<tt>^lecture\\ 3$</tt>") },
        { typed: "\\  \\(  \\)", finds: qsTr("a space, a parenthesis within a term"),
          example: qsTr("<tt>kalman\\ filter</tt>: the phrase") }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Label { text: qsTr("Fuzzy search"); font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "xqt-close"; tip: qsTr("Close (Esc)"); onClicked: sheet.close() }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 16
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: content
                width: parent.width - 8
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    text: qsTr("<b>Names</b> (and folders) match when their letters come in the typed order, "
                               + "like fzf; the best matches come first, their matched letters are marked.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    text: qsTr("<b>Text</b> is matched word by word: a word matches when it contains the typed "
                               + "letters, or starts with the first one and has all of them in this order with few "
                               + "letters between (tbine: \"turbine\", not \"tambourine\"). The whole word is marked. "
                               + "Words of 1-2 letters and terms with other characters are found as typed. "
                               + "Documents with the word as typed come before fuzzy matches.")
                        + " " + sheet.typoText
                }
                // The syntax: per row what is typed, and below what it finds, an example
                Repeater {
                    objectName: "fuzzyHelpRows"
                    model: sheet.rows
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 14
                        Label {
                            Layout.preferredWidth: 84
                            Layout.alignment: Qt.AlignTop
                            text: modelData.typed
                            font.family: "monospace"
                            font.weight: Font.DemiBold
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                textFormat: Text.StyledText
                                text: modelData.finds
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                textFormat: Text.StyledText
                                color: "#6b6f75"
                                font.pixelSize: 13
                                text: qsTr("e.g. %1").arg(modelData.example)
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    text: qsTr("<b>Pages with hits</b> (the pages button): the pages on which the whole expression "
                               + "holds, a term counting as found on a page when the page, the name or the folder "
                               + "has it; if it holds on no single page (the words are on different pages), all "
                               + "pages with hits. The count on a card is the hits of the terms that are not negated.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#6b6f75"
                    text: qsTr("Case never matters. An expression that is not complete (a ( not closed, a | "
                               + "alone) is searched as plain text, with a short hint next to the field.")
                }
            }
        }
    }
}
