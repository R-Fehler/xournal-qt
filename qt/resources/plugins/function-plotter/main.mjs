// The function plotter (qt/docs/features/plugins.md, "The function plotter"): a live dialog beside the page whose plot
// is previewed on the page in a frame the user moves and sizes; Insert puts it there as editable ink and Markdown boxes
// with math, as two groups (ink, boxes) that keep the plot's description, so "Edit plot" opens it again.
import { xournal, ui, elements, selection } from "xournal"
import { defaultSpec, valuesOf, specOf, fieldsOf, syncParams, pickColor } from "./lib/spec.mjs"
import { draw, analyse, ranges, exactSize, POINTS_PER_MM } from "./lib/plot.mjs"

const TR = {
    functions: "Functions", addFunction: "+ Function", addCurve: "+ Curve x(t), y(t)", dashed: "dashed", width: "Line",
    tFrom: "t from", tTo: "to", parameters: "Parameters", from: "from", to: "to", axes: "Axes",
    xFrom: "x from", xTo: "to", yAuto: "y automatic", yFrom: "y from", yTo: "to", piTicks: "π steps",
    xName: "x axis", yName: "y axis", look: "Look", grid: "Grid", numbers: "Numbers", arrows: "Arrows",
    origin: "0 at the origin", formulas: "Formulas", atCurve: "At the curves", legend: "In a legend",
    noFormula: "None", marks: "Mark", roots: "Zeros", extrema: "Extrema", intersections: "Intersections",
    size: "Size", exact: "Exact scale", unit: "1 unit =", decimalComma: "Decimal comma (1,5)"
}

/// The open dialog's plot: {spec, editing (the plot's id being edited, or null), exact (it was at the exact scale)}
let current = null

function decimalComma() { return xournal.decimalPoint === "," }
function paletteColors() { return ui.palette().map(function (c) { return c.color }) }

function frameFor(spec, frame) {
    const r = ranges(spec, analyse(spec))
    const size = exactSize(spec, r)
    return size ? { page: frame.page, x: frame.x, y: frame.y, width: size.width, height: size.height } : frame
}

function open(spec, frame, editing) {
    syncParams(spec, analyse(spec).params)
    current = { spec: spec, editing: editing, exact: spec.exact }
    ui.form({
        title: editing ? "Edit plot" : "Plot a function",
        insertLabel: editing ? "Update" : "Insert",
        fields: fieldsOf(spec, TR),
        values: valuesOf(spec),
        frame: frame,
        change: change,
        insert: insert
    })
}

/// "Plot a function…": a new plot in the middle of what is in view
export function plot() {
    open(defaultSpec(decimalComma(), paletteColors()), { width: 240, height: 200 }, null)
}

/// The dialog changed (or its frame moved): the plot again, and new fields when functions or parameters came or went
export function change(values, ctx) {
    let spec = specOf(values, current.spec)
    let fieldsChanged = spec.yAuto !== current.spec.yAuto
    const action = ctx.action || ""
    if (action === "addFunction" || action === "addCurve") {
        const color = pickColor(paletteColors(), spec.functions.length)
        spec.functions.push(action === "addCurve"
            ? { kind: "curve", x: "2cos(t)", y: "2sin(t)", t0: "0", t1: "2pi", color: color, width: 1.4, dash: false }
            : { kind: "function", expr: "", color: color, width: 1.4, dash: false })
        fieldsChanged = true
    } else if (action.indexOf("remove_f") === 0) {
        spec.functions.splice(Number(action.slice(8)), 1)
        fieldsChanged = true
    }
    const analysed = analyse(spec)
    if (syncParams(spec, analysed.params)) fieldsChanged = true
    current.spec = spec
    const frame = frameFor(spec, ctx.frame)
    const drawn = draw(spec, frame)
    const result = { shapes: drawn.shapes, errors: drawn.errors }
    if (fieldsChanged) {
        result.fields = fieldsOf(spec, TR)
        result.values = valuesOf(spec)
    }
    if (spec.exact) {
        result.frame = { width: frame.width, height: frame.height, resizable: false }
        const cm = function (v) { return (Math.round(v / POINTS_PER_MM) / 10).toString().replace(".", decimalComma() ? "," : ".") }
        result.message = "Printed at 100 %: " + cm(frame.width) + " × " + cm(frame.height) + " cm"
    } else if (current.exact) {
        result.frame = { resizable: true }
    }
    current.exact = spec.exact
    return result
}

/// The bounding box of some elements (from elements.list)
function boxOf(list) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
    list.forEach(function (e) {
        x0 = Math.min(x0, e.x)
        y0 = Math.min(y0, e.y)
        x1 = Math.max(x1, e.x + e.width)
        y1 = Math.max(y1, e.y + e.height)
    })
    return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 }
}

/// Insert (or Update): the old plot goes, the new one comes, as one undo step
export function insert(values, ctx) {
    const spec = specOf(values, current.spec)
    syncParams(spec, analyse(spec).params)
    const frame = frameFor(spec, ctx.frame)
    const drawn = draw(spec, frame)
    const errors = Object.keys(drawn.errors)
    if (errors.length > 0) throw new Error("the plot has a mistake: " + drawn.errors[errors[0]])
    if (drawn.shapes.length === 0) throw new Error("nothing to plot")
    if (current.editing) {
        const old = elements.list(frame.page, { onlyWithData: true }).filter(function (e) {
            return e.data && e.data.plot && e.data.plot.id === current.editing
        })
        if (old.length > 0) elements.remove(old.map(function (e) { return e.ref }), { withGroups: true })
    }
    const refs = elements.insert(frame.page, drawn.shapes, { group: true })
    // The description on both groups, with where the frame lies from the ink's box (for "Edit plot")
    const inkRefs = refs.filter(function (r, i) { return drawn.shapes[i].type === "stroke" })
    const boxRefs = refs.filter(function (r, i) { return drawn.shapes[i].type !== "stroke" })
    const listed = elements.list(frame.page, { withData: false })
    const ink = listed.filter(function (e) { return inkRefs.indexOf(e.ref) >= 0 })
    const box = boxOf(ink)
    const data = { plot: spec, frame: { dx: frame.x - box.x, dy: frame.y - box.y, width: frame.width, height: frame.height } }
    if (inkRefs.length > 0) elements.setData(inkRefs[0], data)
    if (boxRefs.length > 0) elements.setData(boxRefs[0], data)
    current = null
}

/// "Edit plot": the selected plot's dialog again, its frame where the plot is now
export function editPlot() {
    const sel = selection.get()
    let data = null
    if (sel) {
        sel.elements.forEach(function (e) {
            if (!data && e.data && e.data.plot) data = e.data
        })
    }
    if (!data) {
        ui.notify("Select a plot made with “Plot a function…” first")
        return
    }
    selection.clear()
    // (the plot's ink where it is now: the group of the element that keeps the description, in its layer)
    const listed = elements.list(sel.page)
    const anchor = listed.filter(function (e) {
        return e.data && e.data.plot && e.data.plot.id === data.plot.id && e.type === "stroke"
    })[0]
    let frame = { page: sel.page, width: data.frame.width, height: data.frame.height }
    if (anchor) {
        const group = listed.filter(function (e) {
            return e.type === "stroke" && e.layer === anchor.layer &&
                   (anchor.group !== 0 ? e.group === anchor.group : e.ref === anchor.ref)
        })
        const box = boxOf(group)
        frame.x = box.x + data.frame.dx
        frame.y = box.y + data.frame.dy
    }
    const spec = data.plot
    if (spec.decimalComma === undefined) spec.decimalComma = decimalComma()
    open(spec, frame, spec.id)
}
