// A menu that fits the window (qt/docs/features/adaptive-layout.md, "Menus").
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
    /// Its title shown on top of the menu too (the sheet of the phone classes always shows a title): a menu opened by
    /// a long press on a button names the button (qt/docs/features/adaptive-layout.md, "Labels without hover")
    property bool titleShown: false
    /// As a submenu: the icon of its entry in the menu above
    property string iconName: ""
    property real minimumWidth: 200
    property real maximumWidth: 420
    /// The window's layout (Main.qml's `win.adaptive`), if there is one
    readonly property var adaptiveLayout: typeof win !== "undefined" && win ? win.adaptive : null
    /// In a phone class (by the layout class: "Adapt the layout" off keeps the desktop menus) it opens as a sheet
    readonly property bool asSheet: adaptiveLayout !== null && adaptiveLayout.phoneLayout
    /// The widest entry offered (a binding: entries come and go, and change their text, with the document)
    readonly property real entryWidth: {
        let w = 0
        for (let i = 0; i < count; ++i) {
            const it = itemAt(i)
            if (it && it.offered !== false) w = Math.max(w, it.implicitWidth)
        }
        return titleShown && title !== "" ? Math.max(w, titleMetrics.advanceWidth + 32) : w
    }
    readonly property real windowWidth: typeof win !== "undefined" && win ? win.width : 100000
    readonly property real windowHeight: typeof win !== "undefined" && win ? win.height : 100000

    // (whole pixels: the text widths are fractional, and a row of controls (a RowLayout) puts its buttons on whole
    // pixels, so its last button stuck out of a menu a fraction of a pixel wide)
    implicitWidth: Math.min(windowWidth - 16, Math.max(minimumWidth, Math.min(maximumWidth, Math.ceil(entryWidth + leftPadding + rightPadding))))
    margins: 8
    verticalPadding: 6

    // Submenus get an entry that can be left out (`offered`) and has the same height as the others
    delegate: AdaptiveMenuItem {
        offered: subMenu ? subMenu.offered !== false : true
        icon.source: subMenu && subMenu.iconName ? app.iconUrl(subMenu.iconName) : ""
    }

    contentItem: ListView {
        id: list
        implicitHeight: contentHeight
        implicitWidth: control.entryWidth
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        header: control.titleShown && control.title !== "" ? titleHeader : null
        // Scrolls only where the window is too short for it: then the bar shows that there is more
        ScrollBar.vertical: ScrollBar {
            objectName: "menuScrollBar"
            policy: list.interactive ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
    }

    readonly property TextMetrics titleMetrics: TextMetrics { text: control.title; font.weight: Font.DemiBold }
    Component {
        id: titleHeader
        Label {
            objectName: "menuTitle"
            width: ListView.view ? ListView.view.width : implicitWidth
            text: control.title
            elide: Text.ElideRight
            font.weight: Font.DemiBold
            color: "#5f6368"
            leftPadding: 16
            rightPadding: 16
            topPadding: 6
            bottomPadding: 6
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
        const safeTop = typeof win !== "undefined" && win && win.insets.top ? win.insets.top : 0
        // (above the navigation bar, and above the soft keyboard while it is open)
        const safeBottom = typeof win !== "undefined" && win && win.insets.bottom !== undefined
                           ? Math.max(win.insets.bottom, win.insets.keyboardHeight) : 0
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
