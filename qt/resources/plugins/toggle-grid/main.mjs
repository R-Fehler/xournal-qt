// Toggle grid: squared paper on or off (a port of Xournal++'s ToggleGrid plugin). Off gives lined paper back (the
// app's default note paper); PDF and picture pages keep their background.
import { doc, pages } from "xournal"

function next(page) {
    return doc.page(page).background.type === "graph" ? "lined" : "graph"
}

export function toggle() {
    const page = doc.info().currentPage
    pages.setBackground([page], next(page))
}

export function toggleAll() {
    const info = doc.info()
    const type = next(info.currentPage)  // (all pages the same way: as the current one goes)
    const all = []
    for (let i = 0; i < info.pageCount; ++i) all.push(i)
    pages.setBackground(all, type)
}
