// The function plotter's drawing: a plot's description (spec.mjs) and its frame on the page → shapes (strokes for the
// grid, the axes, the ticks and the curves; Markdown boxes with math for the numbers, the axis names and the formulas),
// as the plugin API inserts them and the live dialog previews them.

import { parse, compile, toLatex, numberTex } from "./parse.mjs"
import { niceStep, piStep, ticks, piLabel, numberLabel, shortNumber, roundOut } from "./ticks.mjs"
import { sampleFunction, sampleCurve, fitRange, roots, extrema, toPage } from "./sample.mjs"

export const GRID_COLOR = "#c9d1d9"
export const AXIS_COLOR = "#202124"
export const POINTS_PER_MM = 72 / 25.4
const NAMES = ["f", "g", "h", "p", "q", "r", "s", "u", "v", "w"]

/// A number typed into a range field ("-2pi", "1,5", "3"): its value, or NaN
export function evaluateConstant(text, decimalComma) {
    const r = parse(String(text), { variable: "x", decimalComma: true })
    if (!r.ok || r.params.length > 0) return NaN
    const v = compile(r.tree)({})
    return typeof v === "number" ? v : NaN
}

/// Parses every formula of a spec: {functions: [{fx | fx, fy, tex, error, field}], params: [names], errors: {field: msg}}
export function analyse(spec) {
    const dc = spec.decimalComma
    const errors = {}
    const params = {}
    const out = []
    spec.functions.forEach(function (fn, i) {
        const name = NAMES[i % NAMES.length]
        if (fn.kind === "curve") {
            const rx = parse(fn.x, { variable: "t", decimalComma: true })
            const ry = parse(fn.y, { variable: "t", decimalComma: true })
            if (!rx.ok) errors["f" + i + "_x"] = rx.error
            if (!ry.ok) errors["f" + i + "_y"] = ry.error
            const t0 = evaluateConstant(fn.t0, dc), t1 = evaluateConstant(fn.t1, dc)
            if (!isFinite(t0) || !isFinite(t1) || !(t1 > t0)) errors["f" + i + "_t0"] = "t goes from a number to a larger one"
            if (rx.ok && ry.ok) {
                rx.params.concat(ry.params).forEach(function (p) { params[p] = true })
                out.push({ index: i, fn: fn, curve: true, fx: compile(rx.tree), fy: compile(ry.tree), t0: t0, t1: t1,
                           tex: "x(t) = " + toLatex(rx.tree, { decimalComma: dc }) + ",\\; y(t) = " +
                                toLatex(ry.tree, { decimalComma: dc }),
                           ok: !errors["f" + i + "_t0"] })
            }
        } else {
            if (String(fn.expr).trim() === "") return  // (an empty field: no curve, no message)
            // (a decimal comma is understood whatever the numbers are written with)
            const r = parse(fn.expr, { variable: "x", decimalComma: true })
            if (!r.ok) {
                errors["f" + i + "_expr"] = r.error
                return
            }
            r.params.forEach(function (p) { params[p] = true })
            out.push({ index: i, fn: fn, curve: false, f: compile(r.tree),
                       tex: name + "(x) = " + toLatex(r.tree, { decimalComma: dc }), ok: true })
        }
    })
    return { functions: out, params: Object.keys(params).sort(), errors: errors }
}

