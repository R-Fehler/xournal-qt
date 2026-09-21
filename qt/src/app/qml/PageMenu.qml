// What can be done with a page, from the sidebar or the page grid: it acts on the selection when the page is
// selected, else on the page itself. The everyday actions are a small grid of icons, every one with its tool tip,
// so the whole thing stays short; only what an icon cannot tell is written out. Inserting somewhere else, or
// several pages at once, is in the "Insert pages…" dialog (it asks where, how many, background and size).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: menu
    objectName: "pageMenu"
    parent: Overlay.overlay
    padding: 6
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property int page: 0
    readonly property var pages: app.pages.isSelected(page) ? app.pages.selectedPages() : [page]
    readonly property string what: pages.length > 1 ? qsTr("%1 pages").arg(pages.length) : qsTr("page")
    readonly property int lastPage: pages[pages.length - 1]

    /// Open it where the page was pressed (`x`, `y` in the coordinates of `item`), and keep it inside the window.
    function openFor(p, item, x, y) {
        page = p
        const at = item.mapToItem(Overlay.overlay, x, y)
        menu.x = Math.max(8, Math.min(at.x, Overlay.overlay.width - menu.width - 8))
        menu.y = Math.max(8, Math.min(at.y, Overlay.overlay.height - menu.height - 8))
        open()
    }

    background: Rectangle {
        radius: 12
        color: "#ffffff"
        border.width: 1
        border.color: "#d5d8dc"
    }

    component PageAction: IconButton {
        implicitWidth: 44
        implicitHeight: 44
        icon.width: 21
        icon.height: 21
    }
    component PageLine: ItemDelegate {
        Layout.fillWidth: true
        implicitHeight: 38
        font.pixelSize: 14
    }

    ColumnLayout {
        spacing: 2
        GridLayout {
            columns: 5
            columnSpacing: 0
            rowSpacing: 0
            Layout.alignment: Qt.AlignHCenter
            PageAction {
                objectName: "pageMenuCopy"
                iconName: "xopp-edit-copy"
                tip: qsTr("Copy %1").arg(menu.what)
                onClicked: { app.copyPages(menu.pages); menu.close() }
            }
            PageAction {
                iconName: "xopp-edit-cut"
                tip: qsTr("Cut %1").arg(menu.what)
                onClicked: { app.cutPages(menu.pages); menu.close() }
            }
            PageAction {
                objectName: "pageMenuPaste"
                iconName: "xopp-edit-paste"
                tip: app.copiedPages > 1 ? qsTr("Paste %1 pages after").arg(app.copiedPages) : qsTr("Paste after")
                enabled: app.copiedPages > 0
                onClicked: { app.pastePages(menu.lastPage + 1); menu.close() }
            }
            PageAction {
                iconName: "xqt-duplicate"
                tip: qsTr("Duplicate %1").arg(menu.what)
                onClicked: { app.duplicatePages(menu.pages); menu.close() }
            }
            PageAction {
                objectName: "pageMenuDelete"
                iconName: "xopp-page-delete"
                tip: qsTr("Delete %1").arg(menu.what)
                enabled: menu.pages.length < app.pages.count
                onClicked: { app.deletePages(menu.pages); menu.close() }
            }
            PageAction {
                iconName: "xopp-page-add"
                tip: qsTr("Insert a page after this one")
                onClicked: { app.insertPageAfter(menu.lastPage); menu.close() }
            }
            PageAction {
                objectName: "pageMenuUp"
                iconName: "xqt-chevron-up"
                tip: qsTr("Move %1 up").arg(menu.what)
                enabled: menu.pages[0] > 0
                onClicked: { app.movePages(menu.pages, menu.pages[0] - 1); menu.close() }
            }
            PageAction {
                iconName: "xqt-chevron-down"
                tip: qsTr("Move %1 down").arg(menu.what)
                enabled: menu.lastPage < app.pages.count - 1
                onClicked: { app.movePages(menu.pages, menu.lastPage + 2); menu.close() }
            }
            PageAction {
                iconName: "xopp-document-print"
                tip: menu.pages.length > 1 ? qsTr("Print %1 pages…").arg(menu.pages.length)
                                           : qsTr("Print this page…")
                onClicked: { app.requestPrint(menu.pages); menu.close() }
            }
            PageAction {
                iconName: "xqt-link"
                tip: qsTr("Copy a link to this page")
                onClicked: { app.copyPageLink(menu.page); menu.close() }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#e2e5e9" }
        PageLine {
            text: menu.pages.length > 1 ? qsTr("Background of %1 pages…").arg(menu.pages.length)
                                        : qsTr("Background of this page…")
            onClicked: { app.requestPageBackground(menu.pages); menu.close() }
        }
        PageLine {
            objectName: "insertPagesItem"
            text: qsTr("Insert pages…")
            onClicked: { app.requestInsertPages(menu.lastPage + 1); menu.close() }
        }
        PageLine {
            text: qsTr("Start a chapter here…")
            onClicked: { app.requestChapter(menu.page); menu.close() }
        }
        PageLine {
            text: qsTr("Select all pages")
            onClicked: { app.pages.selectAll(); menu.close() }
        }
    }
}
