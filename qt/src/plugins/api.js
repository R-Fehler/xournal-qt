// xournal-qt: the plugin API, the module "xournal" (qt/docs/features/plugins.md, "The API").
//
// Evaluated once per plugin engine as a function of the native bridge and some facts; it returns the API as frozen
// objects. Each function calls bridge.call(operation, arguments): the operations layer checks it against the plugin's
// permissions and applies it. The bridge itself stays in this closure (the plugin never sees it).
(function (bridge, info) {
    "use strict"

    function call(op, args) {
        // (not a tail call: the plugin's frame stays in an error's stack, for its file and line)
        const result = bridge.call(op, args === undefined ? {} : args)
        return result
    }
    function withArgs(base, more) {
        const out = {}
        const add = function (o) {
            if (!o) return
            Object.keys(o).forEach(function (k) { out[k] = o[k] })
        }
        add(more)
        add(base)
        return out
    }
    function deepFreeze(o) {
        Object.getOwnPropertyNames(o).forEach(function (k) {
            const v = o[k]
            if (v !== null && typeof v === "object" && !Object.isFrozen(v)) deepFreeze(v)
        })
        return Object.freeze(o)
    }
    function text(args) {
        const parts = []
        for (let i = 0; i < args.length; ++i) {
            const a = args[i]
            if (typeof a === "string") parts.push(a)
            else if (a instanceof Error) parts.push(String(a))
            else {
                let s
                try { s = JSON.stringify(a) } catch (e) { s = String(a) }
                parts.push(s === undefined ? String(a) : s)
            }
        }
        return parts.join(" ")
    }
    function logger(level) {
        return function () { bridge.log(level, text(arguments)) }
    }

    const console = {
        log: logger(0), info: logger(0), debug: logger(0), warn: logger(1), error: logger(2)
    }

    const xournal = {
        apiVersion: info.apiVersion,
        platform: info.platform,
        locale: info.locale,
        decimalPoint: info.decimalPoint,
        plugin: info.plugin,
        /// Any operation by its name (the vocabulary of the operations layer)
        apply: function (op, args) { return call(op, args) },
        log: logger(0)
    }

    const doc = {
        /// {pageCount, currentPage, readOnly, file}
        info: function () { return call("document.read") },
        /// {index, width, height, background: {type, color}, layers: [{name, visible, elements, markdown}], selectedLayer}
        page: function (page) { return call("page.read", { page: page }) }
    }

    const elements = {
        /// [{ref, type, layer, group, x, y, width, height, color, data}]; options: {layer, onlyWithData, withData}
        list: function (page, options) { return call("element.list", withArgs({ page: page }, options)) },
        /// shapes: [{type: "stroke" | "text" | "markdown", …}]; options: {layer, group, data} → [ref, …]
        insert: function (page, shapes, options) {
            return call("element.insert", withArgs({ page: page, shapes: shapes }, options))
        },
        /// options: {withGroups} → the number removed
        remove: function (refs, options) { return call("element.delete", withArgs({ refs: refs }, options)) },
        /// The plugin's own data on an element (null removes it)
        setData: function (ref, value) { return call("element.data", { ref: ref, value: value }) }
    }

    const layers = {
        add: function (page, options) { return call("layer.add", withArgs({ page: page }, options)) },
        rename: function (page, layer, name) { return call("layer.rename", { page: page, layer: layer, name: name }) },
        setVisible: function (page, layer, visible) {
            return call("layer.visible", { page: page, layer: layer, visible: visible })
        },
        select: function (page, layer) { return call("layer.select", { page: page, layer: layer }) }
    }

    const pages = {
        /// options: {at, count, background} → the index of the first new page
        insert: function (options) { return call("page.insert", options || {}) },
        remove: function (indices) { return call("page.delete", { pages: indices }) },
        setBackground: function (indices, type, color) {
            const args = { type: type }
            if (indices !== undefined && indices !== null) args.pages = indices
            if (color !== undefined) args.color = color
            return call("background.set", args)
        }
    }

    const selection = {
        /// {page, elements: [{type, group, x, y, width, height, color, data}]} or null
        get: function () { return call("selection.read") },
        clear: function () { return call("selection.clear") }
    }

    const tools = {
        /// {type, color, width}
        get: function () { return call("tool.read") },
        select: function (type) { return call("tool.select", { type: type }) },
        setColor: function (color) { return call("tool.color", { color: color }) },
        setWidth: function (width) { return call("tool.width", { width: width }) }
    }

    const view = {
        /// {page, zoom, visible: {page, x, y, width, height}} (what of the page is in view, page coordinates)
        get: function () { return call("view.read") },
        goToPage: function (page) { return call("view.goto", { page: page }) },
        setZoom: function (percent) { return call("view.zoom", { percent: percent }) }
    }

    const ui = {
        notify: function (message) { return call("ui.notify", { text: String(message) }) },
        /// A modal dialog: {title, fields: […], ok, cancel} → the values, or null when cancelled
        dialog: function (spec) { return call("ui.dialog", spec) },
        /// A live dialog beside the page with a preview: {title, fields, values, insertLabel, frame, change, insert}
        form: function (spec) { return call("ui.form", spec) },
        /// The colors of the palette chosen now: [{key, name, color}]
        palette: function () { return call("ui.palette") }
    }

    const files = {
        /// A file the user chooses to read: a handle {id, name}, or null
        chooseOpen: function (options) { return call("file.chooseOpen", options || {}) },
        /// A file the user chooses to write: a handle {id, name}, or null
        chooseSave: function (options) { return call("file.chooseSave", options || {}) },
        readText: function (handle) { return call("file.read", { handle: handle && handle.id }) },
        writeText: function (handle, content) {
            return call("file.write", { handle: handle && handle.id, text: String(content) })
        },
        /// The document as a PDF into a chosen file
        exportPdf: function (handle) { return call("file.export", { handle: handle && handle.id, format: "pdf" }) }
    }

    return deepFreeze({
        xournal: xournal, doc: doc, elements: elements, layers: layers, pages: pages, selection: selection,
        tools: tools, view: view, ui: ui, files: files, console: console
    })
})
