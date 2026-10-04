.pragma library
// Where the tool bar's buttons go (qt/docs/adaptive-layout.md, "The tool bar"): a pure function of the room there is
// and the buttons offered. The bar is a flexible space filler: every group has several forms, and the bar takes the
// richest that fits, in this order of compression as the room runs short:
//   1. everything expanded: all palette colors, all five widths
//   2. the widths become one cycling width button
//   3. the colors become the current color, the recent ones (at least 4; as many as fit) and a palette button
//   4. low-priority buttons go into the "more tools" overflow, one by one (LADDER)
//   5. the colors become one cycling color button (phone-sized rooms only)
//   6. last resort (phones): the tools that are otherwise never hidden, into the overflow too
// ⋮ (and "more tools" once something is in it) is pinned at the end, outside anything that scrolls. Undo and redo (the
// group "edit") lead the bar and are never in the ladder: they never go into "more tools".
// Layouts: "row" (one row), "twoRows" (tools on the first row, colors, widths and the rest on the second), "rail"
// (a column of two, at a side), "grid" (the tools of the compact chrome: six columns, nothing overflows) and
// "merged" (a text document: everything in the overflow, ⋮ in the format bar).

/// The steps of the ladder after 1, in order (names of buttons, or a form of the colors or widths)
var LADDER = [
    "widths:single",
    "colors:recent",
    "new", "open", "save", "settings", "present", "fullScreen", "editAsNotes", "openExternally", "search",
    "addPage", "image", "emoji", "pdfText", "geometry", "shape",
    "colors:single",
    "sticky", "write", "text", "touchDrawing", "select", "hand"
]
/// The groups in their order, and the row of each in "twoRows"
var GROUPS = ["edit", "tools", "colors", "widths", "insert", "view", "file"]
var ROW_OF = { edit: 0, tools: 0, view: 0, colors: 1, widths: 1, insert: 1, file: 1 }
/// The groups of the buttons (the colors and widths are strips of their own)
var GROUP_OF = {
    undo: "edit", redo: "edit",
    pen: "tools", eraser: "tools", hand: "tools", touchDrawing: "tools", select: "tools", text: "tools", write: "tools",
    sticky: "tools", shape: "tools", geometry: "tools", pdfText: "tools", emoji: "tools",
    image: "insert", addPage: "insert",
    search: "view", fullScreen: "view", present: "view", settings: "view",
    new: "file", open: "file", save: "file", editAsNotes: "file", openExternally: "file"
}

var ICON = 48       // a button
var GAP = 2         // between two
var SWATCH = 40     // a color or width cell in a row
var SWATCH_H = 44
var DIVIDER = 9     // the line between groups
var MIN_RECENT = 4

/// The size of a strip along a row (colors: `count` cells; widths: 5 or 1)
function colorCells(state, presets) {
    if (state.colors === "full") return presets + 1           // the palette and "+"
    if (state.colors === "recent") return 1 + state.recent + 1 // the current color, the recent ones, the palette
    return 1                                                   // one cycling button
}
function widthCells(state) { return state.widths === "full" ? 5 : 1 }

/// The width of a row of `groups` (the buttons not cut), with the end (⋮, "more tools") if `end`
function rowWidth(groups, items, state, input, end) {
    var total = 0, parts = 0
    for (var g = 0; g < groups.length; ++g) {
        var w = groupWidth(groups[g], items, state, input)
        if (w <= 0) continue
        total += (parts > 0 ? DIVIDER + GAP : 0) + w
        ++parts
    }
    if (end) total += (parts > 0 ? 2 * GAP : 0) + endWidth(state, input)
    return total
}
function groupWidth(group, items, state, input) {
    if (group === "colors") return input.colors ? colorCells(state, input.presets) * SWATCH : 0
    if (group === "widths") return input.widths ? widthCells(state) * SWATCH : 0
    var n = 0
    for (var i = 0; i < items.length; ++i)
        if (GROUP_OF[items[i]] === group && !state.cut[items[i]]) ++n
    return n > 0 ? n * ICON + (n - 1) * GAP : 0
}
function endWidth(state, input) {
    var n = 1 + (state.more ? 1 : 0) + (input.endEmoji ? 1 : 0)
    return n * ICON + (n - 1) * GAP
}

