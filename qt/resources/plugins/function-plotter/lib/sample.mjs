// The function plotter's curves: adaptive sampling (more points where the curve bends), breaks at discontinuities
// and asymptotes (1/x, tan x, floor x: no vertical "spikes"), the curve cut exactly at the plot's border (the points
// where it leaves and comes back interpolated), and the points thinned where they lie on a straight line.
//
// A view maps the plot's ranges to page points: {x0, x1, y0, y1, left, top, width, height} (y up in the plot, down on
// the page). Curves come back as polylines in page points: [[x, y, x, y, …], …].

/// The page point of a plot point
export function toPage(view, x, y) {
    return [view.left + (x - view.x0) / (view.x1 - view.x0) * view.width,
            view.top + (view.y1 - y) / (view.y1 - view.y0) * view.height]
}

const BIG = 1e7  // (page points beyond this are capped: the clip works with finite numbers)

/// Cuts a segment to the box [l, r] × [t, b] (Liang–Barsky): the part inside, or null
export function clipSegment(ax, ay, bx, by, l, t, r, b) {
    let t0 = 0, t1 = 1
    const dx = bx - ax, dy = by - ay
    const p = [-dx, dx, -dy, dy]
    const q = [ax - l, r - ax, ay - t, b - ay]
    for (let i = 0; i < 4; ++i) {
        if (p[i] === 0) {
            if (q[i] < 0) return null
        } else {
            const u = q[i] / p[i]
            if (p[i] < 0) { if (u > t1) return null; if (u > t0) t0 = u }
            else { if (u < t0) return null; if (u < t1) t1 = u }
        }
    }
    return [ax + t0 * dx, ay + t0 * dy, ax + t1 * dx, ay + t1 * dy, t0 > 0, t1 < 1]
}

/// Collects segments into polylines cut to the view's box: a segment that leaves the box ends the line there
function Lines(view) {
    this.l = view.left
    this.t = view.top
    this.r = view.left + view.width
    this.b = view.top + view.height
    this.lines = []
    this.current = null
}
Lines.prototype.add = function (ax, ay, bx, by) {
    const c = clipSegment(ax, Math.max(-BIG, Math.min(BIG, ay)), bx, Math.max(-BIG, Math.min(BIG, by)),
                          this.l, this.t, this.r, this.b)
    if (!c) {
        this.end()
        return
    }
    const last = this.current
    if (last && Math.abs(last[last.length - 2] - c[0]) < 1e-9 && Math.abs(last[last.length - 1] - c[1]) < 1e-9 && !c[4]) {
        last.push(c[2], c[3])
    } else {
        this.end()
        this.current = [c[0], c[1], c[2], c[3]]
    }
    if (c[5]) this.end()  // (it leaves the box)
}
Lines.prototype.end = function () {
    if (this.current && this.current.length >= 4) this.lines.push(this.current)
    this.current = null
}
Lines.prototype.result = function () {
    this.end()
    return this.lines.map(function (l) { return simplify(l, 0.02) }).filter(function (l) { return l.length >= 4 })
}

/// Drops points that lie within `tol` points of the line through their neighbours
export function simplify(xy, tol) {
    if (xy.length <= 4) return xy
    const out = [xy[0], xy[1]]
    let ax = xy[0], ay = xy[1]
    for (let i = 2; i + 3 < xy.length; i += 2) {
        const bx = xy[i], by = xy[i + 1], cx = xy[i + 2], cy = xy[i + 3]
        const dx = cx - ax, dy = cy - ay
        const len = Math.hypot(dx, dy)
        const dist = len === 0 ? Math.hypot(bx - ax, by - ay) : Math.abs(dx * (ay - by) - dy * (ax - bx)) / len
        if (dist > tol) {
            out.push(bx, by)
            ax = bx
            ay = by
        }
    }
    out.push(xy[xy.length - 2], xy[xy.length - 1])
    return out
}

