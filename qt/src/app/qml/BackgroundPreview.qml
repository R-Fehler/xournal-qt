// A small drawing of a page background (upstream's page types: plain, ruled, lined, graph, dotted, ...).
import QtQuick

Canvas {
    id: preview
    property string format: "plain"
    property color lineColor: "#8fb4dc"
    onFormatChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = "#ffffff"
        ctx.fillRect(0, 0, width, height)
        ctx.strokeStyle = lineColor
        ctx.fillStyle = lineColor
        ctx.lineWidth = 1
        const s = Math.max(5, width / 10)
        function hline(y) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke() }
        function vline(x) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke() }
        switch (format) {
        case "ruled":
        case "lined":
            for (let y = s * 1.5; y < height; y += s) hline(Math.round(y) + 0.5)
            if (format === "lined") {
                ctx.strokeStyle = "#e57373"
                vline(Math.round(s * 1.6) + 0.5)
            }
            break
        case "graph":
            for (let y = s / 2; y < height; y += s / 1.5) hline(Math.round(y) + 0.5)
            for (let x = s / 2; x < width; x += s / 1.5) vline(Math.round(x) + 0.5)
            break
        case "dotted":
            for (let y = s / 2; y < height; y += s / 1.5)
                for (let x = s / 2; x < width; x += s / 1.5) ctx.fillRect(x - 1, y - 1, 2, 2)
            break
        case "isodotted":
        case "isograph": {
            const d = s / 1.3, h = d * Math.sqrt(3) / 2
            if (format === "isograph") {
                for (let y = 0; y < height; y += h) hline(Math.round(y) + 0.5)
                ctx.beginPath()
                for (let x = -height; x < width + height; x += d) {
                    ctx.moveTo(x, 0); ctx.lineTo(x + height / Math.sqrt(3), height)
                    ctx.moveTo(x, 0); ctx.lineTo(x - height / Math.sqrt(3), height)
                }
                ctx.stroke()
            } else {
                let row = 0
                for (let y = h / 2; y < height; y += h, ++row)
                    for (let x = (row % 2) * d / 2 + d / 2; x < width; x += d) ctx.fillRect(x - 1, y - 1, 2, 2)
            }
            break
        }
        case "staves":
            for (let top = s; top + s * 1.2 < height; top += s * 2.4)
                for (let i = 0; i < 5; ++i) hline(Math.round(top + i * s * 0.3) + 0.5)
            break
        default:
            break
        }
    }
}
