.pragma library
// Where a menu opens. Menu.popup() without arguments uses the mouse position, which is stale (or somewhere else
// entirely) when the user tapped with a finger or a pen. So: at the position of the press when we know it, else
// below the item the menu belongs to. Qt flips the menu up or sideways when there is no room.
function openAt(menu, pos) {
    if (pos !== undefined && pos !== null) {
        menu.popup(menu.parent, pos.x, pos.y)
    } else if (menu.parent) {
        menu.popup(menu.parent, 0, menu.parent.height)
    } else {
        menu.popup()
    }
}