/// The polylines of y = f(x) in a view. options: {maxDepth: 12, tolerance: 0.05 (points)}
export function sampleFunction(f, view, options) {
    options = options || {}
    const maxDepth = options.maxDepth || 12
    const tol = options.tolerance || 0.05
    const jump = Math.max(1, view.height * 0.01)  // (a vertical step this big within the last subdivision: a break)
    const lines = new Lines(view)
    const sx = view.width / (view.x1 - view.x0)
    const sy = view.height / (view.y1 - view.y0)
    function X(x) { return view.left + (x - view.x0) * sx }
    function Y(y) { return view.top + (view.y1 - y) * sy }
    function ok(y) { return typeof y === "number" && isFinite(y) }
    function value(x) {
        const y = f(x)
        return ok(y) ? y : NaN
    }
    /// Where a function defined at `a` and not at `b` (or the other way round) stops being defined (bisection)
    function edge(a, fa, b) {
        for (let i = 0; i < 40; ++i) {
            const m = (a + b) / 2
            const fm = value(m)
            if (ok(fm)) { a = m; fa = fm } else { b = m }
        }
        return [a, fa]
    }
    function segment(a, fa, b, fb, depth) {
        const okA = ok(fa), okB = ok(fb)
        if (!okA && !okB) {
            if (depth < 4) {  // (a short defined stretch between two undefined points: looked for a few times)
                const m = (a + b) / 2
                const fm = value(m)
                segment(a, fa, m, fm, depth + 1)
                segment(m, fm, b, fb, depth + 1)
            } else {
                lines.end()
            }
            return
        }
        if (okA !== okB) {
            if (okA) {
                const e = edge(a, fa, b)
                if (e[0] > a) lines.add(X(a), Y(fa), X(e[0]), Y(e[1]))
                lines.end()
            } else {
                const e = edge(b, fb, a)
                lines.end()
                if (e[0] < b) lines.add(X(e[0]), Y(e[1]), X(b), Y(fb))
            }
            return
        }
        const m = (a + b) / 2
        const fm = value(m)
        const ax = X(a), ay = Y(fa), bx = X(b), by = Y(fb)
        if (ok(fm)) {
            // (wholly above or below the plot: nothing to see, nothing to refine)
            const my = Y(fm)
            if (Math.max(ay, by, my) < view.top || Math.min(ay, by, my) > view.top + view.height) {
                lines.end()
                return
            }
        }
        if (depth < maxDepth) {
            let refine = !ok(fm)
            if (!refine) {
                const mx = X(m), my = Y(fm)
                // (how far the middle is from the chord, in points; a long chord; a steep step)
                const dx = bx - ax, dy = by - ay
                const len = Math.hypot(dx, dy)
                const dist = len === 0 ? 0 : Math.abs(dx * (ay - my) - dy * (ax - mx)) / len
                refine = dist > tol || len > 12 || Math.abs(dy) > jump
            }
            if (refine) {
                segment(a, fa, m, fm, depth + 1)
                segment(m, fm, b, fb, depth + 1)
                return
            }
        }
        if (Math.abs(by - ay) > jump) {
            lines.end()  // (still a step after the last subdivision: a jump or an asymptote, not drawn as a line)
            return
        }
        lines.add(ax, ay, bx, by)
    }
    const n = Math.max(64, Math.min(1000, Math.round(view.width / 2)))
    let a = view.x0, fa = value(a)
    for (let i = 1; i <= n; ++i) {
        const b = view.x0 + (view.x1 - view.x0) * i / n
        const fb = value(b)
        segment(a, fa, b, fb, 0)
        a = b
        fa = fb
    }
    return lines.result()
}

/// The polylines of a curve (x(t), y(t)) for t in [t0, t1] in a view
export function sampleCurve(fx, fy, t0, t1, view, options) {
    options = options || {}
    const maxDepth = options.maxDepth || 12
    const tol = options.tolerance || 0.05
    const jump = Math.max(2, Math.min(view.width, view.height) * 0.05)
    const lines = new Lines(view)
    function point(t) {
        const x = fx(t), y = fy(t)
        if (typeof x !== "number" || typeof y !== "number" || !isFinite(x) || !isFinite(y)) return null
        return toPage(view, x, y)
    }
    function segment(a, pa, b, pb, depth) {
        if (!pa || !pb) {
            if (depth < 8) {
                const m = (a + b) / 2
                const pm = point(m)
                segment(a, pa, m, pm, depth + 1)
                segment(m, pm, b, pb, depth + 1)
            } else {
                lines.end()
            }
            return
        }
        const m = (a + b) / 2
        const pm = point(m)
        if (depth < maxDepth) {
            let refine = !pm
            if (!refine) {
                const dx = pb[0] - pa[0], dy = pb[1] - pa[1]
                const len = Math.hypot(dx, dy)
                const dist = len === 0 ? Math.hypot(pm[0] - pa[0], pm[1] - pa[1])
                                       : Math.abs(dx * (pa[1] - pm[1]) - dy * (pa[0] - pm[0])) / len
                refine = dist > tol || len > 12
            }
            if (refine) {
                segment(a, pa, m, pm, depth + 1)
                segment(m, pm, b, pb, depth + 1)
                return
            }
        }
        if (depth >= maxDepth && Math.hypot(pb[0] - pa[0], pb[1] - pa[1]) > jump) {
            lines.end()
            return
        }
        lines.add(pa[0], pa[1], pb[0], pb[1])
    }
    const n = 200
    let a = t0, pa = point(a)
    for (let i = 1; i <= n; ++i) {
        const b = t0 + (t1 - t0) * i / n
        const pb = point(b)
        segment(a, pa, b, pb, 0)
        a = b
        pa = pb
    }
    return lines.result()
}

