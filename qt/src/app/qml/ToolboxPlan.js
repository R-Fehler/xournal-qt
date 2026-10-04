// What the toolbox shows in the room it has (qt/docs/toolbox.md, "Short rails"): a pure function of the rail's length
// and its sections. The head (undo, redo) and the tail ("+", ⋯) are pinned; in between are the user's sections (split
// at the dividers) and the fixed tools (hand, select, write on the page, …). When they do not fit, sections become
// stacks, one by one: the fixed tools first, then the user's sections from the end. A stack takes one cell: the entry
// used last of that section (the active one, if it is there) with dots for how many it holds; a tap uses it, a tap
// again or a long press opens its list. Only when every section is a stack and it still does not fit, the middle
// scrolls.
.pragma library

/// input: { length, cell, divider (the room a divider takes), head (cells), tail (cells),
///          sections: [count, …] (the user's), fixed (count of fixed tools), slack (px kept free when growing) }
/// returns: { folded: [bool per section], fixedFolded: bool, scroll: bool, need }
function plan(input) {
    const cell = input.cell
    const sections = input.sections || []
    const folded = sections.map(function() { return false })
    let fixedFolded = false
    const room = input.length - (input.slack || 0)
    function need() {
        let n = (input.head + input.tail) * cell
        for (let i = 0; i < sections.length; ++i) n += (folded[i] ? 1 : sections[i]) * cell
        n += Math.max(0, sections.length - 1) * input.divider
        if (input.fixed > 0) n += input.divider + (fixedFolded ? 1 : input.fixed) * cell
        return n
    }
    // The fixed tools first (the user's own tools are what one reaches for), then the user's sections from the end
    if (need() > room && input.fixed > 1) fixedFolded = true
    for (let i = sections.length - 1; i >= 0 && need() > room; --i) {
        if (sections[i] > 1) folded[i] = true
    }
    const n = need()
    return { folded: folded, fixedFolded: fixedFolded, scroll: n > input.length, need: n }
}

/// How many cells of the user's sections and fixed tools a plan shows (for tests and the hysteresis)
function cells(input, p) {
    let n = 0
    for (let i = 0; i < input.sections.length; ++i) n += p.folded[i] ? 1 : input.sections[i]
    return n + (input.fixed > 0 ? (p.fixedFolded ? 1 : input.fixed) : 0)
}
