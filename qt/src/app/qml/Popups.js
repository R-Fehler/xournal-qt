.pragma library
// Where a menu opens. Menu.popup() without arguments uses the mouse position, which is stale (or somewhere else
// entirely) when the user tapped with a finger or a pen. So: at the position of the press when we know it, else
// below the item the menu belongs to. Qt flips the menu up or sideways when there is no room.
// An AdaptiveMenu places itself (clear of its button; a sheet on phones): its openMenu() does it.
function openAt(menu, pos) {
    if (typeof menu.openMenu === "function") {
        menu.openMenu(pos)
    } else if (pos !== undefined && pos !== null) {
        menu.popup(menu.parent, pos.x, pos.y)
    } else if (menu.parent) {
        menu.popup(menu.parent, 0, menu.parent.height)
    } else {
        menu.popup()
    }
}
/// A popup of this item's own (a menu declared inside it) is open: e.g. a button in "more tools" opened its menu
function hasOpenPopup(item) {
    if (!item || !item.data) return false
    for (let i = 0; i < item.data.length; ++i) {
        const o = item.data[i]
        if (o && o.opened === true) return true
    }
    return false
}
/// Some item in `container` has a popup of its own open
function hasOpenPopupIn(container) {
    if (!container) return false
    const children = container.children
    for (let i = 0; i < children.length; ++i) {
        if (hasOpenPopup(children[i])) return true
    }
    return false
}
