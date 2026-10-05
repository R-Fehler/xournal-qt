.pragma library
// The line styles (upstream's StrokeStyle: plain, dashed, dash-dot, dotted) as the samples draw them: the buttons of
// the toolbox's editor and of the pen's options, the ink of a tool on the rail.
// Two things of Qt's Canvas to know (qt/docs/toolbox.md, "Line styles"): setLineDash() takes only a JavaScript array -
// a list that came through a model (a Repeater's modelData.dashes) is silently ignored and the line is drawn solid;
// and, as QPen does, it measures the dashes in widths of the line, not in pixels.

/// Upstream's dashes, in widths of the line (as it draws them, round caps), as a new JavaScript array
function dashes(style) {
    switch (style) {
    case "dash": return [6, 3]
    case "dashdot": return [6, 3, 0.5, 3]
    case "dot": return [0.5, 3]
    }
    return []
}

/// A short sample's dashes (a button, the ink on the rail: 20 to 30 px), in widths of a line `lineWidth` wide drawn
/// with butt caps: at least two dashes or three dots fit in it, whatever its width
function sampleDashes(style, lineWidth) {
    const w = Math.max(0.5, lineWidth)
    const dash = Math.max(4, 1.6 * w), gap = Math.max(2.5, 0.9 * w), dot = Math.max(1.5, 0.8 * w)
    const px = style === "dash" ? [dash, gap]
             : style === "dashdot" ? [dash, gap, dot, gap]
             : style === "dot" ? [dot, gap] : []
    return px.map(function(x) { return x / w })
}

/// The caps a sample is drawn with: round for a solid line, butt for dashes (round caps would close the gaps)
function sampleCap(style, solidCap) {
    return dashes(style).length > 0 ? "butt" : solidCap
}
