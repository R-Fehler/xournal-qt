// Keyboard page operations in the sidebar and the page grid (they have the focus after a page was clicked):
// Ctrl+C/X/V/D, Delete, Ctrl+A, and Ctrl+Z / Ctrl+Y (the one undo stack of the document). With a sticky note
// selected Ctrl+C/X act on it; Ctrl+V pastes a copied note (AppController::pastesNoteBeforePages).
import QtQuick

QtObject {
    function isPageKey(event) {
        const ctrl = event.modifiers & Qt.ControlModifier
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) return true
        if (!ctrl) return false
        return [Qt.Key_C, Qt.Key_X, Qt.Key_V, Qt.Key_D, Qt.Key_A, Qt.Key_Z, Qt.Key_Y].indexOf(event.key) >= 0
    }
    /// Returns true if handled.
    function handle(event) {
        const ctrl = event.modifiers & Qt.ControlModifier
        const shift = event.modifiers & Qt.ShiftModifier
        const sel = app.pages.selectedPages()
        switch (event.key) {
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            app.deletePages(sel)
            return true
        // (a selected sticky note is copied or cut rather than the pages, a copied one pasted on the current page)
        case Qt.Key_C: if (ctrl) { if (app.noteSelected) app.copyStickyNote(); else app.copyPages(sel); return true } break
        case Qt.Key_X: if (ctrl) { if (app.noteSelected) app.cutStickyNote(); else app.cutPages(sel); return true } break
        case Qt.Key_V: if (ctrl) { if (app.pastesNoteBeforePages()) app.pasteElements(); else app.pastePages(-1); return true } break
        case Qt.Key_D: if (ctrl) { app.duplicatePages(sel); return true } break
        case Qt.Key_A: if (ctrl) { app.pages.selectAll(); return true } break
        case Qt.Key_Z:
            if (ctrl) {
                if (shift) app.redoPages(); else app.undoPages()
                return true
            }
            break
        case Qt.Key_Y: if (ctrl) { app.redoPages(); return true } break
        }
        return false
    }
}