/// A y range that shows f on [x0, x1] well: its values without the far ends of asymptotes. null: no values.
export function fitRange(fs, x0, x1) {
    const values = []
    const n = 400
    fs.forEach(function (f) {
        for (let i = 0; i <= n; ++i) {
            const y = f(x0 + (x1 - x0) * i / n)
            if (typeof y === "number" && isFinite(y)) values.push(y)
        }
    })
    if (values.length === 0) return null
    values.sort(function (a, b) { return a - b })
    const q = function (p) { return values[Math.min(values.length - 1, Math.max(0, Math.round(p * (values.length - 1))))] }
    let lo = values[0], hi = values[values.length - 1]
    const qlo = q(0.05), qhi = q(0.95)
    if (hi - lo > 4 * Math.max(qhi - qlo, 1e-9)) {  // (an asymptote: its tails would squash the rest)
        lo = qlo
        hi = qhi
        const pad = (hi - lo) * 0.35
        lo -= pad
        hi += pad
    }
    if (hi - lo < 1e-9) {
        lo -= 1
        hi += 1
    }
    // The x axis in view when it is near
    if (lo > 0 && lo < (hi - lo) * 0.5) lo = 0
    if (hi < 0 && -hi < (hi - lo) * 0.5) hi = 0
    const pad = (hi - lo) * 0.05
    return [lo === 0 ? 0 : lo - pad, hi === 0 ? 0 : hi + pad]
}

/// Where f changes sign on [x0, x1] and is near 0 there (not at a pole): the roots, by bisection
export function roots(f, x0, x1, n) {
    n = n || 800
    const out = []
    let a = x0, fa = f(a)
    for (let i = 1; i <= n; ++i) {
        const b = x0 + (x1 - x0) * i / n
        const fb = f(b)
        if (isFinite(fa) && isFinite(fb)) {
            if (fa === 0) {
                if (out.length === 0 || Math.abs(out[out.length - 1] - a) > 1e-9) out.push(a)
            } else if (fa * fb < 0) {
                let l = a, r = b, fl = fa
                for (let k = 0; k < 60; ++k) {
                    const m = (l + r) / 2, fm = f(m)
                    if (!isFinite(fm)) break
                    if (fl * fm <= 0) { r = m } else { l = m; fl = fm }
                }
                const x = (l + r) / 2
                const scale = Math.max(1, Math.abs(fa), Math.abs(fb))
                if (Math.abs(f(x)) < 1e-6 * scale) out.push(x)  // (a sign change at a pole is no root)
            }
        }
        a = b
        fa = fb
    }
    return out
}

/// The local minima and maxima of f on [x0, x1] (where its slope changes sign): [{x, y, kind: "min" | "max"}]
export function extrema(f, x0, x1, n) {
    n = n || 800
    const out = []
    const h = (x1 - x0) / n
    let prev = null
    for (let i = 0; i <= n; ++i) {
        const x = x0 + h * i
        const d = (f(x + h / 4) - f(x - h / 4)) / (h / 2)
        if (prev !== null && isFinite(d) && isFinite(prev.d) && ((prev.d > 0 && d <= 0) || (prev.d < 0 && d >= 0))) {
            // Golden section for the turning point between the two samples
            const kind = prev.d > 0 ? "max" : "min"
            let l = prev.x, r = x
            const g = (Math.sqrt(5) - 1) / 2
            for (let k = 0; k < 60; ++k) {
                const m1 = r - g * (r - l), m2 = l + g * (r - l)
                const f1 = f(m1), f2 = f(m2)
                if ((kind === "max") === (f1 > f2)) r = m2
                else l = m1
            }
            const xm = (l + r) / 2, ym = f(xm)
            if (isFinite(ym) && isFinite(f(l)) && isFinite(f(r))) out.push({ x: xm, y: ym, kind: kind })
        }
        prev = { x: x, d: d }
    }
    return out
}
