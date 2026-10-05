// What the toolbox shows in the room it has (qt/docs/toolbox.md, "Short rails"): a pure function of the rail's length
// and its sections. The head (undo, redo) and the tail ("+", ⋯) are pinned; in between are the user's sections (split
// at the dividers) and the fixed tools (hand, select, snip, write on the page, …). The rail uses all the room it has:
// every tool on its own while they fit. When they do not, sections become stacks, one by one: the fixed tools first,
// from their end (the last ones go into one stack after the others, one more at a time, until all of them are in it;
// qt/copy-tools), then the user's sections from the end; what a fold freed beyond the need goes back to the sections
// before it, then to the fixed tools (qt/rail-fill). A stack takes one cell: the entry used last of that section (the
// active one, if it is there) with dots for how many it holds; a tap uses it, a tap again or a long press opens its
// list. Only when every section is a stack and it still does not fit, the middle scrolls.
.pragma library

/// input: { length, cell, divider (the room a divider takes), head (cells), tail (cells),
///          sections: [count, …] (the user's), fixed (count of fixed tools), slack (px kept free when growing) }
/// returns: { folded: [bool per section], fixedShown: the first fixed tools shown on their own (the others are in the
///           stack), fixedFolded: bool (some are in the stack), scroll: bool, need }
function plan(input) {
    const cell = input.cell
    const sections = input.sections || []
    const folded = sections.map(function() { return false })
    let fixedShown = input.fixed
    const room = input.length - (input.slack || 0)
    function need() {
        let n = (input.head + input.tail) * cell
        for (let i = 0; i < sections.length; ++i) n += (folded[i] ? 1 : sections[i]) * cell
        n += Math.max(0, sections.length - 1) * input.divider
        if (input.fixed > 0) n += input.divider + (fixedShown + (fixedShown < input.fixed ? 1 : 0)) * cell
        return n
    }
    // The fixed tools first (the user's own tools are what one reaches for), from their end: the stack takes the last
    // two, then one more each time; then the user's sections from the end
    while (need() > room && input.fixed > 1 && fixedShown > 0)
        fixedShown = fixedShown === input.fixed ? Math.max(0, input.fixed - 2) : fixedShown - 1
    for (let i = sections.length - 1; i >= 0 && need() > room; --i) {
        if (sections[i] > 1) folded[i] = true
    }
    // A section folded may have freed more than was needed: the room left goes back, to the user's sections first
    // (from the start: the tools one reaches for first), then to the fixed tools, one at a time
    for (let i = 0; i < sections.length; ++i) {
        if (!folded[i]) continue
        folded[i] = false
        if (need() > room) folded[i] = true
    }
    while (fixedShown < input.fixed) {
        const was = fixedShown
        // (all but one on their own and the stack take as much room as all of them)
        fixedShown = was + 1 >= input.fixed - 1 ? input.fixed : was + 1
        if (need() > room) {
            fixedShown = was
            break
        }
    }
    const n = need()
    return { folded: folded, fixedShown: fixedShown, fixedFolded: fixedShown < input.fixed, scroll: n > input.length,
             need: n }
}

/// How many cells of the user's sections and fixed tools a plan shows (for tests and the hysteresis)
function cells(input, p) {
    let n = 0
    for (let i = 0; i < input.sections.length; ++i) n += p.folded[i] ? 1 : input.sections[i]
    const shown = p.fixedShown === undefined ? input.fixed : p.fixedShown
    return n + (input.fixed > 0 ? shown + (shown < input.fixed ? 1 : 0) : 0)
}
