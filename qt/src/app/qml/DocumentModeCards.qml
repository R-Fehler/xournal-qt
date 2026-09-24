// The two ways to keep documents (session/DocumentMode.h): PDF files or Xournal++ files, as two cards with what each
// means and whom it suits. Used by the first-start question and by Settings → Documents.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: cards
    /// The card chosen: "pdf" or "xopp"
    property string mode: "pdf"
    /// A tapped card becomes `mode` (else `mode` is bound by the user of the cards: it follows the setting)
    property bool selfSelect: false
    /// A card was tapped
    signal picked(string mode)
    spacing: 10

    component Card: AbstractButton {
        id: card
        property string value
        property string subtitle
        property string detail
        property string badge
        readonly property bool chosen: cards.mode === value
        Layout.fillWidth: true
        checkable: false
        focusPolicy: Qt.StrongFocus
        onClicked: {
            if (cards.selfSelect)
                cards.mode = value
            cards.picked(value)
        }
        Accessible.role: Accessible.RadioButton
        Accessible.checked: chosen
        Accessible.name: text
        padding: 14
        background: Rectangle {
            radius: 10
            color: card.chosen ? Qt.rgba(Material.accentColor.r, Material.accentColor.g, Material.accentColor.b, 0.08)
                               : (card.down ? "#f0f0f0" : "white")
            border.width: card.chosen ? 2 : 1
            border.color: card.chosen ? Material.accentColor : (card.visualFocus ? "#9aa0a6" : "#d6d8db")
        }
        contentItem: RowLayout {
            spacing: 12
            // The radio mark: which card is chosen
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                width: 20; height: 20; radius: 10
                border.width: 2
                border.color: card.chosen ? Material.accentColor : "#8a8f95"
                color: "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: 10; height: 10; radius: 5
                    color: Material.accentColor
                    visible: card.chosen
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
                    text: card.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Label {
                    text: card.subtitle
                    color: "#6b6f75"
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Label {
                    text: card.detail
                    Layout.topMargin: 4
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                // The recommendation
                Label {
                    visible: card.badge !== ""
                    text: card.badge
                    Layout.topMargin: 6
                    color: Material.accentColor
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Card {
        objectName: "documentModePdfCard"
        value: "pdf"
        text: qsTr("PDF files")
        subtitle: qsTr("Like Drawboard PDF, GoodNotes, Xodo")
        detail: qsTr("Every document is one PDF that any app opens. Notes on a PDF are saved into that PDF, and "
                     + "nothing else is written next to your files.")
        badge: qsTr("Recommended for most people")
    }
    Card {
        objectName: "documentModeXoppCard"
        value: "xopp"
        text: qsTr("Xournal++ files")
        subtitle: qsTr("Like Xournal++")
        detail: qsTr("Notes are .xopp files next to their PDFs, fully compatible with Xournal++.")
        badge: qsTr("Recommended if you also use Xournal++")
    }
}
