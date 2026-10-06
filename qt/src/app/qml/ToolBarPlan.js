.pragma library
// Where the command bar's buttons go (qt/docs/toolbox.md, "The command bar"): a pure function of the room there is and
// the buttons offered. The bar is one row at the top; as the room runs short, its buttons give way one by one, in the
// order of LADDER: the entries of ⋮ shown as buttons (PROMOTED) go back into ⋮, the others into "more tools".
// ⋮ (and "more tools" once something is in it) is pinned at the end. Undo and redo (the group "edit") lead the bar and
// are never in the ladder: they never go into "more tools".
// Layouts: "row" (the bar) and "merged" (a text document: everything in the overflow but what its format bar keeps,
// ⋮ in the format bar). (The classic tool bar's colors, widths, two rows, rails and grid were removed in 0.8.0.)

/// Entries of ⋮ shown as buttons where there is room (qt/docs/toolbox.md, "The command bar"): the first to give way,
/// back into ⋮ (not into "more tools"), in this order - the tags first, sharing last (qt/ui-rework: reading, the replay
/// of the writing, a milestone of the version history, the tags; qt/zen: Zen)
var PROMOTED = ["tags", "favourite", "bookmark", "print", "milestone", "replay", "zen", "read", "share"]
/// The steps of the ladder, in order (names of buttons)
var LADDER = PROMOTED.concat([
    // (Open externally after Search, the stickers and recording: for a file shown here, a text file or an image, it
    // is how the file is edited)
    "new", "open", "save", "settings", "present", "fullScreen", "editAsNotes", "search", "sticker", "record",
    "openExternally",
    // (the toolbox's fixed tools, where they are not lent to it)
    "snip", "addPage", "image", "emoji", "pdfText", "geometry",
    "write", "touchDrawing", "select", "hand"
])
/// The groups in their order
var GROUPS = ["edit", "tools", "insert", "view", "doc", "file"]
/// The groups of the buttons
var GROUP_OF = {
    undo: "edit", redo: "edit",
    hand: "tools", touchDrawing: "tools", select: "tools", snip: "tools", write: "tools", geometry: "tools",
    pdfText: "tools", emoji: "tools",
    image: "insert", sticker: "insert", record: "insert", addPage: "insert",
    search: "view", fullScreen: "view", present: "view", read: "view", zen: "view", replay: "view", settings: "view",
    new: "file", open: "file", save: "file", milestone: "file", editAsNotes: "file", openExternally: "file",
    share: "doc", print: "doc", bookmark: "doc", favourite: "doc", tags: "doc"
}

var ICON = 48       // a button
var GAP = 2         // between two
var DIVIDER = 9     // the line between groups

/// The width of the row (the buttons not cut), with the end (⋮, "more tools")
function rowWidth(items, state) {
    var total = 0, parts = 0
    for (var g = 0; g < GROUPS.length; ++g) {
        var w = groupWidth(GROUPS[g], items, state)
        if (w <= 0) continue
        total += (parts > 0 ? DIVIDER + GAP : 0) + w
        ++parts
    }
    return total + (parts > 0 ? 2 * GAP : 0) + endWidth(state)
}
function groupWidth(group, items, state) {
    var n = 0
    for (var i = 0; i < items.length; ++i)
        if (GROUP_OF[items[i]] === group && !state.cut[items[i]]) ++n
    return n > 0 ? n * ICON + (n - 1) * GAP : 0
}
function endWidth(state) {
    var n = 1 + (state.more ? 1 : 0)
    return n * ICON + (n - 1) * GAP
}

/// Applies the ladder until the row fits
function fit(items, state, width) {
    for (var i = 0; i < LADDER.length && rowWidth(items, state) > width; ++i) {
        var step = LADDER[i]
        if (items.indexOf(step) < 0 || state.cut[step]) continue
        state.cut[step] = true
        if (PROMOTED.indexOf(step) < 0) state.more = true  // (a promoted one goes back into ⋮)
    }
    return state
}

/// A measure of how much a plan shows (for the hysteresis: a wider bar takes a richer plan only with room to spare)
function richness(result) { return -result.overflow.length }

/// The plan. input: {
///   layout: "row" | "merged",
///   width, height: the room of the bar's content,
///   items: the names of the buttons offered, in their order,
///   keep: (merged) the names its format bar keeps
/// }
/// Returns { layout, overflow: [names], more, placed: {name: {x, y, row}}, dividers: [{x, y, w, h}], contentWidth,
/// contentHeight, end: {x, y} } (merged: `kept`, the names kept, instead of places)
function plan(input) {
    var items = input.items.slice()
    var state = { cut: {}, more: false }
    if (input.layout === "merged") {
        // (a text document: everything in "more tools", but what its format bar keeps; ⋮'s entries back in ⋮)
        var keep = input.keep || []
        for (var m = 0; m < items.length; ++m) {
            if (keep.indexOf(items[m]) >= 0) continue
            state.cut[items[m]] = true
            if (PROMOTED.indexOf(items[m]) < 0) state.more = true
        }
    } else {
        fit(items, state, input.width)
    }
    return place(input, items, state)
}

function place(input, items, state) {
    var result = {
        layout: input.layout, more: state.more,
        overflow: items.filter(function(n) { return !!state.cut[n] }), placed: {}, dividers: [],
        contentWidth: input.width, contentHeight: ICON, end: { x: input.width - endWidth(state), y: 0 }
    }
    if (input.layout === "merged") {
        result.kept = items.filter(function(n) { return !state.cut[n] })
        return result
    }
    var x = 0, parts = 0
    for (var g = 0; g < GROUPS.length; ++g) {
        var group = GROUPS[g]
        var names = items.filter(function(n) { return GROUP_OF[n] === group && !state.cut[n] })
        if (names.length === 0) continue
        if (parts > 0) {
            result.dividers.push({ x: x + 4, y: 10, w: 1, h: ICON - 20 })
            x += DIVIDER + GAP
        }
        ++parts
        for (var j = 0; j < names.length; ++j) {
            result.placed[names[j]] = { x: x, y: 0, row: 0 }
            x += ICON + GAP
        }
    }
    return result
}
