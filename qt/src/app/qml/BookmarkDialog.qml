// The label of a page's bookmark (qt/docs/bookmarks.md): "" or "Page N" is the automatic one, which follows the page.
// From the page menu and the Bookmarks section of the contents sidebar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: bookmarkDialog
    objectName: "bookmarkDialog"
    property int page: 0
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Bookmark of page %1").arg(page + 1)
    width: Math.min(parent ? parent.width * 0.9 : 400, 400)
    function openFor(p) {
        page = p
        bookmarkField.text = app.bookmarkOf(p)
        open()
    }
    onOpened: { bookmarkField.selectAll(); bookmarkField.forceActiveFocus() }
    onAccepted: app.renameBookmark(page, bookmarkField.text)
    footer: DialogButtonBox {
        Button {
            objectName: "bookmarkRemove"
            text: qsTr("Remove bookmark")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: { app.toggleBookmark(bookmarkDialog.page); bookmarkDialog.close() }
        }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { objectName: "bookmarkOk"; text: qsTr("OK"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
    }
    ColumnLayout {
        width: bookmarkDialog.availableWidth
        TextField {
            id: bookmarkField
            objectName: "bookmarkField"
            Layout.fillWidth: true
            placeholderText: qsTr("Page %1").arg(bookmarkDialog.page + 1)
            onAccepted: bookmarkDialog.accept()
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#80868b"
            text: qsTr("Empty: the page's number, which follows the page when pages move.")
        }
    }
}
