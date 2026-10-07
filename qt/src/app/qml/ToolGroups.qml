// The cycling tool buttons (qt/docs/adaptive-layout.md, "Cycling buttons"): tools that do almost the same share one
// button. A tap on it while its tool is in use takes the next variant; a tap while another tool is in use takes it
// with the variant last used (remembered per group, in the settings); a long press lists all variants. The fixed
// tools of the toolbox, the phone's sheet "My tools" and the toolbox's snip entry use the same groups.
//   select    rectangle ↔ lasso (the multi-layer ones only in the list)
//   snip      rectangle ↔ lasso snip (one picture to the clipboard, then the tool before, qt/docs/snip.md): a fixed
//             tool of the toolbox (qt/copy-tools); the toolbox's snip entry cycles the same way
//   text      mark PDF text ↔ copy handwriting as text (one sweep over ink, its words to the clipboard, then the tool
//             before; qt/docs/handwriting-search.md); the PDF text tool's button, whose list also says how it marks
//   geometry  setsquare ↔ compass (on the page; the geometry pill takes it away); curtain and spotlight only in the list (they are
//             not tools of their own: they lie over the page whatever tool is in hand, qt/docs/curtain.md)
// The kinds of shapes and erasers are listed here too (the toolbox's editor offers them); the toolbox's entries take
// them. The keyboard's tools (S, L, Shift+S, Shift+L) take a variant directly; the button follows and remembers it.
import QtQuick