/// The ranges and the scale of a spec for its functions: {x0, x1, y0, y1, unit (points per unit, exact scale), error}
export function ranges(spec, analysed) {
    const dc = spec.decimalComma
    const x0 = evaluateConstant(spec.xMin, dc), x1 = evaluateConstant(spec.xMax, dc)
    if (!isFinite(x0) || !isFinite(x1) || !(x1 > x0)) return { error: "x goes from a number to a larger one", field: "xMin" }
    let y0, y1
    const vars = paramValues(spec)
    if (spec.yAuto) {
        const fs = []
        analysed.functions.forEach(function (a) {
            if (!a.ok) return
            if (a.curve) {
                // (a curve's y values over its t range, those within the x range)
                fs.push(function (u) {
                    const t = a.t0 + (a.t1 - a.t0) * (u - x0) / (x1 - x0)
                    const x = a.fx(Object.assign({ t: t }, vars))
                    return x >= x0 && x <= x1 ? a.fy(Object.assign({ t: t }, vars)) : NaN
                })
            } else {
                fs.push(function (x) { return a.f(Object.assign({ x: x }, vars)) })
            }
        })
        const fit = fitRange(fs, x0, x1)
        if (fit) {
            // (out to whole steps of about ten ticks: an auto range ends on ticks, as a textbook's does)
            const out = roundOut(fit[0], fit[1], niceStep(fit[1] - fit[0], 100, 10))
            y0 = out[0]
            y1 = out[1]
        } else {
            y0 = -5
            y1 = 5
        }
    } else {
        y0 = evaluateConstant(spec.yMin, dc)
        y1 = evaluateConstant(spec.yMax, dc)
        if (!isFinite(y0) || !isFinite(y1) || !(y1 > y0)) return { error: "y goes from a number to a larger one", field: "yMin" }
    }
    return { x0: x0, x1: x1, y0: y0, y1: y1 }
}

export function paramValues(spec) {
    const out = {}
    Object.keys(spec.params || {}).forEach(function (k) { out[k] = Number(spec.params[k].value) })
    return out
}

/// The frame a spec needs at the exact scale ("1 unit = n mm"): {width, height} in points, or null (free size)
export function exactSize(spec, r) {
    if (!spec.exact || r.error) return null
    const unit = Math.max(1, Number(spec.unitMm) || 10) * POINTS_PER_MM
    return { width: (r.x1 - r.x0) * unit, height: (r.y1 - r.y0) * unit }
}

function stroke(points, color, width, extra) {
    const s = { type: "stroke", points: points, color: color, width: width }
    if (extra) Object.keys(extra).forEach(function (k) { s[k] = extra[k] })
    return s
}
function label(x, y, tex, size, color, anchor) {
    return { type: "markdown", x: x, y: y, text: "$" + tex + "$", size: size, color: color, anchor: anchor }
}
/// A filled arrowhead pointing in direction (dx, dy) with its tip at (x, y)
function arrowHead(x, y, dx, dy, color) {
    const len = 7, half = 3
    const bx = x - dx * len, by = y - dy * len
    return stroke([x, y, bx - dy * half, by + dx * half, bx + dy * half, by - dx * half], color, 0.6,
                  { closed: true, fill: 255, cap: "round" })
}
function dot(x, y, color, r) {
    const pts = []
    for (let i = 0; i < 12; ++i) {
        const a = i / 12 * 2 * Math.PI
        pts.push(x + r * Math.cos(a), y + r * Math.sin(a))
    }
    return stroke(pts, color, 0.4, { closed: true, fill: 255 })
}

