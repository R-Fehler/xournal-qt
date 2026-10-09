// The function plotter's axes: where the ticks go (1, 2, 5 × 10^n, or multiples of π), how their numbers are written
// (as LaTeX for the Markdown boxes), and a range rounded out to the ticks.

const PI = Math.PI

/// The nice step (1, 2 or 5 × 10^n) for a range drawn `length` points long, so that ticks are at least `minGap`
/// points apart
export function niceStep(range, length, minGap) {
    if (!(range > 0) || !(length > 0)) return 1
    const raw = range * minGap / length  // (the smallest step that keeps the gap)
    const p = Math.pow(10, Math.floor(Math.log10(raw)))
    for (const m of [1, 2, 5, 10]) {
        if (m * p >= raw * (1 - 1e-9)) return m * p
    }
    return 10 * p
}

/// The step in multiples of π: π/4, π/2, π, 2π, 4π … (as {num, den} of π), at least `minGap` points apart
export function piStep(range, length, minGap) {
    const raw = range * minGap / length
    const candidates = [[1, 4], [1, 2], [1, 1], [2, 1], [4, 1], [8, 1], [16, 1], [32, 1]]
    for (const c of candidates) {
        if (c[0] / c[1] * PI >= raw * (1 - 1e-9)) return { num: c[0], den: c[1], value: c[0] / c[1] * PI }
    }
    return { num: 64, den: 1, value: 64 * PI }
}

/// The ticks of [min, max] for a step (values, exactly multiples: no 0.30000000000000004)
export function ticks(min, max, step) {
    const out = []
    if (!(step > 0) || !(max > min)) return out
    const first = Math.ceil(min / step - 1e-9)
    const last = Math.floor(max / step + 1e-9)
    if (last - first > 2000) return out
    for (let k = first; k <= last; ++k) out.push({ k: k, v: k * step })
    return out
}

function gcd(a, b) { return b === 0 ? a : gcd(b, a % b) }

/// k·(num/den)·π as LaTeX: \frac{\pi}{2}, \pi, \frac{3\pi}{2}, 2\pi, -\frac{\pi}{4}
export function piLabel(k, num, den) {
    if (k === 0) return "0"
    let n = Math.abs(k) * num, d = den
    const g = gcd(n, d)
    n /= g
    d /= g
    const sign = k < 0 ? "-" : ""
    const top = (n === 1 ? "" : String(n)) + "\\pi"
    return sign + (d === 1 ? top : "\\frac{" + top + "}{" + d + "}")
}

/// How many decimals a step needs (0.25 → 2, 0.5 → 1, 2 → 0)
export function decimalsOf(step) {
    for (let d = 0; d < 10; ++d) {
        const scaled = step * Math.pow(10, d)
        if (Math.abs(scaled - Math.round(scaled)) < 1e-6 * Math.max(1, scaled)) return d
    }
    return 10
}

/// A tick's number as LaTeX with the step's decimals ("-2", "0{,}5" with a decimal comma)
export function numberLabel(v, step, decimalComma) {
    const d = decimalsOf(step)
    let s = Math.abs(v) < step * 1e-6 ? "0" : v.toFixed(Math.min(d, 8))
    if (s.indexOf(".") >= 0) s = s.replace(/0+$/, "").replace(/\.$/, "")
    if (s === "-0") s = "0"
    if (Math.abs(v) >= 1e6 || (Math.abs(v) < 1e-4 && v !== 0 && Math.abs(v) >= step * 1e-6)) {
        const e = Math.floor(Math.log10(Math.abs(v)))
        const m = Math.round(v / Math.pow(10, e) * 1000) / 1000
        s = (m === 1 ? "" : String(m) + " \\cdot ") + "10^{" + e + "}"
    }
    if (decimalComma) s = s.replace(".", "{,}")
    return s
}

/// A number for the dialog's messages and the marks' coordinates (with the decimal comma; at most 3 decimals)
export function shortNumber(v, decimalComma) {
    let s = (Math.round(v * 1000) / 1000).toString()
    if (s === "-0") s = "0"
    return decimalComma ? s.replace(".", ",") : s
}

/// [min, max] widened to whole steps (an auto-fitted range ends on ticks)
export function roundOut(min, max, step) {
    return [Math.floor(min / step + 1e-9) * step, Math.ceil(max / step - 1e-9) * step]
}
