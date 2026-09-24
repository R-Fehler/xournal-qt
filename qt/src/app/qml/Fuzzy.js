.pragma library
// The fuzzy search (FuzzyQuery.h) in QML: a name with the characters its query matched highlighted, as fzf shows them.

/// `text` as styled text (Text.StyledText) with the characters at `marks` (UTF-16 indices, sorted) in `color`;
/// "" when nothing is marked (the caller shows the plain text).
function marked(text, marks, color) {
    if (!marks || marks.length === 0 || !text) return ""
    const esc = function(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
    }
    let out = ""
    let i = 0
    let m = 0
    while (i < text.length) {
        while (m < marks.length && marks[m] < i) m++
        if (m < marks.length && marks[m] === i) {
            let j = i
            while (m < marks.length && marks[m] === j && j < text.length) { j++; m++ }
            out += "<font color=\"" + color + "\"><u>" + esc(text.substring(i, j)) + "</u></font>"
            i = j
        } else {
            const next = m < marks.length ? Math.min(marks[m], text.length) : text.length
            out += esc(text.substring(i, next))
            i = next
        }
    }
    return out
}