QtObject {
    id: groups

    readonly property var defs: ({
        "select": {
            name: qsTr("Select"),
            variants: [
                { key: "selectRect", icon: "xopp-select-rect", name: qsTr("Select a rectangle") },
                { key: "selectRegion", icon: "xopp-select-lasso", name: qsTr("Lasso") },
                { key: "selectMultiLayerRect", icon: "xopp-select-rect", name: qsTr("Rectangle on all layers"), listOnly: true },
                { key: "selectMultiLayerRegion", icon: "xopp-select-lasso", name: qsTr("Lasso on all layers"), listOnly: true }
            ]
        },
        // The snips as a group of their own (their button, the toolbox's snip entry): the icon shows which
        "snip": {
            name: qsTr("Snip (copy a picture)"),
            variants: [
                { key: "snipRect", icon: "xqt-snip-rect", name: qsTr("Snip a rectangle (copy its picture)"), snip: "rect" },
                { key: "snipLasso", icon: "xqt-snip-lasso", name: qsTr("Snip with the lasso (copy its picture)"), snip: "lasso" }
            ]
        },
        // Text on the page: PDF text marked, or handwriting copied as text (one sweep, then the tool before)
        "text": {
            name: qsTr("Mark PDF text, copy handwriting"),
            variants: [
                { key: "markPdfText", icon: "xqt-mark-text", name: qsTr("Mark PDF text") },
                { key: "copyInkText", icon: "xqt-copy-ink-text", name: qsTr("Copy handwriting as text") }
            ]
        },
        "shape": {
            name: qsTr("Shapes"),
            variants: [
                { key: "line", icon: "xopp-draw-line", name: qsTr("Line") },
                { key: "rectangle", icon: "xopp-draw-rect", name: qsTr("Rectangle") },
                { key: "ellipse", icon: "xopp-draw-ellipse", name: qsTr("Ellipse") },
                { key: "arrow", icon: "xopp-draw-arrow", name: qsTr("Arrow") },
                { key: "doubleArrow", icon: "xopp-draw-double-arrow", name: qsTr("Double arrow") },
                { key: "drawCoordinateSystem", icon: "xopp-draw-coordinate-system", name: qsTr("Coordinate system") },
                { key: "strokeRecognizer", icon: "xopp-shape-recognizer", name: qsTr("Recognize shapes") }
            ]
        },
        "geometry": {
            name: qsTr("Setsquare, compass and curtain"),
            variants: [
                { key: "setsquare", icon: "xopp-setsquare", name: qsTr("Setsquare") },
                { key: "compass", icon: "xopp-compass", name: qsTr("Compass") },
                { key: "curtain", icon: "xqt-curtain", name: qsTr("Curtain") + win.keyNote("curtain"), listOnly: true, curtain: true },
                { key: "spotlight", icon: "xqt-spotlight", name: qsTr("Spotlight") + win.keyNote("spotlight"), listOnly: true, curtain: true }
            ]
        },
        "eraser": {
            name: qsTr("Eraser"),
            variants: [
                { key: "default", icon: "xopp-tool-eraser", name: qsTr("Eraser") },
                { key: "whiteout", icon: "xqt-eraser-whiteout", name: qsTr("Whiteout") },
                { key: "deleteStroke", icon: "xqt-eraser-stroke", name: qsTr("Whole strokes") }
            ]
        }
    })

    /// How sharp a snip's picture is (app.snipResolution; Settings and the snip's list, qt/docs/snip.md)
    readonly property var snipResolutions: [
        { key: "screen", name: qsTr("As sharp as the screen (at least 200 dpi)"), short: qsTr("Screen") },
        { key: "high", name: qsTr("High resolution (300 dpi)"), short: qsTr("300 dpi") },
        { key: "veryHigh", name: qsTr("Very high resolution (600 dpi)"), short: qsTr("600 dpi") }
    ]
    function name(group) { return defs[group].name }
    function variants(group) { return defs[group].variants }
    /// The variants a tap goes through (not the list-only ones)
    function cycle(group) { return defs[group].variants.filter(function(v) { return !v.listOnly }) }
    /// A variant that is the curtain (put out or taken away beside the group's tool, never remembered as its variant)
    /// A variant that is a snip ("rect", "lasso"; "": none): one picture copied, then the tool before comes back
    function snipOf(group, key) {
        const v = defs[group].variants.filter(function(v) { return v.key === key })
        return v.length > 0 && v[0].snip ? v[0].snip : ""
    }
    function isCurtain(group, key) {
        const v = defs[group].variants.filter(function(v) { return v.key === key })
        return v.length > 0 && v[0].curtain === true
    }
    function variant(group, key) {
        const vs = defs[group].variants
        for (let i = 0; i < vs.length; ++i) if (vs[i].key === key) return vs[i]
        return vs[0]
    }

    /// The variant in use now ("": the group's tool is not in use)
    function activeKey(group) {
        const tool = app.tool
        if (group === "snip")
            return app.snip === "" ? "" : app.snip === "lasso" ? "snipLasso" : "snipRect"
        // (the select tool in hand snips, or copies handwriting: the snip button's, the text button's)
        if (group === "select" && (app.snip !== "" || app.inkCopy)) return ""
        if (group === "text")
            return app.inkCopy ? "copyInkText" : tool === "selectPdfTextLinear" || tool === "selectPdfTextRect" ? "markPdfText" : ""
        if (group === "select")
            return ["selectRect", "selectRegion", "selectMultiLayerRect", "selectMultiLayerRegion"].indexOf(tool) >= 0 ? tool : ""
        if (group === "geometry") return app.geometryTool
        return ""
    }
    function isActive(group) { return activeKey(group) !== "" }
    /// The variant the button shows: the one in use, else the one last used
    function current(group) {
        const a = activeKey(group)
        return a !== "" ? a : last(group)
    }
    readonly property var remembered: {
        const map = {}
        const text = String((app.settings.revision, app.settings.get("toolVariants")) || "")
        text.split(";").forEach(function(part) {
            const eq = part.indexOf("=")
            if (eq > 0) map[part.substring(0, eq)] = part.substring(eq + 1)
        })
        return map
    }
    /// The variant last used of a group (the first one at the start)
    function last(group) {
        const key = remembered[group]
        const vs = defs[group].variants
        for (let i = 0; i < vs.length; ++i) if (vs[i].key === key) return key
        return vs[0].key
    }
    function remember(group, key) {
        if (remembered[group] === key) return
        const map = Object.assign({}, remembered)
        map[group] = key
        app.settings.set("toolVariants", Object.keys(map).sort().map(function(k) { return k + "=" + map[k] }).join(";"))
    }

    /// Takes the group's tool with this variant (default: the one last used)
    function activate(group, key) {
        if (key === undefined || key === "") key = last(group)
        if (isCurtain(group, key)) {
            app.toggleCurtain(key)
            return
        }
        if (snipOf(group, key) !== "") {
            app.startSnip(snipOf(group, key))
            if (group === "snip") remember(group, key)  // (the select button stays the selection's)
            return
        }
        if (group === "select") {
            app.selectTool(key)
        } else if (group === "text") {
            if (key === "copyInkText") {
                if (!app.startInkCopy()) return  // (the handwriting search is off: the window says so)
            } else {
                app.selectTool("selectPdfTextLinear")
            }
        } else if (group === "geometry") {
            if (app.geometryTool !== key) app.toggleGeometryTool(key)
        }
        remember(group, key)
    }
    /// The next variant of the cycle (after a list-only one: the first)
    function next(group) {
        const c = cycle(group)
        let i = -1
        for (let j = 0; j < c.length; ++j) if (c[j].key === current(group)) i = j
        activate(group, c[(i + 1) % c.length].key)
    }
    /// A tap on the group's button
    function tap(group) {
        if (isActive(group)) next(group)
        else activate(group)
    }

    /// The colors used last, newest first (the toolbox editor's "colors used lately"; at most 12)
    readonly property var recentColors: {
        const text = String((app.settings.revision, app.settings.get("recentColors")) || "")
        return text === "" ? [] : text.split(",")
    }
    function noteColor(c) {
        const name = String(c)
        if (recentColors.length > 0 && Qt.colorEqual(recentColors[0], name)) return
        const list = [name].concat(recentColors.filter(function(o) { return !Qt.colorEqual(o, name) })).slice(0, 12)
        app.settings.set("recentColors", list.join(","))
    }

    // A variant taken another way (a key, a menu) is the one to come back to; a color used is a recent one
    readonly property Connections followTools: Connections {
        target: app
        function onToolChanged() {
            ["select", "geometry", "text"].forEach(function(g) {
                const a = groups.activeKey(g)
                if (a !== "" && groups.snipOf(g, a) === "") groups.remember(g, a)
            })
            const snip = groups.activeKey("snip")  // (Shift+S, Shift+L)
            if (snip !== "") groups.remember("snip", snip)
            if (["pen", "highlighter", "text"].indexOf(app.tool) >= 0) groups.noteColor(app.color)
        }
    }
}
