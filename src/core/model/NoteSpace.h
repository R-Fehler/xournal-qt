/*
 * Xournal++
 *
 * xournal-qt: space for notes around a page's content (qt/docs/note-space.md).
 *
 * A page with note space is larger than its "slide" by these amounts (points). Its PDF background is drawn at
 * (left, top), at its own scale; other backgrounds fill the whole page. Saved as the page attribute
 * notespace="left top right bottom" (only when not empty).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

struct NoteSpace {
    double left = 0;
    double top = 0;
    double right = 0;
    double bottom = 0;

    bool empty() const { return left == 0 && top == 0 && right == 0 && bottom == 0; }
    bool operator==(const NoteSpace& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    bool operator!=(const NoteSpace& o) const { return !(*this == o); }
};
