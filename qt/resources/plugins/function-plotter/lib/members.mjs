// Whether a plot was changed by hand since the plotter drew it (qt/docs/features/plugins.md, "Edit plot"): its
// members (the elements of its group) are remembered when it is inserted, as their count and where each lies within
// the plot (a stroke by the middle of its box, a text by its top-left corner, as a fraction of the span of those
// points, in thousandths). Moving or scaling the whole plot keeps every fraction; erasing (or cutting) a member changes
// the count, moving one its place. The order does not matter (a selection and a layer may list them differently).

const TOLERANCE = 15  // (thousandths of the plot: a member moved less than 1.5 % of the plot's size is not "changed")

/// The members' description to keep with the plot: {n, ink: [u, v, …], text: [u, v, …]}
export function membersOf(list) {
    const pts = list.map(function (e) {
        const ink = e.type === "stroke"
        return { ink: ink, x: ink ? e.x + e.width / 2 : e.x, y: ink ? e.y + e.height / 2 : e.y }
    })
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
    pts.forEach(function (p) {
        x0 = Math.min(x0, p.x)
        y0 = Math.min(y0, p.y)
        x1 = Math.max(x1, p.x)
        y1 = Math.max(y1, p.y)
    })
    const w = x1 - x0 > 1e-6 ? x1 - x0 : 1
    const h = y1 - y0 > 1e-6 ? y1 - y0 : 1
    const out = { n: list.length, ink: [], text: [] }
    pts.forEach(function (p) {
        const to = p.ink ? out.ink : out.text
        to.push(Math.round((p.x - x0) / w * 1000), Math.round((p.y - y0) / h * 1000))
    })
    return out
}

/// Every point of `a` has its own point of `b` near it (a and b: [u, v, …] of the same length)
function matches(a, b) {
    if (a.length !== b.length) return false
    const used = new Array(b.length / 2).fill(false)
    for (let i = 0; i < a.length; i += 2) {
        let found = false
        for (let j = 0; j < b.length && !found; j += 2) {
            if (!used[j / 2] && Math.abs(a[i] - b[j]) <= TOLERANCE && Math.abs(a[i + 1] - b[j + 1]) <= TOLERANCE) {
                used[j / 2] = true
                found = true
            }
        }
        if (!found) return false
    }
    return true
}

/// The plot's members are no longer what the plotter made (`kept`: membersOf then; a plot from before it was kept:
/// not changed)
export function changedByHand(kept, list) {
    if (!kept || !kept.ink || !kept.text || typeof kept.n !== "number") return false
    const now = membersOf(list)
    return kept.n !== now.n || !matches(kept.ink, now.ink) || !matches(kept.text, now.text)
}
