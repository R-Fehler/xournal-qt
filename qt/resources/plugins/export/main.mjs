// Export (a port of Xournal++'s Export plugin): there it wrote next to the document without asking; here a plugin
// reaches a file only through a dialog the user answers (a handle, no paths).
import { doc, files, ui } from "xournal"

function baseName() {
    const file = doc.info().file || "Document"
    return file.replace(/\.[^.]*$/, "")
}

export function exportPdf() {
    const handle = files.chooseSave({ title: "Export as PDF", name: baseName() + ".pdf", filters: ["PDF (*.pdf)"] })
    if (handle === null) return
    files.exportPdf(handle)
    ui.notify("Exported " + handle.name)
}

export function exportOutline() {
    const handle = files.chooseSave({ title: "Export the outline", name: baseName() + ".md", filters: ["Markdown (*.md)"] })
    if (handle === null) return
    const info = doc.info()
    const lines = ["# " + baseName(), ""]
    for (let i = 0; i < info.pageCount; ++i) {
        const p = doc.page(i)
        const count = p.layers.reduce(function (n, l) { return n + l.elements }, 0)
        lines.push("- Page " + (i + 1) + ": " + p.background.type + ", " + count + " element" + (count === 1 ? "" : "s"))
    }
    files.writeText(handle, lines.join("\n") + "\n")
    ui.notify("Wrote " + handle.name)
}
