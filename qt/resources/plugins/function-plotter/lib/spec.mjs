// The function plotter's description of a plot (what is kept with the plot on the page, as the plugin's data on its
// groups) and the dialog's fields for it.
//
// spec = {v: 1, id, functions: [{kind: "function", expr, color, width, dash} | {kind: "curve", x, y, t0, t1, color,
//         width, dash}], params: {a: {value, min, max, step}}, xMin, xMax, yAuto, yMin, yMax (texts: "-2pi" works),
//         grid, numbers, arrows, piTicks, origin, xName, yName, labels: "curve" | "legend" | "none",
//         exact, unitMm, marks: {roots, extrema, intersections}, decimalComma}

export const COLORS = ["#1a5fb4", "#c01c28", "#26a269", "#e66100", "#813d9c", "#202124"]

export function newId() {
    return "plot-" + Date.now().toString(36) + "-" + Math.floor(Math.random() * 1e9).toString(36)
}

/// A new plot's description; `colors`: the palette's (its first ones are the curves' colors)
export function defaultSpec(decimalComma, colors) {
    return {
        v: 1,
        id: newId(),
        functions: [{ kind: "function", expr: "x^2 - 2x - 1", color: pickColor(colors, 0), width: 1.4, dash: false }],
        params: {},
        xMin: "-4", xMax: "5", yAuto: true, yMin: "-3", yMax: "6",
        grid: true, numbers: true, arrows: true, piTicks: false, origin: true,
        xName: "x", yName: "y", labels: "curve",
        exact: false, unitMm: 10,
        marks: { roots: false, extrema: false, intersections: false },
        decimalComma: !!decimalComma
    }
}

/// How light a color is (0 black … 1 white)
function lightness(c) {
    const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(c)
    if (!m) return 0
    return (0.2126 * parseInt(m[1], 16) + 0.7152 * parseInt(m[2], 16) + 0.0722 * parseInt(m[3], 16)) / 255
}

/// The palette's color for the i-th curve (colors that stand out on paper: not near black, white or grey)
export function pickColor(colors, i) {
    const usable = (colors || []).filter(function (c) {
        const l = lightness(c)
        const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(c)
        const spread = m ? Math.max(parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)) -
                           Math.min(parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)) : 0
        return l > 0.15 && l < 0.8 && spread > 60
    })
    const list = usable.length >= 3 ? usable : COLORS
    return list[i % list.length]
}

/// The dialog's values of a description (by field id)
export function valuesOf(spec) {
    const v = {
        xMin: spec.xMin, xMax: spec.xMax, yAuto: spec.yAuto, yMin: spec.yMin, yMax: spec.yMax,
        grid: spec.grid, numbers: spec.numbers, arrows: spec.arrows, piTicks: spec.piTicks, origin: spec.origin,
        xName: spec.xName, yName: spec.yName, labels: spec.labels, exact: spec.exact, unitMm: spec.unitMm,
        roots: spec.marks.roots, extrema: spec.marks.extrema, intersections: spec.marks.intersections,
        decimalComma: spec.decimalComma
    }
    spec.functions.forEach(function (fn, i) {
        if (fn.kind === "curve") {
            v["f" + i + "_x"] = fn.x
            v["f" + i + "_y"] = fn.y
            v["f" + i + "_t0"] = fn.t0
            v["f" + i + "_t1"] = fn.t1
        } else {
            v["f" + i + "_expr"] = fn.expr
        }
        v["f" + i + "_color"] = fn.color
        v["f" + i + "_width"] = fn.width
        v["f" + i + "_dash"] = fn.dash
    })
    Object.keys(spec.params).forEach(function (p) {
        const q = spec.params[p]
        v["p_" + p] = q.value
        v["p_" + p + "_min"] = q.min
        v["p_" + p + "_max"] = q.max
    })
    return v
}

/// The description after the dialog's values changed (the functions' kinds and the parameters from `before`)
export function specOf(values, before) {
    const s = JSON.parse(JSON.stringify(before))
    const take = function (key, field) { if (values[field || key] !== undefined) s[key] = values[field || key] }
    ;["xMin", "xMax", "yAuto", "yMin", "yMax", "grid", "numbers", "arrows", "piTicks", "origin", "xName", "yName",
      "labels", "exact", "decimalComma"].forEach(function (k) { take(k) })
    if (values.unitMm !== undefined && Number(values.unitMm) > 0) s.unitMm = Number(values.unitMm)
    ;["roots", "extrema", "intersections"].forEach(function (k) { if (values[k] !== undefined) s.marks[k] = values[k] })
    s.functions.forEach(function (fn, i) {
        const g = function (k) { return values["f" + i + "_" + k] }
        if (fn.kind === "curve") {
            if (g("x") !== undefined) fn.x = g("x")
            if (g("y") !== undefined) fn.y = g("y")
            if (g("t0") !== undefined) fn.t0 = g("t0")
            if (g("t1") !== undefined) fn.t1 = g("t1")
        } else if (g("expr") !== undefined) {
            fn.expr = g("expr")
        }
        if (g("color") !== undefined) fn.color = g("color")
        if (g("width") !== undefined && Number(g("width")) > 0) fn.width = Number(g("width"))
        if (g("dash") !== undefined) fn.dash = g("dash")
    })
    Object.keys(s.params).forEach(function (p) {
        const q = s.params[p]
        if (values["p_" + p] !== undefined) q.value = Number(values["p_" + p])
        if (values["p_" + p + "_min"] !== undefined && isFinite(Number(values["p_" + p + "_min"]))) q.min = Number(values["p_" + p + "_min"])
        if (values["p_" + p + "_max"] !== undefined && isFinite(Number(values["p_" + p + "_max"]))) q.max = Number(values["p_" + p + "_max"])
        if (q.max <= q.min) q.max = q.min + 1
        q.step = niceSliderStep(q.max - q.min)
    })
    return s
}