/// The height of a column (a rail): each group in `columns`, strips on lines of their own
function columnHeight(groups, items, state, input, columns) {
    var total = 0, parts = 0
    for (var g = 0; g < groups.length; ++g) {
        var h = groupHeight(groups[g], items, state, input, columns)
        if (h <= 0) continue
        total += (parts > 0 ? DIVIDER + GAP : 0) + h
        ++parts
    }
    return total + (parts > 0 ? DIVIDER + GAP : 0) + ICON  // (the end: ⋮ and "more tools" side by side)
}
function groupHeight(group, items, state, input, columns) {
    if (group === "colors")
        return input.colors ? Math.ceil(colorCells(state, input.presets) / columns) * (SWATCH_H + GAP) - GAP : 0
    if (group === "widths")
        return input.widths ? Math.ceil(widthCells(state) / columns) * (SWATCH_H + GAP) - GAP : 0
    var n = 0
    for (var i = 0; i < items.length; ++i)
        if (GROUP_OF[items[i]] === group && !state.cut[items[i]]) ++n
    return n > 0 ? Math.ceil(n / columns) * (ICON + GAP) - GAP : 0
}

function copyState(s) {
    var cut = {}
    for (var k in s.cut) cut[k] = s.cut[k]
    return { colors: s.colors, recent: s.recent, widths: s.widths, cut: cut, more: s.more }
}

/// Applies the ladder to one zone until `fits(state)`; the colors then take what is left (the filler)
function fitZone(items, state, fits, maxRecent, input) {
    var steps = 0
    for (var i = 0; i < LADDER.length && !fits(state); ++i) {
        var step = LADDER[i]
        if (step === "widths:single") {
            if (!input.widths || state.widths !== "full") continue
            state.widths = "single"
        } else if (step === "colors:recent") {
            if (!input.colors || state.colors !== "full") continue
            state.colors = "recent"
            state.recent = Math.min(MIN_RECENT, maxRecent)
        } else if (step === "colors:single") {
            if (!input.colors || state.colors === "single") continue
            state.colors = "single"
        } else {
            if (items.indexOf(step) < 0 || state.cut[step]) continue
            state.cut[step] = true
            state.more = true
        }
        ++steps
    }
    // The filler: as many recent colors as fit (in the zone that has the colors)
    if (input.colors && state.colors === "recent") {
        while (state.recent < maxRecent) {
            state.recent += 1
            if (!fits(state)) { state.recent -= 1; break }
        }
    }
    return state
}

/// The tools shown in any class above a phone (qt/docs/ui-adaptive-audit.md, D3), besides the colors and the width
var NEVER_HIDDEN = ["pen", "eraser", "hand", "touchDrawing", "select", "text", "write", "sticky"]
/// A plan hides one of them, or has the colors down to one button
function hidesImportant(result) {
    if (result.colors === "single") return true
    for (var i = 0; i < NEVER_HIDDEN.length; ++i)
        if (result.overflow.indexOf(NEVER_HIDDEN[i]) >= 0) return true
    return false
}

/// A measure of how much a plan shows (for the hysteresis: a wider bar takes a richer plan only with room to spare)
function richness(result) {
    var r = -result.overflow.length * 1000
    r += result.colors === "full" ? 500 : result.colors === "recent" ? 100 + result.recent : 0
    r += result.widths === "full" ? 50 : 0
    return r
}

/// The plan. input: {
///   layout: "row" | "twoRows" | "rail" | "grid" | "merged",
///   width, height: the room of the bar's content,
///   items: the names of the buttons offered, in their order,
///   colors, widths: whether the strips are offered (not for a text document),
///   presets: the number of palette colors, recents: how many other colors there are to show (recent or palette),
///   endEmoji: the emoji button sits at the end (merged)
/// }
/// Returns { layout, colors, recent, widths, overflow: [names], more, placed: {name: {x, y, row}}, dividers:
/// [{x, y, w, h}], stripColumns, cell, contentWidth, contentHeight, end: {x, y} }
function plan(input) {
    var items = input.items.slice()
    var state = { colors: "full", recent: 0, widths: "full", cut: {}, more: false }
    var maxRecent = Math.max(MIN_RECENT, input.recents || 0)
    var layout = input.layout
    if (layout === "merged") {
        for (var m = 0; m < items.length; ++m) {
            state.cut[items[m]] = true
            state.more = true
        }
    } else if (layout === "row") {
        fitZone(items, state, function(s) { return rowWidth(GROUPS, items, s, input, true) <= input.width }, maxRecent, input)
    } else if (layout === "twoRows") {
        var row1 = GROUPS.filter(function(g) { return ROW_OF[g] === 0 })
        var row2 = GROUPS.filter(function(g) { return ROW_OF[g] === 1 })
        var items2 = items.filter(function(n) { return ROW_OF[GROUP_OF[n]] === 1 })
        var items1 = items.filter(function(n) { return ROW_OF[GROUP_OF[n]] === 0 })
        fitZone(items2, state, function(s) { return rowWidth(row2, items2, s, input, false) <= input.width }, maxRecent, input)
        // (the strips belong to the second row: the first one only cuts buttons)
        var strips = { colors: false, widths: false, presets: 0 }
        fitZone(items1, state, function(s) { return rowWidth(row1, items1, s, strips, true) <= input.width }, maxRecent, strips)
    } else if (layout === "rail") {
        fitZone(items, state, function(s) { return columnHeight(GROUPS, items, s, input, 2) <= input.height }, maxRecent, input)
    }
    // ("grid": everything, the compact chrome's tools scroll)
    return place(input, items, state)
}

