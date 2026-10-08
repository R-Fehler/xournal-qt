/*
 * xournal-qt: handwriting copied as text (qt/docs/features/handwriting-search.md, "Copy handwriting as text").
 *
 * The recogniser's readings of the ink words (InkText.h) become text on the clipboard, never in the document: the
 * tool "Copy handwriting as text" takes the words a sweep goes over (sweptWords), "Copy as text" of a selection all
 * its words; inReadingOrder puts them in order:
 *  - lines from the words' heights: a word joins the line whose band (the mean middle and height of its words) it
 *    overlaps by at least half the smaller height, else it starts a line. Lines top to bottom, words left to right;
 *  - words by a space, lines by a line break; where two lines are further apart than one and a half of their height
 *    and twice the usual gap between the lines (the lower median), an empty line between them (a paragraph);
 *  - each word its best reading, as recognised (case, punctuation), with its confidence (unsure: below
 *    ink::WEAK_P, the search's mark for unsure words);
 *  - words written at an angle (InkLayout.h) make lines in their own frame by the same rule, words in the direction
 *    they were written in; such a line is one line of the text, a paragraph of its own (empty lines around it),
 *    placed among the others by the top of the box around it on the page.
 * A sweep takes a word at an angle by its turned box.
 * Qt-free apart from the geometry and the strings; any thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QPointF>
#include <QRectF>
#include <QString>

#include "session/InkText.h"

namespace xqt::hwr {

struct CopiedWord {
    QString text;    ///< the best reading
    float conf = 0;  ///< how sure the recogniser is of it (0..1)
    QRectF box;      ///< page points; with an angle: upright around its middle (ink::Word::box)
    float angle = 0;  ///< the direction it was written in (ink::Word::angle)
    bool unsure() const { return conf < ink::WEAK_P; }
    /// The box around it on the page.
    QRectF bounds() const;
};

struct CopiedText {
    QString text;                                ///< words by spaces, lines by '\n' (an empty line between paragraphs)
    std::vector<std::vector<CopiedWord>> lines;  ///< in reading order
    QRectF box;                                  ///< of all its words
    int words = 0;
    int unsure = 0;  ///< words the recogniser was unsure of
    bool empty() const { return words == 0; }
};

/// The words in reading order, as text.
CopiedText inReadingOrder(std::vector<CopiedWord> words);

/// The words of a page's handwriting that a sweep takes: those whose box (`reach` points bigger) the path touches, and
/// those whose middle lies inside the path closed (a loop drawn around them). `path`: page points.
std::vector<CopiedWord> sweptWords(const ink::PageText& ink, const std::vector<QPointF>& path, double reach);
/// All words of the handwriting (a selection's).
std::vector<CopiedWord> allWords(const ink::PageText& ink);

}  // namespace xqt::hwr
