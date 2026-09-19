// Menu of a page in the sidebar or the page grid. It acts on the selection when the page is selected, else on the
// page itself.
import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    property int page: 0
    readonly property var pages: app.pages.isSelected(page) ? app.pages.selectedPages() : [page]
    readonly property string what: pages.length > 1 ? qsTr("%1 pages").arg(pages.length) : qsTr("page")

    function openFor(p, item, x, y) {
        page = p
        popup(item, x, y)
    }

    MenuItem { text: qsTr("Copy %1").arg(menu.what); onTriggered: app.copyPages(menu.pages) }
    MenuItem { text: qsTr("Cut %1").arg(menu.what); onTriggered: app.cutPages(menu.pages) }
    MenuItem {
        text: app.copiedPages > 1 ? qsTr("Paste %1 pages after").arg(app.copiedPages) : qsTr("Paste after")
        enabled: app.copiedPages > 0
        onTriggered: app.pastePages(menu.pages[menu.pages.length - 1] + 1)
    }
    MenuItem { text: qsTr("Duplicate %1").arg(menu.what); onTriggered: app.duplicatePages(menu.pages) }
    MenuSeparator {}
    MenuItem { text: qsTr("Insert page before"); onTriggered: app.insertPageBefore(menu.pages[0]) }
    MenuItem { text: qsTr("Insert page after"); onTriggered: app.insertPageAfter(menu.pages[menu.pages.length - 1]) }
    MenuItem {
        objectName: "insertPagesItem"
        text: qsTr("Insert pages… (background, size)")
        onTriggered: app.requestInsertPages(menu.pages[menu.pages.length - 1] + 1)
    }
    MenuItem {
        text: qsTr("Move up")
        enabled: menu.pages[0] > 0
        onTriggered: app.movePages(menu.pages, menu.pages[0] - 1)
    }
    MenuItem {
        text: qsTr("Move down")
        enabled: menu.pages[menu.pages.length - 1] < app.pages.count - 1
        onTriggered: app.movePages(menu.pages, menu.pages[menu.pages.length - 1] + 2)
    }
    MenuSeparator {}
    MenuItem { text: qsTr("Select all pages"); onTriggered: app.pages.selectAll() }
    MenuItem {
        text: qsTr("Delete %1").arg(menu.what)
        enabled: menu.pages.length < app.pages.count
        onTriggered: app.deletePages(menu.pages)
    }
}