function place(input, items, state) {
    var result = {
        layout: input.layout, colors: state.colors, recent: state.recent, widths: state.widths, more: state.more,
        overflow: items.filter(function(n) { return !!state.cut[n] }), placed: {}, dividers: [], stripColumns: -1,
        cell: SWATCH, contentWidth: 0, contentHeight: 0, end: { x: 0, y: 0 }
    }
    var layout = input.layout
    if (layout === "merged") return result
    var vertical = layout === "rail" || layout === "grid"
    var columns = layout === "grid" ? 6 : 2
    var cellW = vertical ? Math.floor((input.width + GAP) / columns) : 0
    var x = 0, y = 0
    function groupItems(g) {
        return items.filter(function(n) { return GROUP_OF[n] === g && !state.cut[n] })
    }
    function rows(groups, rowIndex) {
        var parts = 0
        for (var g = 0; g < groups.length; ++g) {
            var group = groups[g]
            var names = group === "colors" || group === "widths" ? (input[group] ? [group] : []) : groupItems(group)
            if (names.length === 0) continue
            if (parts > 0) {
                if (vertical) {
                    result.dividers.push({ x: 0, y: y + 1, w: input.width, h: 1 })
                    y += DIVIDER + GAP
                } else {
                    result.dividers.push({ x: x + 4, y: rowIndex * (ICON + GAP) + 10, w: 1, h: ICON - 20 })
                    x += DIVIDER + GAP
                }
            }
            ++parts
            if (vertical) {
                if (group === "colors" || group === "widths") {
                    var cells = group === "colors" ? colorCells(state, input.presets) : widthCells(state)
                    result.placed[group] = { x: 0, y: y, row: 0 }
                    y += Math.ceil(cells / columns) * (SWATCH_H + GAP)
                } else {
                    for (var i = 0; i < names.length; ++i) {
                        result.placed[names[i]] = { x: (i % columns) * cellW + (cellW - ICON) / 2,
                                                    y: y + Math.floor(i / columns) * (ICON + GAP), row: 0 }
                    }
                    y += Math.ceil(names.length / columns) * (ICON + GAP)
                }
            } else {
                var rowY = rowIndex * (ICON + GAP)
                if (group === "colors" || group === "widths") {
                    var n = group === "colors" ? colorCells(state, input.presets) : widthCells(state)
                    result.placed[group] = { x: x, y: rowY + (ICON - SWATCH_H) / 2, row: rowIndex }
                    x += n * SWATCH + GAP
                } else {
                    for (var j = 0; j < names.length; ++j) {
                        result.placed[names[j]] = { x: x, y: rowY, row: rowIndex }
                        x += ICON + GAP
                    }
                }
            }
        }
    }
    if (layout === "twoRows") {
        rows(GROUPS.filter(function(g) { return ROW_OF[g] === 0 }), 0)
        x = 0
        rows(GROUPS.filter(function(g) { return ROW_OF[g] === 1 }), 1)
        result.contentHeight = 2 * ICON + GAP
        result.end = { x: input.width - endWidth(state, input), y: 0 }
    } else if (layout === "row") {
        rows(GROUPS, 0)
        result.contentHeight = ICON
        result.end = { x: input.width - endWidth(state, input), y: 0 }
    } else {
        rows(GROUPS, 0)
        result.stripColumns = columns
        result.cell = cellW
        if (layout === "rail") {
            result.dividers.push({ x: 0, y: y + 1, w: input.width, h: 1 })
            y += DIVIDER + GAP
            // (at the bottom when there is room: ⋮ stays at the end, never scrolled away)
            result.end = { x: 0, y: Math.max(y, input.height - ICON) }
            result.contentHeight = result.end.y + ICON
        } else {
            result.end = { x: 0, y: y }  // (the grid: ⋮ as one more cell)
            result.contentHeight = y + ICON
        }
    }
    result.contentWidth = input.width
    return result
}
