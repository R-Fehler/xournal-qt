// Layer actions (a port of Xournal++'s LayerActions plugin): whatever a command does to many pages is one undo step.
import { doc, layers, ui } from "xournal"

export function addTopLayer() {
    const v = ui.dialog({ title: "A new top layer on every page", ok: "Add",
                          fields: [{ id: "name", type: "text", label: "Name", value: "Notes" }] })
    if (v === null) return
    const n = doc.info().pageCount
    for (let i = 0; i < n; ++i) layers.add(i, { name: v.name })
}

function showLayers(all) {
    const page = doc.info().currentPage
    const info = doc.page(page)
    let first = true
    info.layers.forEach(function (l, i) {
        if (l.markdown) return  // (the page's Markdown boxes stay as they are)
        layers.setVisible(page, i, all || first)
        first = false
    })
}

export function onlyFirstLayer() { showLayers(false) }
export function allLayers() { showLayers(true) }
