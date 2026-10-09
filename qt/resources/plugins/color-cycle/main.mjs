// Color cycle: the tool in hand takes the next color of the palette chosen now (a port of Xournal++'s ColorCycle
// plugin; there a fixed list, here the palette, so it follows the user's choice of colors).
import { tools, ui } from "xournal"

export function cycle() {
    const colors = ui.palette().map(function (c) { return c.color.toLowerCase() })
    if (colors.length === 0) return
    const now = tools.get().color.toLowerCase()
    const i = colors.indexOf(now)
    tools.setColor(colors[(i + 1) % colors.length])
}
