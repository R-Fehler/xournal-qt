// A menu that fits the window (qt/docs/adaptive-layout.md, "Menus").
// - Desktop and tablet classes: a Menu as wide as its longest entry (within the window), never taller than the window,
//   and never over the button it came from: from a button in the upper half it opens below it, from one in the lower
//   half above it; what does not fit scrolls, with a scroll bar that shows it.
// - Phone classes (phone portrait, phone short, tiny): the same entries in a bottom sheet (MenuSheet, one per window);
//   a submenu drills in there, with a back arrow.
// Open it with openMenu() (Popups.openAt does), not popup(): only that knows the sheet. Entries whose presence depends
// on something are AdaptiveMenuItems with `offered` (not `visible`); a submenu is an AdaptiveMenu with a `title`, and
// `offered` hides its entry.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Menu {
    id: control
    /// As a submenu: whether its entry is offered at all
    property bool offered: true
    property real minimumWidth: 200
    property real maximumWidth: 420
    /// The window's layout (Main.qml's `win.adaptive`), if there is one
    readonly property var adaptiveLayout: typeof win !== "undefined" && win ? win.adaptive : null
    /// In a phone class (by the layout class: "Adapt the layout" off keeps the desktop menus) it opens as a sheet
    readonly property bool asSheet: adaptiveLayout !== null && ["phonePortrait", "phoneShort", "tiny"].indexOf(adaptiveLayout.layoutClass) >= 0
    /// The widest entry offered (a binding: entries come and go, and change their text, with the document)
    readonly property real entryWidth: {
        let w = 0
        for (let i = 0; i < count; ++i) {
            const it = itemAt(i)
            if (it && it.offered !== false) w = Math.max(w, it.implicitWidth)
        }
        return w
    }
    readonly property real windowWidth: typeof win !== "undefined" && win ? win.width : 100000
    readonly property real windowHeight: typeof win !== "undefined" && win ? win.height : 100000

    implicitWidth: Math.min(windowWidth - 16, Math.max(minimumWidth, Math.min(maximumWidth, entryWidth + leftPadding + rightPadding)))
    margins: 8
    verticalPadding: 6

    // Submenus get an entry that can be left out (`offered`) and has the same height as the others
    delegate: AdaptiveMenuItem { offered: subMenu ? subMenu.offered !== false : true }

    contentItem: ListView {
        id: list
        implicitHeight: contentHeight
        implicitWidth: control.entryWidth
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        // Scrolls only where the window is too short for it: then the bar shows that there is more
        ScrollBar.vertical: ScrollBar {
            objectName: "menuScrollBar"
            policy: list.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
    }

    /// Opens the menu: as a sheet in the phone classes, else at `pos` in `anchor` (default: the item it belongs to), or
    /// below the anchor. A small anchor (a button, a tab) stays visible: the menu keeps to the side of the window's
    /// middle it is on.
    function openMenu(pos, anchor) {
        if (!anchor) anchor = control.parent
        if (asSheet && typeof menuSheet !== "undefined" && menuSheet) {
            menuSheet.show(control)
            return
        }
        const safeTop = typeof win !== "undefined" && win && win.safeTop ? win.safeTop : 0
        const safeBottom = typeof win !== "undefined" && win && win.safeBottom ? win.safeBottom : 0
        let top = 8 + safeTop, bottom = 8 + safeBottom
        if (anchor && anchor.height <= 96) {
            const r = anchor.mapToItem(null, 0, 0, anchor.width, anchor.height)
            if (r.y + r.height / 2 < windowHeight / 2) top = Math.max(top, r.y + r.height)
            else bottom = Math.max(bottom, windowHeight - r.y)
        }
        topMargin = top
        bottomMargin = bottom
        if (pos !== undefined && pos !== null && anchor) popup(anchor, pos.x, pos.y)
        else if (anchor) popup(anchor, 0, anchor.height)
        else popup()
    }
}