function niceSliderStep(range) {
    const raw = range / 100
    const p = Math.pow(10, Math.floor(Math.log10(raw)))
    return raw / p < 2 ? p : raw / p < 5 ? 2 * p : 5 * p
}

/// The parameters a description needs (`names`): new ones with a slider from -5 to 5 at 1, gone ones dropped.
/// True if they changed.
export function syncParams(spec, names) {
    let changed = false
    names.forEach(function (n) {
        if (!spec.params[n]) {
            spec.params[n] = { value: 1, min: -5, max: 5, step: 0.1 }
            changed = true
        }
    })
    Object.keys(spec.params).forEach(function (n) {
        if (names.indexOf(n) < 0) {
            delete spec.params[n]
            changed = true
        }
    })
    return changed
}

/// The dialog's fields for a description
export function fieldsOf(spec, tr) {
    const f = []
    const dcHint = spec.decimalComma ? "x^2 - 2x + 1,5" : "x^2 - 2x + 1.5"
    f.push({ type: "label", style: "heading", text: tr.functions })
    spec.functions.forEach(function (fn, i) {
        const name = ["f", "g", "h", "p", "q", "r", "s", "u", "v", "w"][i % 10]
        if (fn.kind === "curve") {
            f.push({ id: "f" + i + "_x", type: "text", label: "x(t) =", row: "f" + i + "a", placeholder: "cos(t)" })
            f.push({ id: "remove_f" + i, type: "button", label: "✕", row: "f" + i + "a" })
            f.push({ id: "f" + i + "_y", type: "text", label: "y(t) =", placeholder: "sin(t)" })
            f.push({ id: "f" + i + "_t0", type: "text", label: tr.tFrom, row: "f" + i + "t", width: 90 })
            f.push({ id: "f" + i + "_t1", type: "text", label: tr.tTo, row: "f" + i + "t", width: 90 })
        } else {
            f.push({ id: "f" + i + "_expr", type: "text", label: name + "(x) =", row: "f" + i + "a", placeholder: dcHint })
            f.push({ id: "remove_f" + i, type: "button", label: "✕", row: "f" + i + "a" })
        }
        f.push({ id: "f" + i + "_color", type: "color" })
        f.push({ id: "f" + i + "_width", type: "number", label: tr.width, unit: "pt", row: "f" + i + "c", width: 80 })
        f.push({ id: "f" + i + "_dash", type: "checkbox", label: tr.dashed, row: "f" + i + "c" })
    })
    f.push({ id: "addFunction", type: "button", label: tr.addFunction, row: "add" })
    f.push({ id: "addCurve", type: "button", label: tr.addCurve, row: "add" })
    const params = Object.keys(spec.params)
    if (params.length > 0) {
        f.push({ type: "label", style: "heading", text: tr.parameters })
        params.forEach(function (p) {
            const q = spec.params[p]
            f.push({ id: "p_" + p, type: "slider", label: p, min: q.min, max: q.max, step: q.step, row: "p_" + p })
            f.push({ id: "p_" + p + "_min", type: "number", label: tr.from, row: "p_" + p, width: 56 })
            f.push({ id: "p_" + p + "_max", type: "number", label: tr.to, row: "p_" + p, width: 56 })
        })
    }
    f.push({ type: "label", style: "heading", text: tr.axes })
    f.push({ id: "xMin", type: "text", label: tr.xFrom, row: "x", width: 90 })
    f.push({ id: "xMax", type: "text", label: tr.xTo, row: "x", width: 90 })
    f.push({ id: "piTicks", type: "checkbox", label: tr.piTicks, row: "x" })
    f.push({ id: "yAuto", type: "checkbox", label: tr.yAuto, row: "y" })
    if (!spec.yAuto) {
        f.push({ id: "yMin", type: "text", label: tr.yFrom, row: "y", width: 90 })
        f.push({ id: "yMax", type: "text", label: tr.yTo, row: "y", width: 90 })
    }
    f.push({ id: "xName", type: "text", label: tr.xName, row: "names", width: 90 })
    f.push({ id: "yName", type: "text", label: tr.yName, row: "names", width: 90 })
    f.push({ type: "label", style: "heading", text: tr.look })
    f.push({ id: "grid", type: "checkbox", label: tr.grid, row: "look1" })
    f.push({ id: "numbers", type: "checkbox", label: tr.numbers, row: "look1" })
    f.push({ id: "arrows", type: "checkbox", label: tr.arrows, row: "look2" })
    f.push({ id: "origin", type: "checkbox", label: tr.origin, row: "look2" })
    f.push({ id: "decimalComma", type: "checkbox", label: tr.decimalComma })
    f.push({ id: "labels", type: "choice", label: tr.formulas,
             options: [{ value: "curve", label: tr.atCurve }, { value: "legend", label: tr.legend },
                       { value: "none", label: tr.noFormula }] })
    f.push({ type: "label", style: "heading", text: tr.marks })
    f.push({ id: "roots", type: "checkbox", label: tr.roots, row: "marks" })
    f.push({ id: "extrema", type: "checkbox", label: tr.extrema, row: "marks" })
    f.push({ id: "intersections", type: "checkbox", label: tr.intersections })
    f.push({ type: "label", style: "heading", text: tr.size })
    f.push({ id: "exact", type: "checkbox", label: tr.exact, row: "exact" })
    f.push({ id: "unitMm", type: "number", label: tr.unit, unit: "mm", row: "exact", width: 80 })
    return f
}