/// The shapes of a plot in a frame ({x, y, width, height} on the page). {shapes, errors, ranges, analysed}
export function draw(spec, frame) {
    const analysed = analyse(spec)
    const errors = Object.assign({}, analysed.errors)
    const r = ranges(spec, analysed)
    if (r.error) {
        errors[r.field] = r.error
        return { shapes: [], errors: errors, analysed: analysed }
    }
    const dc = spec.decimalComma
    const view = { x0: r.x0, x1: r.x1, y0: r.y0, y1: r.y1, left: frame.x, top: frame.y, width: frame.width,
                   height: frame.height }
    const shapes = []
    const labels = []
    const P = function (x, y) { return toPage(view, x, y) }
    const right = frame.x + frame.width, bottom = frame.y + frame.height

    // --- ticks ---
    const xGap = 30, yGap = 20
    const xStep = spec.piTicks ? piStep(r.x1 - r.x0, frame.width, xGap) : null
    const xs = xStep ? xStep.value : niceStep(r.x1 - r.x0, frame.width, spec.exact ? 18 : xGap)
    const ys = niceStep(r.y1 - r.y0, frame.height, spec.exact ? 18 : yGap)
    const xTicks = ticks(r.x0, r.x1, xs)
    const yTicks = ticks(r.y0, r.y1, ys)
    // The axes: through 0 where 0 is in the range, else along the border
    const axisY = r.y0 <= 0 && r.y1 >= 0 ? 0 : r.y0
    const axisX = r.x0 <= 0 && r.x1 >= 0 ? 0 : r.x0
    const ay = P(0, axisY)[1], ax = P(axisX, 0)[0]

    if (spec.grid) {
        xTicks.forEach(function (t) {
            const px = P(t.v, 0)[0]
            shapes.push(stroke([px, frame.y, px, bottom], GRID_COLOR, 0.35, { cap: "butt" }))
        })
        yTicks.forEach(function (t) {
            const py = P(0, t.v)[1]
            shapes.push(stroke([frame.x, py, right, py], GRID_COLOR, 0.35, { cap: "butt" }))
        })
    }
    // Axes, with arrows at their ends
    const over = spec.arrows ? 8 : 0
    shapes.push(stroke([frame.x, ay, right + over, ay], AXIS_COLOR, 0.8, { cap: "butt" }))
    shapes.push(stroke([ax, bottom, ax, frame.y - over], AXIS_COLOR, 0.8, { cap: "butt" }))
    if (spec.arrows) {
        shapes.push(arrowHead(right + over + 1, ay, 1, 0, AXIS_COLOR))
        shapes.push(arrowHead(ax, frame.y - over - 1, 0, -1, AXIS_COLOR))
    }
    // Ticks and their numbers (not at the origin, not under the arrows)
    const tick = 3
    const numSize = 8
    xTicks.forEach(function (t) {
        const px = P(t.v, 0)[0]
        if (Math.abs(px - ax) < 0.5 && axisX === 0) return
        shapes.push(stroke([px, ay - tick, px, ay + tick], AXIS_COLOR, 0.6, { cap: "butt" }))
        if (spec.numbers && px < right - 4) {
            labels.push(label(px, ay + tick + 1.5, xStep ? piLabel(t.k, xStep.num, xStep.den) : numberLabel(t.v, xs, dc),
                              numSize, AXIS_COLOR, "top"))
        }
    })
    yTicks.forEach(function (t) {
        const py = P(0, t.v)[1]
        if (Math.abs(py - ay) < 0.5 && axisY === 0) return
        shapes.push(stroke([ax - tick, py, ax + tick, py], AXIS_COLOR, 0.6, { cap: "butt" }))
        if (spec.numbers && py > frame.y + 4) labels.push(label(ax - tick - 2, py, numberLabel(t.v, ys, dc), numSize, AXIS_COLOR, "right"))
    })
    if (spec.numbers && spec.origin && axisX === 0 && axisY === 0) {
        labels.push(label(ax - 3, ay + 2, "0", numSize, AXIS_COLOR, "top-right"))
    }
    // The axes' names at their ends
    const nameSize = 10
    if (String(spec.xName).trim() !== "") {
        labels.push(label(right + over + 1, ay + 4, nameTex(spec.xName), nameSize, AXIS_COLOR, "top-right"))
    }
    if (String(spec.yName).trim() !== "") {
        labels.push(label(ax - 6, frame.y - over - 1, nameTex(spec.yName), nameSize, AXIS_COLOR, "top-right"))
    }

    // --- the curves ---
    const vars = paramValues(spec)
    const formulas = []
    const plotted = []
    analysed.functions.forEach(function (a) {
        if (!a.ok) return
        const fn = a.fn
        const width = Math.max(0.2, Math.min(10, Number(fn.width) || 1.2))
        let lines
        if (a.curve) {
            lines = sampleCurve(function (t) { return a.fx(Object.assign({ t: t }, vars)) },
                                function (t) { return a.fy(Object.assign({ t: t }, vars)) }, a.t0, a.t1, view)
        } else {
            const f = function (x) { return a.f(Object.assign({ x: x }, vars)) }
            lines = sampleFunction(f, view)
            plotted.push({ f: f, color: fn.color })
        }
        lines.forEach(function (l) {
            shapes.push(stroke(l, fn.color, width, fn.dash ? { style: "dash" } : undefined))
        })
        formulas.push({ tex: a.tex, color: fn.color, lines: lines, dash: fn.dash, width: width })
    })

    // --- marks: roots, extrema, intersections ---
    const marks = []
    if (spec.marks) {
        plotted.forEach(function (p, i) {
            if (spec.marks.roots) roots(p.f, r.x0, r.x1).forEach(function (x) { marks.push({ x: x, y: 0, color: p.color }) })
            if (spec.marks.extrema) extrema(p.f, r.x0, r.x1).forEach(function (e) { marks.push({ x: e.x, y: e.y, color: p.color }) })
            if (spec.marks.intersections) {
                for (let j = i + 1; j < plotted.length; ++j) {
                    const q = plotted[j]
                    roots(function (x) { return p.f(x) - q.f(x) }, r.x0, r.x1).forEach(function (x) {
                        marks.push({ x: x, y: p.f(x), color: AXIS_COLOR })
                    })
                }
            }
        })
    }
    marks.forEach(function (m) {
        if (m.x < r.x0 || m.x > r.x1 || m.y < r.y0 || m.y > r.y1) return
        const pt = P(m.x, m.y)
        shapes.push(dot(pt[0], pt[1], m.color, 1.8))
        const sep = dc ? "\\,|\\," : ",\\,"
        labels.push(label(pt[0] + 3, pt[1] - 2, "(" + numberTex(shortNumber(m.x, false), { decimalComma: dc }) + sep +
                                                 numberTex(shortNumber(m.y, false), { decimalComma: dc }) + ")",
                          7, m.color, "bottom-left"))
    })

    // --- the formulas: next to their curves, or in a legend ---
    const formulaSize = 9
    const legend = []  // (over everything else: drawn last)
    if (spec.labels === "legend" && formulas.length > 0) {
        const rowH = 15
        const boxW = Math.min(frame.width * 0.6, 170), boxH = formulas.length * rowH + 6
        const bx = frame.x + 6, by = frame.y + 6
        legend.push(stroke([bx, by, bx + boxW, by, bx + boxW, by + boxH, bx, by + boxH], "#ffffff", 0.3,
                           { closed: true, fill: 255 }))
        legend.push(stroke([bx, by, bx + boxW, by, bx + boxW, by + boxH, bx, by + boxH], GRID_COLOR, 0.5, { closed: true }))
        formulas.forEach(function (fm, i) {
            const cy = by + 3 + rowH * (i + 0.5)
            legend.push(stroke([bx + 5, cy, bx + 19, cy], fm.color, fm.width, fm.dash ? { style: "dash" } : undefined))
            legend.push(label(bx + 24, cy, fm.tex, formulaSize, fm.color, "left"))
        })
    } else if (spec.labels === "curve") {
        formulas.forEach(function (fm, i) {
            // At the curve's end furthest to the right, above the curve where the formula lies (its width guessed
            // from its LaTeX), in the plot
            let best = null
            fm.lines.forEach(function (l) {
                const px = l[l.length - 2], py = l[l.length - 1]
                if (!best || px > best[0]) best = [px, py]
            })
            if (!best) return
            const guess = Math.min(frame.width * 0.8, 6 + 4.6 * fm.tex.replace(/\\[a-z]+|[{}]/g, "x").length)
            let top = best[1]
            fm.lines.forEach(function (l) {
                for (let k = 0; k + 1 < l.length; k += 2) {
                    if (l[k] >= best[0] - guess - 4 && l[k] <= best[0] + 2) top = Math.min(top, l[k + 1])
                }
            })
            const y = Math.max(frame.y + 14 + i * 14, top - 4)
            labels.push(label(best[0] - 2, y, fm.tex, formulaSize, fm.color, "bottom-right"))
        })
    }
    return { shapes: shapes.concat(labels, legend), errors: errors, analysed: analysed, ranges: r }
}

/// An axis name as math ("x", "t", "f(x)"); words ("time") as text
function nameTex(name) {
    const n = String(name).trim()
    if (/^[A-Za-z]$/.test(n) || /^[A-Za-z]\([A-Za-z]\)$/.test(n)) return n
    return "\\text{" + n.replace(/[\\{}$]/g, "") + "}"
}
